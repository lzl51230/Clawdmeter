#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t XINGZHI_SPLASH_GRID_SIZE = 20;
constexpr uint8_t XINGZHI_SPLASH_PALETTE_SIZE = 10;
constexpr uint8_t XINGZHI_SPLASH_GROUP_COUNT = 4;
constexpr uint32_t XINGZHI_SPLASH_ROTATE_INTERVAL_MS = 20000;

struct XingzhiSplashFrame {
    const uint8_t *cells = nullptr;
    const uint16_t *palette = nullptr;
    uint8_t width = XINGZHI_SPLASH_GRID_SIZE;
    uint8_t height = XINGZHI_SPLASH_GRID_SIZE;
    uint16_t hold_ms = 0;
};

struct XingzhiSplashSnapshot {
    bool valid = false;
    const char *name = "-";
    const char *category = "-";
    uint16_t animation_index = 0;
    uint16_t animation_count = 0;
    uint16_t frame_index = 0;
    uint16_t frame_count = 0;
    uint16_t hold_ms = 0;
    uint8_t group = 0;
    const char *group_name = "idle";
};

struct XingzhiSplashAnimState {
    uint16_t animation_index = 0;
    uint16_t frame_index = 0;
    uint32_t frame_started_ms = 0;
    uint32_t last_rotation_ms = 0;
    uint8_t group = 0;
    uint8_t group_rotation[XINGZHI_SPLASH_GROUP_COUNT] = {};
};

void xingzhi_splash_anim_init(XingzhiSplashAnimState *state, uint32_t now_ms = 0);
bool xingzhi_splash_anim_tick(XingzhiSplashAnimState *state, uint32_t now_ms);
bool xingzhi_splash_anim_select(XingzhiSplashAnimState *state, size_t animation_index, uint32_t now_ms);
bool xingzhi_splash_anim_next(XingzhiSplashAnimState *state, uint32_t now_ms);
bool xingzhi_splash_anim_set_group(XingzhiSplashAnimState *state, int group, uint32_t now_ms);
bool xingzhi_splash_anim_pick_group(XingzhiSplashAnimState *state, int group, uint32_t now_ms);
bool xingzhi_splash_anim_rotate_if_due(XingzhiSplashAnimState *state, uint32_t now_ms);
bool xingzhi_splash_anim_get_frame(const XingzhiSplashAnimState *state, XingzhiSplashFrame *frame);
XingzhiSplashSnapshot xingzhi_splash_anim_snapshot(const XingzhiSplashAnimState *state);
const char *xingzhi_splash_group_name(int group);
