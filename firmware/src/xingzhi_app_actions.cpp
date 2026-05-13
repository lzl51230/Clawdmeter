#include "xingzhi_app_actions.h"

namespace {

XingzhiScreen next_screen(XingzhiScreen screen) {
    switch (screen) {
    case XingzhiScreen::Usage:
        return XingzhiScreen::Status;
    case XingzhiScreen::Status:
        return XingzhiScreen::Splash;
    case XingzhiScreen::Splash:
    default:
        return XingzhiScreen::Usage;
    }
}

bool is_pressed(const XingzhiActionState *state, XingzhiAction action) {
    if (!state) {
        return false;
    }
    switch (action) {
    case XingzhiAction::HidSpace:
        return state->space_pressed;
    case XingzhiAction::HidShiftTab:
        return state->shift_tab_pressed;
    case XingzhiAction::CycleScreen:
    case XingzhiAction::None:
    default:
        return false;
    }
}

void set_pressed(XingzhiActionState *state, XingzhiAction action, bool pressed) {
    if (!state) {
        return;
    }
    switch (action) {
    case XingzhiAction::HidSpace:
        state->space_pressed = pressed;
        break;
    case XingzhiAction::HidShiftTab:
        state->shift_tab_pressed = pressed;
        break;
    case XingzhiAction::CycleScreen:
    case XingzhiAction::None:
    default:
        break;
    }
}

XingzhiActionResult record_success(
    XingzhiActionState *state,
    XingzhiAction action,
    XingzhiActionEvent event,
    const char *message
) {
    state->last_action = action;
    state->last_event = event;
    state->last_error = "";
    state->action_count += 1;
    return {true, message};
}

XingzhiActionResult record_error(XingzhiActionState *state, const char *message) {
    if (state) {
        state->last_error = message;
    }
    return {false, message};
}

}  // namespace

void xingzhi_actions_init(XingzhiActionState *state) {
    if (!state) {
        return;
    }
    *state = XingzhiActionState{};
}

XingzhiActionResult xingzhi_actions_dispatch(
    XingzhiActionState *state,
    XingzhiAction action,
    XingzhiActionEvent event
) {
    if (!state) {
        return {false, "missing_state"};
    }
    if (action == XingzhiAction::None) {
        return record_error(state, "unknown_action");
    }

    if (action == XingzhiAction::CycleScreen) {
        if (event == XingzhiActionEvent::Click) {
            state->current_screen = next_screen(state->current_screen);
            return record_success(state, action, event, "screen_changed");
        }
        return record_success(state, action, event, "cycle_event_recorded");
    }

    if (event == XingzhiActionEvent::Press) {
        set_pressed(state, action, true);
        return record_success(state, action, event, "pressed");
    }
    if (event == XingzhiActionEvent::Release) {
        if (!is_pressed(state, action)) {
            return record_error(state, "release_without_press");
        }
        set_pressed(state, action, false);
        return record_success(state, action, event, "released");
    }

    return record_success(state, action, event, "clicked");
}

const char *xingzhi_screen_name(XingzhiScreen screen) {
    switch (screen) {
    case XingzhiScreen::Status:
        return "status";
    case XingzhiScreen::Splash:
        return "splash";
    case XingzhiScreen::Usage:
    default:
        return "usage";
    }
}

const char *xingzhi_action_name(XingzhiAction action) {
    switch (action) {
    case XingzhiAction::CycleScreen:
        return "cycle";
    case XingzhiAction::HidSpace:
        return "space";
    case XingzhiAction::HidShiftTab:
        return "shift_tab";
    case XingzhiAction::None:
    default:
        return "none";
    }
}

const char *xingzhi_event_name(XingzhiActionEvent event) {
    switch (event) {
    case XingzhiActionEvent::Press:
        return "press";
    case XingzhiActionEvent::Release:
        return "release";
    case XingzhiActionEvent::Click:
    default:
        return "click";
    }
}

bool xingzhi_action_is_hid(XingzhiAction action) {
    return action == XingzhiAction::HidSpace || action == XingzhiAction::HidShiftTab;
}
