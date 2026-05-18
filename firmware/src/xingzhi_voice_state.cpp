#include "xingzhi_voice_state.h"

namespace {

void set_idle(XingzhiVoiceState *state, const char *detail) {
    state->phase = XingzhiVoicePhase::Idle;
    state->pressed = false;
    state->long_press_started = false;
    state->started_ms = 0;
    state->duration_ms = 0;
    state->detail = detail;
    state->error = "";
}

XingzhiVoiceResult ok(const char *message, bool changed = true) {
    return {true, changed, message};
}

XingzhiVoiceResult fail(XingzhiVoiceState *state, const char *message) {
    if (state) {
        state->phase = XingzhiVoicePhase::Error;
        state->pressed = false;
        state->long_press_started = false;
        state->detail = message;
        state->error = message;
    }
    return {false, true, message};
}

void start_recording(XingzhiVoiceState *state, uint32_t now_ms) {
    state->phase = XingzhiVoicePhase::Recording;
    state->pressed = true;
    state->long_press_started = true;
    state->started_ms = now_ms;
    state->duration_ms = 0;
    state->detail = "recording";
    state->error = "";
}

void finish_recording(XingzhiVoiceState *state, uint32_t now_ms, const char *detail) {
    uint32_t duration = now_ms - state->started_ms;
    if (duration > XINGZHI_VOICE_MAX_RECORDING_MS) {
        duration = XINGZHI_VOICE_MAX_RECORDING_MS;
    }
    state->phase = XingzhiVoicePhase::PendingSend;
    state->pressed = false;
    state->long_press_started = false;
    state->duration_ms = duration;
    state->detail = detail;
    state->error = "";
}

}  // namespace

void xingzhi_voice_init(XingzhiVoiceState *state) {
    if (!state) {
        return;
    }
    set_idle(state, "idle");
}

XingzhiVoiceResult xingzhi_voice_handle_event(
    XingzhiVoiceState *state,
    XingzhiActionEvent event,
    uint32_t now_ms
) {
    if (!state) {
        return {false, false, "missing_state"};
    }

    if (event == XingzhiActionEvent::Press) {
        state->pressed = true;
        state->detail = "armed";
        state->error = "";
        return ok("voice_armed", true);
    }

    if (event == XingzhiActionEvent::LongPress) {
        if (state->phase == XingzhiVoicePhase::Recording) {
            return ok("already_recording", false);
        }
        start_recording(state, now_ms);
        return ok("recording");
    }

    if (event == XingzhiActionEvent::Release) {
        if (state->phase == XingzhiVoicePhase::Recording) {
            finish_recording(state, now_ms, "pending_send");
            return ok("pending_send");
        }
        if (state->phase == XingzhiVoicePhase::PendingSend) {
            state->pressed = false;
            state->long_press_started = false;
            return ok("pending_send", false);
        }
        if (state->phase == XingzhiVoicePhase::Sending ||
            state->phase == XingzhiVoicePhase::WaitingAck ||
            state->phase == XingzhiVoicePhase::Done ||
            state->phase == XingzhiVoicePhase::Error) {
            state->pressed = false;
            state->long_press_started = false;
            return ok(state->detail && state->detail[0] ? state->detail : "release_ignored", false);
        }
        if (state->pressed && !state->long_press_started) {
            set_idle(state, "short_ignored");
            return ok("short_ignored");
        }
        return fail(state, "release_without_recording");
    }

    if (event == XingzhiActionEvent::Click) {
        set_idle(state, "short_ignored");
        return ok("short_ignored");
    }

    return fail(state, "unknown_voice_event");
}

XingzhiVoiceResult xingzhi_voice_tick(XingzhiVoiceState *state, uint32_t now_ms) {
    if (!state) {
        return {false, false, "missing_state"};
    }
    if (state->phase != XingzhiVoicePhase::Recording) {
        return ok("unchanged", false);
    }
    const uint32_t duration = now_ms - state->started_ms;
    if (duration < XINGZHI_VOICE_MAX_RECORDING_MS) {
        state->duration_ms = duration;
        return ok("recording", false);
    }

    finish_recording(state, now_ms, "max_duration");
    return ok("max_duration");
}

void xingzhi_voice_set_error(XingzhiVoiceState *state, const char *message) {
    if (!state) {
        return;
    }
    fail(state, message && message[0] ? message : "voice_error");
}

const char *xingzhi_voice_phase_name(XingzhiVoicePhase phase) {
    switch (phase) {
    case XingzhiVoicePhase::Recording:
        return "recording";
    case XingzhiVoicePhase::PendingSend:
        return "pending_send";
    case XingzhiVoicePhase::Sending:
        return "sending";
    case XingzhiVoicePhase::WaitingAck:
        return "waiting_ack";
    case XingzhiVoicePhase::Done:
        return "done";
    case XingzhiVoicePhase::Error:
        return "error";
    case XingzhiVoicePhase::Idle:
    default:
        return "idle";
    }
}
