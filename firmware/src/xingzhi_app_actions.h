#pragma once

#include <stdint.h>

enum class XingzhiScreen {
    Usage,
    Status,
    Splash,
};

enum class XingzhiAction {
    None,
    CycleScreen,
    ExitSplash,
    HidSpace,
    HidShiftTab,
};

enum class XingzhiActionEvent {
    Click,
    Press,
    Release,
    LongPress,
};

struct XingzhiActionState {
    XingzhiScreen current_screen = XingzhiScreen::Usage;
    XingzhiScreen previous_screen = XingzhiScreen::Usage;
    XingzhiAction last_action = XingzhiAction::None;
    XingzhiActionEvent last_event = XingzhiActionEvent::Click;
    uint32_t action_count = 0;
    bool space_pressed = false;
    bool shift_tab_pressed = false;
    const char *last_error = "";
};

struct XingzhiActionResult {
    bool ok = false;
    const char *message = "";
    bool splash_next = false;
};

void xingzhi_actions_init(XingzhiActionState *state);
XingzhiActionResult xingzhi_actions_dispatch(
    XingzhiActionState *state,
    XingzhiAction action,
    XingzhiActionEvent event
);
const char *xingzhi_screen_name(XingzhiScreen screen);
const char *xingzhi_action_name(XingzhiAction action);
const char *xingzhi_event_name(XingzhiActionEvent event);
bool xingzhi_action_is_hid(XingzhiAction action);
