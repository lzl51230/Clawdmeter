#include "xingzhi_splash_anim.h"

#include "splash_animations.h"

#include <string.h>

namespace {

constexpr uint8_t GROUP_MAX = 4;

const char *GROUP_NAMES[XINGZHI_SPLASH_GROUP_COUNT][GROUP_MAX] = {
    {"expression sleep", "idle breathe", "idle blink", "expression wink"},
    {"idle look around", "work think", "work coding", nullptr},
    {"dance sway", "expression surprise", "dance bounce", nullptr},
    {"dance bounce dj", "dance sway dj", "dance djmix", nullptr},
};

const splash_anim_def_t *animation_at(size_t index) {
    if (index >= SPLASH_ANIM_COUNT) {
        return nullptr;
    }
    return &splash_anims[index];
}

const splash_anim_def_t *current_animation(const XingzhiSplashAnimState *state) {
    if (!state) {
        return nullptr;
    }
    return animation_at(state->animation_index);
}

bool animation_has_frames(const splash_anim_def_t *animation) {
    return animation && animation->frame_count > 0 && animation->frames && animation->holds;
}

uint8_t clamp_group(int group) {
    if (group < 0) {
        return 0;
    }
    if (group >= XINGZHI_SPLASH_GROUP_COUNT) {
        return XINGZHI_SPLASH_GROUP_COUNT - 1;
    }
    return static_cast<uint8_t>(group);
}

uint8_t group_size(uint8_t group) {
    uint8_t size = 0;
    for (uint8_t slot = 0; slot < GROUP_MAX; ++slot) {
        if (GROUP_NAMES[group][slot]) {
            ++size;
        }
    }
    return size;
}

int animation_index_by_name(const char *name) {
    if (!name) {
        return -1;
    }
    for (int index = 0; index < SPLASH_ANIM_COUNT; ++index) {
        const splash_anim_def_t *animation = &splash_anims[index];
        if (animation->name && strcmp(animation->name, name) == 0) {
            return index;
        }
    }
    return -1;
}

int animation_index_for_group_slot(uint8_t group, uint8_t slot) {
    const char *name = GROUP_NAMES[group][slot];
    return animation_index_by_name(name);
}

bool animation_in_group(uint16_t animation_index, uint8_t group) {
    const splash_anim_def_t *animation = animation_at(animation_index);
    if (!animation || !animation->name) {
        return false;
    }
    const uint8_t size = group_size(group);
    for (uint8_t slot = 0; slot < size; ++slot) {
        const char *name = GROUP_NAMES[group][slot];
        if (name && strcmp(animation->name, name) == 0) {
            return true;
        }
    }
    return false;
}

bool set_animation(XingzhiSplashAnimState *state, size_t animation_index, uint32_t now_ms) {
    if (!state || !animation_has_frames(animation_at(animation_index))) {
        return false;
    }
    state->animation_index = static_cast<uint16_t>(animation_index);
    state->frame_index = 0;
    state->frame_started_ms = now_ms;
    state->last_rotation_ms = now_ms;
    return true;
}

uint16_t current_hold_ms(const XingzhiSplashAnimState *state, const splash_anim_def_t *animation) {
    if (!state || !animation_has_frames(animation) || state->frame_index >= animation->frame_count) {
        return 0;
    }
    return animation->holds[state->frame_index];
}

}  // namespace

void xingzhi_splash_anim_init(XingzhiSplashAnimState *state, uint32_t now_ms) {
    if (!state) {
        return;
    }
    state->animation_index = 0;
    state->frame_index = 0;
    state->frame_started_ms = now_ms;
    state->last_rotation_ms = now_ms;
    state->group = 0;
    for (uint8_t index = 0; index < XINGZHI_SPLASH_GROUP_COUNT; ++index) {
        state->group_rotation[index] = 0;
    }
}

bool xingzhi_splash_anim_tick(XingzhiSplashAnimState *state, uint32_t now_ms) {
    const splash_anim_def_t *animation = current_animation(state);
    if (!state || !animation_has_frames(animation)) {
        return false;
    }
    if (state->frame_index >= animation->frame_count) {
        state->frame_index = 0;
        state->frame_started_ms = now_ms;
        return true;
    }

    const uint16_t hold = current_hold_ms(state, animation);
    if (now_ms - state->frame_started_ms < hold) {
        return false;
    }

    state->frame_index = (state->frame_index + 1) % animation->frame_count;
    state->frame_started_ms = now_ms;
    return true;
}

bool xingzhi_splash_anim_select(XingzhiSplashAnimState *state, size_t animation_index, uint32_t now_ms) {
    return set_animation(state, animation_index, now_ms);
}

bool xingzhi_splash_anim_next(XingzhiSplashAnimState *state, uint32_t now_ms) {
    if (!state || SPLASH_ANIM_COUNT == 0) {
        return false;
    }
    const size_t next_index = (static_cast<size_t>(state->animation_index) + 1) % SPLASH_ANIM_COUNT;
    return xingzhi_splash_anim_select(state, next_index, now_ms);
}

bool xingzhi_splash_anim_pick_group(XingzhiSplashAnimState *state, int group, uint32_t now_ms) {
    if (!state || SPLASH_ANIM_COUNT == 0) {
        return false;
    }
    const uint8_t safe_group = clamp_group(group);
    const uint8_t size = group_size(safe_group);
    if (size == 0) {
        return false;
    }

    for (uint8_t attempt = 0; attempt < size; ++attempt) {
        const uint8_t slot = state->group_rotation[safe_group] % size;
        state->group_rotation[safe_group]++;
        const int animation_index = animation_index_for_group_slot(safe_group, slot);
        if (animation_index >= 0 && set_animation(state, static_cast<size_t>(animation_index), now_ms)) {
            state->group = safe_group;
            return true;
        }
    }
    return false;
}

bool xingzhi_splash_anim_set_group(XingzhiSplashAnimState *state, int group, uint32_t now_ms) {
    if (!state) {
        return false;
    }
    const uint8_t safe_group = clamp_group(group);
    if (state->group == safe_group && animation_in_group(state->animation_index, safe_group)) {
        return false;
    }
    return xingzhi_splash_anim_pick_group(state, safe_group, now_ms);
}

bool xingzhi_splash_anim_rotate_if_due(XingzhiSplashAnimState *state, uint32_t now_ms) {
    if (!state || now_ms - state->last_rotation_ms < XINGZHI_SPLASH_ROTATE_INTERVAL_MS) {
        return false;
    }
    return xingzhi_splash_anim_pick_group(state, state->group, now_ms);
}

bool xingzhi_splash_anim_get_frame(const XingzhiSplashAnimState *state, XingzhiSplashFrame *frame) {
    if (!frame) {
        return false;
    }
    *frame = {};

    const splash_anim_def_t *animation = current_animation(state);
    if (!state || !animation_has_frames(animation) || state->frame_index >= animation->frame_count) {
        return false;
    }

    frame->cells = animation->frames[state->frame_index];
    frame->palette = animation->palette;
    frame->width = XINGZHI_SPLASH_GRID_SIZE;
    frame->height = XINGZHI_SPLASH_GRID_SIZE;
    frame->hold_ms = animation->holds[state->frame_index];
    return true;
}

XingzhiSplashSnapshot xingzhi_splash_anim_snapshot(const XingzhiSplashAnimState *state) {
    XingzhiSplashSnapshot snapshot = {};
    snapshot.animation_count = SPLASH_ANIM_COUNT;

    const splash_anim_def_t *animation = current_animation(state);
    if (!state || !animation_has_frames(animation) || state->frame_index >= animation->frame_count) {
        return snapshot;
    }

    snapshot.valid = true;
    snapshot.name = animation->name ? animation->name : "-";
    snapshot.category = animation->category ? animation->category : "-";
    snapshot.animation_index = state->animation_index;
    snapshot.frame_index = state->frame_index;
    snapshot.frame_count = animation->frame_count;
    snapshot.hold_ms = animation->holds[state->frame_index];
    snapshot.group = state->group;
    snapshot.group_name = xingzhi_splash_group_name(state->group);
    return snapshot;
}

const char *xingzhi_splash_group_name(int group) {
    switch (clamp_group(group)) {
    case 1:
        return "normal";
    case 2:
        return "active";
    case 3:
        return "heavy";
    case 0:
    default:
        return "idle";
    }
}
