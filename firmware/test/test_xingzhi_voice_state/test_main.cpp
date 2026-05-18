#include <unity.h>

#include "xingzhi_voice_state.h"

void test_short_press_is_ignored() {
    XingzhiVoiceState state = {};
    xingzhi_voice_init(&state);

    XingzhiVoiceResult result = xingzhi_voice_handle_event(&state, XingzhiActionEvent::Press, 100);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("voice_armed", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::Idle, state.phase);

    result = xingzhi_voice_handle_event(&state, XingzhiActionEvent::Release, 200);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("short_ignored", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::Idle, state.phase);
    TEST_ASSERT_FALSE(state.pressed);
}

void test_long_press_starts_recording_and_release_marks_pending_send() {
    XingzhiVoiceState state = {};
    xingzhi_voice_init(&state);

    xingzhi_voice_handle_event(&state, XingzhiActionEvent::Press, 100);
    XingzhiVoiceResult result = xingzhi_voice_handle_event(&state, XingzhiActionEvent::LongPress, 900);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("recording", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::Recording, state.phase);

    result = xingzhi_voice_handle_event(&state, XingzhiActionEvent::Release, 2300);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("pending_send", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::PendingSend, state.phase);
    TEST_ASSERT_EQUAL(1400U, state.duration_ms);
}

void test_repeated_long_press_does_not_restart_recording() {
    XingzhiVoiceState state = {};
    xingzhi_voice_init(&state);

    xingzhi_voice_handle_event(&state, XingzhiActionEvent::LongPress, 1000);
    XingzhiVoiceResult result = xingzhi_voice_handle_event(&state, XingzhiActionEvent::LongPress, 1500);

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_FALSE(result.changed);
    TEST_ASSERT_EQUAL_STRING("already_recording", result.message);
    TEST_ASSERT_EQUAL(1000U, state.started_ms);
}

void test_release_without_recording_is_error() {
    XingzhiVoiceState state = {};
    xingzhi_voice_init(&state);

    XingzhiVoiceResult result = xingzhi_voice_handle_event(&state, XingzhiActionEvent::Release, 1000);

    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::Error, state.phase);
    TEST_ASSERT_EQUAL_STRING("release_without_recording", state.error);
}

void test_tick_caps_recording_at_max_duration() {
    XingzhiVoiceState state = {};
    xingzhi_voice_init(&state);

    xingzhi_voice_handle_event(&state, XingzhiActionEvent::LongPress, 1000);
    XingzhiVoiceResult result = xingzhi_voice_tick(&state, 1000 + XINGZHI_VOICE_MAX_RECORDING_MS);

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("max_duration", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::PendingSend, state.phase);
    TEST_ASSERT_EQUAL(XINGZHI_VOICE_MAX_RECORDING_MS, state.duration_ms);
}

void test_release_after_max_duration_remains_pending_send() {
    XingzhiVoiceState state = {};
    xingzhi_voice_init(&state);

    xingzhi_voice_handle_event(&state, XingzhiActionEvent::LongPress, 1000);
    xingzhi_voice_tick(&state, 1000 + XINGZHI_VOICE_MAX_RECORDING_MS);
    XingzhiVoiceResult result = xingzhi_voice_handle_event(
        &state,
        XingzhiActionEvent::Release,
        1000 + XINGZHI_VOICE_MAX_RECORDING_MS + 500
    );

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_FALSE(result.changed);
    TEST_ASSERT_EQUAL_STRING("pending_send", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::PendingSend, state.phase);
    TEST_ASSERT_EQUAL_STRING("", state.error);
    TEST_ASSERT_EQUAL(XINGZHI_VOICE_MAX_RECORDING_MS, state.duration_ms);
}

void test_release_after_transport_error_preserves_error() {
    XingzhiVoiceState state = {};
    xingzhi_voice_init(&state);

    xingzhi_voice_handle_event(&state, XingzhiActionEvent::LongPress, 1000);
    xingzhi_voice_tick(&state, 1000 + XINGZHI_VOICE_MAX_RECORDING_MS);
    xingzhi_voice_set_error(&state, "host_unavailable");
    XingzhiVoiceResult result = xingzhi_voice_handle_event(
        &state,
        XingzhiActionEvent::Release,
        1000 + XINGZHI_VOICE_MAX_RECORDING_MS + 500
    );

    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_FALSE(result.changed);
    TEST_ASSERT_EQUAL_STRING("host_unavailable", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoicePhase::Error, state.phase);
    TEST_ASSERT_EQUAL_STRING("host_unavailable", state.error);
    TEST_ASSERT_EQUAL_STRING("host_unavailable", state.detail);
}

void test_phase_names_are_stable_for_debug_status() {
    TEST_ASSERT_EQUAL_STRING("idle", xingzhi_voice_phase_name(XingzhiVoicePhase::Idle));
    TEST_ASSERT_EQUAL_STRING("recording", xingzhi_voice_phase_name(XingzhiVoicePhase::Recording));
    TEST_ASSERT_EQUAL_STRING("pending_send", xingzhi_voice_phase_name(XingzhiVoicePhase::PendingSend));
    TEST_ASSERT_EQUAL_STRING("sending", xingzhi_voice_phase_name(XingzhiVoicePhase::Sending));
    TEST_ASSERT_EQUAL_STRING("waiting_ack", xingzhi_voice_phase_name(XingzhiVoicePhase::WaitingAck));
    TEST_ASSERT_EQUAL_STRING("done", xingzhi_voice_phase_name(XingzhiVoicePhase::Done));
    TEST_ASSERT_EQUAL_STRING("error", xingzhi_voice_phase_name(XingzhiVoicePhase::Error));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_short_press_is_ignored);
    RUN_TEST(test_long_press_starts_recording_and_release_marks_pending_send);
    RUN_TEST(test_repeated_long_press_does_not_restart_recording);
    RUN_TEST(test_release_without_recording_is_error);
    RUN_TEST(test_tick_caps_recording_at_max_duration);
    RUN_TEST(test_release_after_max_duration_remains_pending_send);
    RUN_TEST(test_release_after_transport_error_preserves_error);
    RUN_TEST(test_phase_names_are_stable_for_debug_status);
    return UNITY_END();
}
