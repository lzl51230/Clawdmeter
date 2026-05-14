#include "xingzhi_buttons.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace {

constexpr uint32_t DEBOUNCE_MS = 35;
constexpr uint32_t LONG_PRESS_MS = 800;

struct PhysicalButton {
    int pin;
    XingzhiAction action;
    XingzhiButtonDebounce debounce;
};

PhysicalButton buttons[] = {
    {0, XingzhiAction::CycleScreen, {}},
    {40, XingzhiAction::HidSpace, {}},
    {39, XingzhiAction::HidShiftTab, {}},
};

bool read_pin_pressed(int pin) {
#ifdef ARDUINO
    return digitalRead(pin) == LOW;
#else
    (void)pin;
    return false;
#endif
}

}  // namespace

void xingzhi_button_debounce_init(XingzhiButtonDebounce *state, bool raw_pressed, uint32_t now_ms) {
    if (!state) {
        return;
    }
    state->initialized = true;
    state->raw_pressed = raw_pressed;
    state->stable_pressed = raw_pressed;
    state->long_press_reported = false;
    state->last_change_ms = now_ms;
    state->stable_pressed_ms = raw_pressed ? now_ms : 0;
}

XingzhiButtonEvent xingzhi_button_debounce_update(
    XingzhiButtonDebounce *state,
    XingzhiAction action,
    bool raw_pressed,
    uint32_t now_ms,
    uint32_t debounce_ms,
    uint32_t long_press_ms
) {
    XingzhiButtonEvent event = {};
    event.action = action;

    if (!state) {
        return event;
    }
    if (!state->initialized) {
        xingzhi_button_debounce_init(state, raw_pressed, now_ms);
        return event;
    }

    if (raw_pressed != state->raw_pressed) {
        state->raw_pressed = raw_pressed;
        state->last_change_ms = now_ms;
        return event;
    }

    if (state->stable_pressed != state->raw_pressed && now_ms - state->last_change_ms >= debounce_ms) {
        state->stable_pressed = state->raw_pressed;
        if (state->stable_pressed) {
            state->long_press_reported = false;
            state->stable_pressed_ms = now_ms;
            event.active = true;
            event.event = XingzhiActionEvent::Press;
        } else {
            state->stable_pressed_ms = 0;
            if (!state->long_press_reported) {
                event.active = true;
                event.event = XingzhiActionEvent::Release;
            }
            state->long_press_reported = false;
        }
        return event;
    }

    if (
        long_press_ms > 0 &&
        state->stable_pressed &&
        !state->long_press_reported &&
        now_ms - state->stable_pressed_ms >= long_press_ms
    ) {
        state->long_press_reported = true;
        event.active = true;
        event.event = XingzhiActionEvent::LongPress;
    }
    return event;
}

void xingzhi_buttons_begin() {
#ifdef ARDUINO
    for (PhysicalButton &button : buttons) {
        pinMode(button.pin, INPUT_PULLUP);
        xingzhi_button_debounce_init(&button.debounce, read_pin_pressed(button.pin), millis());
    }
#endif
}

bool xingzhi_buttons_poll(XingzhiButtonEvent *event, uint32_t now_ms) {
    if (!event) {
        return false;
    }

    for (PhysicalButton &button : buttons) {
        XingzhiButtonEvent next = xingzhi_button_debounce_update(
            &button.debounce,
            button.action,
            read_pin_pressed(button.pin),
            now_ms,
            DEBOUNCE_MS,
            button.action == XingzhiAction::CycleScreen ? LONG_PRESS_MS : 0
        );
        if (next.active) {
            *event = next;
            return true;
        }
    }
    return false;
}
