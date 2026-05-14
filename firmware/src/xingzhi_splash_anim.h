#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t XINGZHI_SPLASH_GRID_SIZE = 20;
constexpr uint8_t XINGZHI_SPLASH_PALETTE_SIZE = 10;

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
};

struct XingzhiSplashAnimState {
    uint16_t animation_index = 0;
    uint16_t frame_index = 0;
    uint32_t frame_started_ms = 0;
};

void xingzhi_splash_anim_init(XingzhiSplashAnimState *state, uint32_t now_ms = 0);
bool xingzhi_splash_anim_tick(XingzhiSplashAnimState *state, uint32_t now_ms);
bool xingzhi_splash_anim_select(XingzhiSplashAnimState *state, size_t animation_index, uint32_t now_ms);
bool xingzhi_splash_anim_next(XingzhiSplashAnimState *state, uint32_t now_ms);
bool xingzhi_splash_anim_get_frame(const XingzhiSplashAnimState *state, XingzhiSplashFrame *frame);
XingzhiSplashSnapshot xingzhi_splash_anim_snapshot(const XingzhiSplashAnimState *state);
