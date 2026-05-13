#include "xingzhi_buttons.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace {

constexpr uint32_t DEBOUNCE_MS = 35;

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
    state->last_change_ms = now_ms;
}

XingzhiButtonEvent xingzhi_button_debounce_update(
    XingzhiButtonDebounce *state,
    XingzhiAction action,
    bool raw_pressed,
    uint32_t now_ms,
    uint32_t debounce_ms
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
        event.active = true;
        event.event = state->stable_pressed ? XingzhiActionEvent::Press : XingzhiActionEvent::Release;
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
            DEBOUNCE_MS
        );
        if (next.active) {
            *event = next;
            return true;
        }
    }
    return false;
}
