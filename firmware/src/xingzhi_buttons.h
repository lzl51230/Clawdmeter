#pragma once

#include <stdint.h>

#include "xingzhi_app_actions.h"

struct XingzhiButtonDebounce {
    bool initialized = false;
    bool raw_pressed = false;
    bool stable_pressed = false;
    uint32_t last_change_ms = 0;
};

struct XingzhiButtonEvent {
    bool active = false;
    XingzhiAction action = XingzhiAction::None;
    XingzhiActionEvent event = XingzhiActionEvent::Click;
};

void xingzhi_button_debounce_init(XingzhiButtonDebounce *state, bool raw_pressed, uint32_t now_ms);
XingzhiButtonEvent xingzhi_button_debounce_update(
    XingzhiButtonDebounce *state,
    XingzhiAction action,
    bool raw_pressed,
    uint32_t now_ms,
    uint32_t debounce_ms
);

void xingzhi_buttons_begin();
bool xingzhi_buttons_poll(XingzhiButtonEvent *event, uint32_t now_ms);
