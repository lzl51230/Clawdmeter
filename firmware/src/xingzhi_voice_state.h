#pragma once

#include <stdint.h>

#include "xingzhi_app_actions.h"

constexpr uint32_t XINGZHI_VOICE_MAX_RECORDING_MS = 10000;

enum class XingzhiVoicePhase {
    Idle,
    Recording,
    PendingSend,
    Sending,
    WaitingAck,
    Done,
    Error,
};

struct XingzhiVoiceState {
    XingzhiVoicePhase phase = XingzhiVoicePhase::Idle;
    bool pressed = false;
    bool long_press_started = false;
    uint32_t started_ms = 0;
    uint32_t duration_ms = 0;
    const char *detail = "idle";
    const char *error = "";
};

struct XingzhiVoiceResult {
    bool ok = false;
    bool changed = false;
    const char *message = "";
};

void xingzhi_voice_init(XingzhiVoiceState *state);
XingzhiVoiceResult xingzhi_voice_handle_event(
    XingzhiVoiceState *state,
    XingzhiActionEvent event,
    uint32_t now_ms
);
XingzhiVoiceResult xingzhi_voice_tick(XingzhiVoiceState *state, uint32_t now_ms);
void xingzhi_voice_set_error(XingzhiVoiceState *state, const char *message);
const char *xingzhi_voice_phase_name(XingzhiVoicePhase phase);
