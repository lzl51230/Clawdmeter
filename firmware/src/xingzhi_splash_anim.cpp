#include "xingzhi_splash_anim.h"

#include "splash_animations.h"

namespace {

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
    if (!state || !animation_has_frames(animation_at(animation_index))) {
        return false;
    }
    state->animation_index = static_cast<uint16_t>(animation_index);
    state->frame_index = 0;
    state->frame_started_ms = now_ms;
    return true;
}

bool xingzhi_splash_anim_next(XingzhiSplashAnimState *state, uint32_t now_ms) {
    if (!state || SPLASH_ANIM_COUNT == 0) {
        return false;
    }
    const size_t next_index = (static_cast<size_t>(state->animation_index) + 1) % SPLASH_ANIM_COUNT;
    return xingzhi_splash_anim_select(state, next_index, now_ms);
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
    return snapshot;
}
