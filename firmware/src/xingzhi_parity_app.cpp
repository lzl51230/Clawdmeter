#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <string.h>

#include "usage_rate.h"
#include "usage_input.h"
#include "xingzhi_app_actions.h"
#include "xingzhi_audio_cfg.h"
#include "xingzhi_ble.h"
#include "xingzhi_buttons.h"
#include "xingzhi_debug_serial.h"
#include "xingzhi_display_cfg.h"
#include "xingzhi_meter_ui.h"
#include "xingzhi_power.h"
#include "xingzhi_splash_anim.h"
#include "xingzhi_voice_audio.h"
#include "xingzhi_voice_state.h"
#include "xingzhi_voice_transport.h"

using namespace xingzhi_display;

namespace {

class XingzhiST7789 : public Arduino_ST7789 {
public:
    using Arduino_ST7789::Arduino_ST7789;

    bool begin(int32_t speed = GFX_NOT_DEFINED) override {
        _override_datamode = DISPLAY_SPI_MODE;
        return Arduino_ST7789::begin(speed);
    }
};

Arduino_ESP32SPI bus(
    PIN_LCD_DC,
    PIN_LCD_CS,
    PIN_LCD_SCL,
    PIN_LCD_SDA,
    GFX_NOT_DEFINED,
    FSPI
);

XingzhiST7789 panel(
    &bus,
    PIN_LCD_RES,
    DISPLAY_ROTATION,
    true,
    DISPLAY_WIDTH,
    DISPLAY_HEIGHT,
    0,
    0,
    0,
    0
);

Arduino_Canvas canvas(DISPLAY_WIDTH, DISPLAY_HEIGHT, &panel);
UsageLineReader line_reader;
UsageData usage = {};
MeterPayloadState payload_state = MeterPayloadState::NoData;
char payload_detail[40] = {};
const char *last_payload_source = "none";
XingzhiActionState action_state = {};
XingzhiSplashAnimState splash_anim = {};
XingzhiVoiceState voice_state = {};
char last_ble_detail[40] = {};
const char *last_power_state = "";
int last_power_level = -2;
uint8_t last_power_samples = 255;
bool last_power_charging = false;
bool display_ready = false;
int last_hid_battery_level = -1;

constexpr uint8_t HID_KEY_SPACE = 0x2C;
constexpr uint8_t HID_KEY_TAB = 0x2B;
constexpr uint8_t HID_MOD_LEFT_SHIFT = 0x02;

void set_backlight(bool on) {
    const bool level = on != BACKLIGHT_OUTPUT_INVERT;
    digitalWrite(PIN_LCD_BACKLIGHT, level ? HIGH : LOW);
}

const char *payload_state_name() {
    switch (payload_state) {
    case MeterPayloadState::Valid:
        return "valid";
    case MeterPayloadState::Invalid:
        return "invalid";
    case MeterPayloadState::NoData:
    default:
        return "none";
    }
}

const char *framebuffer_state_name() {
    if (!display_ready) {
        return "unavailable";
    }
    return canvas.getFramebuffer() ? "ready" : "missing";
}

void set_detail(const char *message) {
    snprintf(payload_detail, sizeof(payload_detail), "%s", message ? message : "");
}

void draw_meter() {
    if (!display_ready || !canvas.getFramebuffer()) {
        return;
    }
    XingzhiUiState state = {};
    state.screen = action_state.current_screen;
    state.data = &usage;
    state.payload_state = payload_state;
    state.detail = payload_detail;
    state.last_source = last_payload_source;
    state.ble_state = xingzhi_ble_state_name();
    state.ble_detail = last_ble_detail[0] ? last_ble_detail : xingzhi_ble_mac();
    state.last_error = action_state.last_error[0] ? action_state.last_error : xingzhi_ble_last_error();
    state.last_action = xingzhi_action_name(action_state.last_action);
    state.action_count = action_state.action_count;
    state.voice = xingzhi_voice_phase_name(voice_state.phase);
    state.voice_detail = voice_state.detail;
    state.voice_duration_ms = voice_state.duration_ms;
    XingzhiPowerStatus power = xingzhi_power_status();
    state.power_valid = power.valid;
    state.battery_level = power.level;
    state.charging = power.charging;
    state.power_detail = power.state;
    XingzhiSplashFrame splash_frame = {};
    if (xingzhi_splash_anim_get_frame(&splash_anim, &splash_frame)) {
        state.splash_frame = &splash_frame;
    }
    xingzhi_meter_ui_draw_screen(&canvas, &state);
    canvas.flush();
}

void send_status() {
    char line[1400];
    char battery[8];
    char charging[4];
    char adc[20];
    char samples[8];
    char hid_battery[8];
    char splash_frame[20];
    char voice_ms[12];
    char audio_ms[12];
    char audio_samples[12];
    char audio_peak[12];
    char audio_rms[12];
    char audio_bytes[12];
    char voice_tx_bytes[24];
    char voice_tx_chunks[20];
    XingzhiPowerStatus power = xingzhi_power_status();
    XingzhiVoiceAudioStatus audio = xingzhi_voice_audio_status();
    XingzhiVoiceTransportStatus voice_tx = xingzhi_voice_transport_status();
    if (power.valid) {
        snprintf(battery, sizeof(battery), "%d", power.level);
        snprintf(charging, sizeof(charging), "%d", power.charging ? 1 : 0);
    } else {
        snprintf(battery, sizeof(battery), "-");
        snprintf(charging, sizeof(charging), "-");
    }
    if (power.average_adc >= 0 || power.raw_adc >= 0) {
        snprintf(adc, sizeof(adc), "%d/%d", power.average_adc, power.raw_adc);
    } else {
        snprintf(adc, sizeof(adc), "-");
    }
    snprintf(samples, sizeof(samples), "%u", static_cast<unsigned int>(power.sample_count));
    snprintf(hid_battery, sizeof(hid_battery), "%d", xingzhi_ble_battery_level());
    snprintf(voice_ms, sizeof(voice_ms), "%lu", static_cast<unsigned long>(voice_state.duration_ms));
    snprintf(audio_ms, sizeof(audio_ms), "%lu", static_cast<unsigned long>(audio.duration_ms));
    snprintf(audio_samples, sizeof(audio_samples), "%lu", static_cast<unsigned long>(audio.samples));
    snprintf(audio_peak, sizeof(audio_peak), "%d", audio.peak);
    snprintf(audio_rms, sizeof(audio_rms), "%d", audio.rms);
    snprintf(audio_bytes, sizeof(audio_bytes), "%lu", static_cast<unsigned long>(audio.wav_bytes));
    snprintf(
        voice_tx_bytes,
        sizeof(voice_tx_bytes),
        "%lu/%lu",
        static_cast<unsigned long>(voice_tx.sent_bytes),
        static_cast<unsigned long>(voice_tx.total_bytes)
    );
    snprintf(
        voice_tx_chunks,
        sizeof(voice_tx_chunks),
        "%u/%u",
        static_cast<unsigned int>(voice_tx.sent_chunks),
        static_cast<unsigned int>(voice_tx.total_chunks)
    );
    XingzhiSplashSnapshot splash = xingzhi_splash_anim_snapshot(&splash_anim);
    if (splash.valid) {
        snprintf(
            splash_frame,
            sizeof(splash_frame),
            "%u/%u",
            static_cast<unsigned int>(splash.frame_index),
            static_cast<unsigned int>(splash.frame_count)
        );
    } else {
        snprintf(splash_frame, sizeof(splash_frame), "-");
    }

    XingzhiDebugStatus status = {};
    status.target = "xingzhi_parity";
    status.screen = xingzhi_screen_name(action_state.current_screen);
    status.width = DISPLAY_WIDTH;
    status.height = DISPLAY_HEIGHT;
    status.payload = payload_state_name();
    status.source = last_payload_source;
    status.ble = xingzhi_ble_state_name();
    status.ble_name = xingzhi_ble_device_name();
    status.ble_mac = xingzhi_ble_mac();
    status.hid = xingzhi_ble_hid_available() ? "available" : "unavailable";
    status.hid_battery = hid_battery;
    status.power = power.state;
    status.battery = battery;
    status.charging = charging;
    status.adc = adc;
    status.samples = samples;
    status.uptime_ms = millis();
    status.framebuffer = framebuffer_state_name();
    status.detail = payload_detail;
    status.action = xingzhi_action_name(action_state.last_action);
    status.event = xingzhi_event_name(action_state.last_event);
    status.action_count = action_state.action_count;
    status.action_error = action_state.last_error[0] ? action_state.last_error : xingzhi_ble_last_error();
    status.voice = xingzhi_voice_phase_name(voice_state.phase);
    status.voice_detail = voice_state.detail;
    status.voice_ms = voice_ms;
    status.voice_error = voice_state.error;
    status.audio = xingzhi_voice_audio_phase_name(audio.phase);
    status.audio_detail = audio.detail;
    status.audio_ms = audio_ms;
    status.audio_samples = audio_samples;
    status.audio_peak = audio_peak;
    status.audio_rms = audio_rms;
    status.audio_bytes = audio_bytes;
    status.audio_error = audio.error;
    status.voice_tx = xingzhi_voice_transport_phase_name(voice_tx.phase);
    status.voice_tx_detail = voice_tx.detail;
    status.voice_tx_bytes = voice_tx_bytes;
    status.voice_tx_chunks = voice_tx_chunks;
    status.voice_tx_error = voice_tx.error;
    status.splash = splash.valid ? splash.name : "-";
    status.splash_group = splash.valid ? splash.group_name : "-";
    status.splash_category = splash.valid ? splash.category : "-";
    status.splash_frame = splash_frame;
    xingzhi_debug_format_status(&status, line, sizeof(line));
    Serial.println(line);
}

void send_debug_error(const char *code, const char *message) {
    char line[128];
    xingzhi_debug_format_error(code, message, line, sizeof(line));
    Serial.println(line);
}

void send_action_result(XingzhiAction action, XingzhiActionEvent event, const XingzhiActionResult &result) {
    Serial.printf(
        "XDBG ACTION ok=%d action=%s event=%s screen=%s count=%lu message=%s\n",
        result.ok ? 1 : 0,
        xingzhi_action_name(action),
        xingzhi_event_name(event),
        xingzhi_screen_name(action_state.current_screen),
        static_cast<unsigned long>(action_state.action_count),
        result.message && result.message[0] ? result.message : "-"
    );
}

void send_ble_result(const char *subcommand, bool ok, const char *message) {
    Serial.printf(
        "XDBG BLE ok=%d command=%s ble=%s screen=%s message=%s\n",
        ok ? 1 : 0,
        subcommand && subcommand[0] ? subcommand : "-",
        xingzhi_ble_state_name(),
        xingzhi_screen_name(action_state.current_screen),
        message && message[0] ? message : "-"
    );
}

void send_imu_probe_result() {
    char line[192];
    XingzhiDebugProbeResult result = {};
    result.target = "xingzhi_parity";
    result.probe = "imu";
    result.ok = true;
    result.status = "not_available";
    result.method = "xiaozhi_board_config";
    result.detail = "no_i2c_or_imu_config";
    result.checked = "qmi8658_0x6b";
    xingzhi_debug_format_probe(&result, line, sizeof(line));
    Serial.println(line);
}

void send_audio_probe_result() {
    char line[224];
    char detail[56];
    char checked[40];
    XingzhiVoiceAudioStatus audio = xingzhi_voice_audio_status();
    snprintf(
        detail,
        sizeof(detail),
        "ws%d_sck%d_din%d_cap%lu",
        xingzhi_audio::kMicWsPin,
        xingzhi_audio::kMicSckPin,
        xingzhi_audio::kMicDinPin,
        static_cast<unsigned long>(audio.capacity_bytes)
    );
    snprintf(
        checked,
        sizeof(checked),
        "ms%lu_s%lu_p%d_r%d_t%lu",
        static_cast<unsigned long>(audio.duration_ms),
        static_cast<unsigned long>(audio.samples),
        audio.peak,
        audio.rms,
        static_cast<unsigned long>(audio.timeouts)
    );

    XingzhiDebugProbeResult result = {};
    result.target = "xingzhi_parity";
    result.probe = "audio";
    result.ok = audio.buffer_ready && audio.phase != XingzhiVoiceAudioPhase::Error;
    result.status = xingzhi_voice_audio_phase_name(audio.phase);
    result.method = "i2s_std_16k_mono";
    result.detail = detail;
    result.checked = checked;
    xingzhi_debug_format_probe(&result, line, sizeof(line));
    Serial.println(line);
}

void send_probe_result(const char *target) {
    if (target && (strcmp(target, "audio") == 0 || strcmp(target, "mic") == 0)) {
        send_audio_probe_result();
        return;
    }
    send_imu_probe_result();
}

bool send_hid_press(XingzhiAction action) {
    if (action == XingzhiAction::HidSpace) {
        return xingzhi_ble_keyboard_press(HID_KEY_SPACE, 0);
    }
    if (action == XingzhiAction::HidShiftTab) {
        return xingzhi_ble_keyboard_press(HID_KEY_TAB, HID_MOD_LEFT_SHIFT);
    }
    return true;
}

bool send_hid_release(XingzhiAction action) {
    if (xingzhi_action_is_hid(action)) {
        return xingzhi_ble_keyboard_release();
    }
    return true;
}

XingzhiActionResult apply_hid_side_effect(
    XingzhiAction action,
    XingzhiActionEvent event,
    const XingzhiActionResult &input
) {
    XingzhiActionResult result = input;
    if (!result.ok || !xingzhi_action_is_hid(action)) {
        return result;
    }

    bool sent = false;
    if (event == XingzhiActionEvent::Click) {
        sent = send_hid_press(action) && send_hid_release(action);
    } else if (event == XingzhiActionEvent::Press) {
        sent = send_hid_press(action);
    } else {
        sent = send_hid_release(action);
    }

    if (sent) {
        result.message = "hid_sent";
        return result;
    }

    action_state.last_error = "hid_unavailable";
    result.ok = false;
    result.message = "hid_unavailable";
    return result;
}

void mark_voice_audio_error(const char *message) {
    xingzhi_voice_audio_abort(message);
    xingzhi_voice_set_error(&voice_state, message);
    action_state.last_error = message && message[0] ? message : "audio_error";
}

void mark_voice_audio_ready(const char *message) {
    XingzhiVoiceAudioStatus audio = xingzhi_voice_audio_status();
    voice_state.detail = message && message[0] ? message : "wav_ready";
    voice_state.duration_ms = audio.duration_ms;
    voice_state.error = "";
}

void mark_voice_transport_error(const char *message) {
    const char *error_message = message && message[0] ? message : "transport_error";
    xingzhi_voice_transport_set_error(error_message);
    xingzhi_voice_set_error(&voice_state, error_message);
    action_state.last_error = error_message;
}

void start_voice_transport_if_ready() {
    const uint8_t *wav_data = xingzhi_voice_audio_wav_data();
    const size_t wav_len = xingzhi_voice_audio_wav_bytes();
    if (!wav_data || wav_len == 0) {
        mark_voice_transport_error("audio_not_ready");
        return;
    }
    if (!xingzhi_ble_voice_subscribed()) {
        mark_voice_transport_error("host_unavailable");
        return;
    }

    XingzhiVoiceTransportResult result = xingzhi_voice_transport_begin(wav_data, wav_len, millis());
    if (!result.ok) {
        mark_voice_transport_error(result.message);
        return;
    }
    voice_state.phase = XingzhiVoicePhase::Sending;
    voice_state.detail = "sending";
    voice_state.error = "";
}

XingzhiActionResult execute_action(XingzhiAction action, XingzhiActionEvent event) {
    XingzhiActionResult result = xingzhi_actions_dispatch(&action_state, action, event);
    if (result.ok && result.splash_next) {
        xingzhi_splash_anim_next(&splash_anim, millis());
    }
    if (result.ok && xingzhi_action_is_voice(action)) {
        XingzhiVoiceResult voice_result = xingzhi_voice_handle_event(&voice_state, event, millis());
        result.ok = voice_result.ok;
        result.message = voice_result.message;
        if (!voice_result.ok) {
            action_state.last_error = voice_result.message;
            xingzhi_voice_audio_abort(voice_result.message);
            return result;
        }
        if (event == XingzhiActionEvent::LongPress && strcmp(voice_result.message, "recording") == 0) {
            XingzhiVoiceAudioResult audio_result = xingzhi_voice_audio_start();
            if (!audio_result.ok) {
                mark_voice_audio_error(audio_result.message);
                result.ok = false;
                result.message = audio_result.message;
            }
        } else if (event == XingzhiActionEvent::Release && voice_state.phase == XingzhiVoicePhase::PendingSend) {
            XingzhiVoiceAudioResult audio_result = xingzhi_voice_audio_stop();
            if (audio_result.ok) {
                mark_voice_audio_ready(audio_result.message);
                result.message = audio_result.message;
                start_voice_transport_if_ready();
            } else {
                mark_voice_audio_error(audio_result.message);
                result.ok = false;
                result.message = audio_result.message;
            }
        }
        return result;
    }
    return apply_hid_side_effect(action, event, result);
}

void send_screenshot() {
    uint16_t *framebuffer = display_ready ? canvas.getFramebuffer() : nullptr;
    if (!framebuffer) {
        send_debug_error("framebuffer_unavailable", "framebuffer not ready");
        return;
    }

    const size_t byte_count = static_cast<size_t>(DISPLAY_WIDTH) * DISPLAY_HEIGHT * sizeof(uint16_t);
    char line[128];
    xingzhi_debug_format_screenshot_start(
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
        "RGB565LE",
        byte_count,
        line,
        sizeof(line)
    );
    Serial.println(line);
    Serial.write(reinterpret_cast<const uint8_t *>(framebuffer), byte_count);
    Serial.print("\nXDBG SCREENSHOT_END\n");
}

void sample_usage_rate(float session_pct) {
    const int before = usage_rate_group();
    usage_rate_sample(session_pct);
    const int after = usage_rate_group();
    if (after != before) {
        Serial.printf(
            "Usage rate group: %s -> %s (session=%.2f%%)\n",
            usage_rate_group_name(before),
            usage_rate_group_name(after),
            session_pct
        );
    }
    xingzhi_splash_anim_set_group(&splash_anim, after, millis());
}

void handle_payload_line(const char *line) {
    UsageData parsed = {};
    UsageParseResult result = parse_usage_payload(line, &parsed);

    switch (result) {
    case UsageParseResult::Valid:
        usage = parsed;
        payload_state = MeterPayloadState::Valid;
        last_payload_source = "serial";
        set_detail(parsed.status);
        sample_usage_rate(parsed.session_pct);
        Serial.printf(
            "Usage update: session=%.1f weekly=%.1f status=%s\n",
            usage.session_pct,
            usage.weekly_pct,
            usage.status
        );
        break;
    case UsageParseResult::ErrorPayload:
        payload_state = MeterPayloadState::Invalid;
        last_payload_source = "serial";
        set_detail(parsed.status);
        Serial.printf("Usage error payload: status=%s\n", parsed.status);
        break;
    case UsageParseResult::InvalidJson:
        payload_state = MeterPayloadState::Invalid;
        last_payload_source = "serial";
        set_detail("malformed JSON");
        Serial.println("Usage payload invalid JSON");
        break;
    case UsageParseResult::Empty:
    default:
        return;
    }

    draw_meter();
}

void handle_ble_payload_line(const char *line) {
    UsageData parsed = {};
    UsageParseResult result = parse_usage_payload(line, &parsed);

    switch (result) {
    case UsageParseResult::Valid:
        usage = parsed;
        payload_state = MeterPayloadState::Valid;
        last_payload_source = "ble";
        set_detail(parsed.status);
        sample_usage_rate(parsed.session_pct);
        snprintf(last_ble_detail, sizeof(last_ble_detail), "rx ok");
        xingzhi_ble_send_ack();
        Serial.printf(
            "BLE usage update: session=%.1f weekly=%.1f status=%s\n",
            usage.session_pct,
            usage.weekly_pct,
            usage.status
        );
        break;
    case UsageParseResult::ErrorPayload:
        payload_state = MeterPayloadState::Invalid;
        last_payload_source = "ble";
        set_detail(parsed.status);
        snprintf(last_ble_detail, sizeof(last_ble_detail), "rx error");
        xingzhi_ble_send_nack();
        Serial.printf("BLE usage error payload: status=%s\n", parsed.status);
        break;
    case UsageParseResult::InvalidJson:
        payload_state = MeterPayloadState::Invalid;
        last_payload_source = "ble";
        set_detail("malformed BLE JSON");
        snprintf(last_ble_detail, sizeof(last_ble_detail), "rx invalid");
        xingzhi_ble_send_nack();
        Serial.println("BLE usage payload invalid JSON");
        break;
    case UsageParseResult::Empty:
    default:
        return;
    }

    draw_meter();
}

void poll_ble() {
    xingzhi_ble_tick();
    if (xingzhi_ble_has_error()) {
        const char *error = xingzhi_ble_take_error();
        snprintf(last_ble_detail, sizeof(last_ble_detail), "%s", error ? error : "ble error");
        xingzhi_ble_send_nack();
        draw_meter();
    }
    if (xingzhi_ble_has_payload()) {
        handle_ble_payload_line(xingzhi_ble_take_payload());
    }
}

bool parse_action(const char *token, XingzhiAction *action) {
    if (!token || !action) {
        return false;
    }
    if (strcmp(token, "cycle") == 0 || strcmp(token, "screen") == 0 || strcmp(token, "1") == 0) {
        *action = XingzhiAction::CycleScreen;
        return true;
    }
    if (strcmp(token, "exit") == 0 || strcmp(token, "back") == 0 || strcmp(token, "splash_exit") == 0) {
        *action = XingzhiAction::ExitSplash;
        return true;
    }
    if (
        strcmp(token, "voice") == 0 ||
        strcmp(token, "dictate") == 0 ||
        strcmp(token, "dictation") == 0 ||
        strcmp(token, "2") == 0
    ) {
        *action = XingzhiAction::VoiceInput;
        return true;
    }
    if (
        strcmp(token, "shift_tab") == 0 ||
        strcmp(token, "shift-tab") == 0 ||
        strcmp(token, "tab") == 0 ||
        strcmp(token, "3") == 0
    ) {
        *action = XingzhiAction::HidShiftTab;
        return true;
    }
    return false;
}

bool parse_event(const char *token, XingzhiActionEvent *event) {
    if (!event) {
        return false;
    }
    if (!token || token[0] == '\0' || strcmp(token, "click") == 0) {
        *event = XingzhiActionEvent::Click;
        return true;
    }
    if (strcmp(token, "press") == 0) {
        *event = XingzhiActionEvent::Press;
        return true;
    }
    if (strcmp(token, "release") == 0) {
        *event = XingzhiActionEvent::Release;
        return true;
    }
    if (strcmp(token, "long") == 0 || strcmp(token, "hold") == 0 || strcmp(token, "long_press") == 0) {
        *event = XingzhiActionEvent::LongPress;
        return true;
    }
    return false;
}

void handle_button_command(const XingzhiDebugCommand &command) {
    XingzhiAction action = XingzhiAction::None;
    XingzhiActionEvent event = XingzhiActionEvent::Click;
    if (!parse_action(command.arg1, &action)) {
        send_debug_error("unknown_button", command.arg1);
        return;
    }
    if (!parse_event(command.arg2, &event)) {
        send_debug_error("unknown_event", command.arg2);
        return;
    }

    XingzhiActionResult result = execute_action(action, event);
    draw_meter();
    send_action_result(action, event, result);
}

void handle_ble_command(const XingzhiDebugCommand &command) {
    if (
        strcmp(command.arg1, "reset") != 0 &&
        strcmp(command.arg1, "recover") != 0 &&
        strcmp(command.arg1, "clear") != 0
    ) {
        send_debug_error("unknown_ble_command", command.arg1);
        return;
    }

    action_state.current_screen = XingzhiScreen::Status;
    const bool ok = xingzhi_ble_reset_pairing();
    snprintf(last_ble_detail, sizeof(last_ble_detail), "%s", ok ? "pairing_reset" : xingzhi_ble_last_error());
    draw_meter();
    send_ble_result(command.arg1, ok, ok ? "pairing_reset" : xingzhi_ble_last_error());
}

void handle_probe_command(const XingzhiDebugCommand &command) {
    if (
        command.arg1[0] != '\0' &&
        strcmp(command.arg1, "imu") != 0 &&
        strcmp(command.arg1, "audio") != 0 &&
        strcmp(command.arg1, "mic") != 0
    ) {
        send_debug_error("unknown_probe", command.arg1);
        return;
    }
    send_probe_result(command.arg1);
}

void handle_physical_button_event(const XingzhiButtonEvent &button_event) {
    if (!button_event.active) {
        return;
    }

    XingzhiActionEvent event = button_event.event;
    if (button_event.action == XingzhiAction::CycleScreen) {
        if (button_event.event == XingzhiActionEvent::Press) {
            return;
        }
        event = button_event.event == XingzhiActionEvent::Release ? XingzhiActionEvent::Click : button_event.event;
    }

    XingzhiActionResult result = execute_action(button_event.action, event);
    draw_meter();
    Serial.printf(
        "Button action: ok=%d action=%s event=%s screen=%s count=%lu message=%s\n",
        result.ok ? 1 : 0,
        xingzhi_action_name(button_event.action),
        xingzhi_event_name(event),
        xingzhi_screen_name(action_state.current_screen),
        static_cast<unsigned long>(action_state.action_count),
        result.message && result.message[0] ? result.message : "-"
    );
}

void handle_debug_line(const char *line) {
    XingzhiDebugCommand command = xingzhi_debug_parse_command(line);
    switch (command.type) {
    case XingzhiDebugCommandType::Status:
        send_status();
        break;
    case XingzhiDebugCommandType::Screenshot:
        send_screenshot();
        break;
    case XingzhiDebugCommandType::Button:
        handle_button_command(command);
        break;
    case XingzhiDebugCommandType::Ble:
        handle_ble_command(command);
        break;
    case XingzhiDebugCommandType::Probe:
        handle_probe_command(command);
        break;
    case XingzhiDebugCommandType::None:
        break;
    case XingzhiDebugCommandType::Unknown:
    default:
        send_debug_error("unknown_command", command.token);
        break;
    }
}

void handle_serial_line(const char *line) {
    if (xingzhi_debug_is_usage_payload(line)) {
        handle_payload_line(line);
    } else {
        handle_debug_line(line);
    }
}

void poll_serial() {
    while (Serial.available()) {
        UsageLineResult result = line_reader.push(static_cast<char>(Serial.read()));
        if (result == UsageLineResult::Ready) {
            handle_serial_line(line_reader.line());
            line_reader.clear();
        } else if (result == UsageLineResult::Overflow) {
            payload_state = MeterPayloadState::Invalid;
            last_payload_source = "serial";
            set_detail("line too long");
            Serial.println("Usage payload line overflow");
            draw_meter();
        }
    }
}

void poll_buttons() {
    XingzhiButtonEvent event = {};
    if (xingzhi_buttons_poll(&event, millis())) {
        handle_physical_button_event(event);
    }
}

void poll_power(uint32_t now_ms) {
    xingzhi_power_tick(now_ms);
    XingzhiPowerStatus power = xingzhi_power_status();
    if (power.hid_level >= 0 && power.hid_level != last_hid_battery_level) {
        if (xingzhi_ble_set_battery_level(power.hid_level)) {
            last_hid_battery_level = power.hid_level;
        }
    }
    if (
        strcmp(power.state, last_power_state) != 0 ||
        power.level != last_power_level ||
        power.sample_count != last_power_samples ||
        power.charging != last_power_charging
    ) {
        last_power_state = power.state;
        last_power_level = power.level;
        last_power_samples = power.sample_count;
        last_power_charging = power.charging;
        draw_meter();
    }
}

void poll_splash(uint32_t now_ms) {
    if (action_state.current_screen != XingzhiScreen::Splash) {
        return;
    }
    const bool rotated = xingzhi_splash_anim_rotate_if_due(&splash_anim, now_ms);
    const bool advanced = xingzhi_splash_anim_tick(&splash_anim, now_ms);
    if (rotated || advanced) {
        draw_meter();
    }
}

void poll_voice(uint32_t now_ms) {
    static uint32_t last_audio_log_ms = 0;
    XingzhiVoiceAudioResult audio_result = {};
    if (voice_state.phase == XingzhiVoicePhase::Recording) {
        audio_result = xingzhi_voice_audio_tick();
        if (!audio_result.ok) {
            mark_voice_audio_error(audio_result.message);
            draw_meter();
            Serial.printf("Voice audio error: %s\n", audio_result.message ? audio_result.message : "audio_error");
            return;
        }
    }

    XingzhiVoiceResult result = xingzhi_voice_tick(&voice_state, now_ms);
    if (result.changed && voice_state.phase == XingzhiVoicePhase::PendingSend) {
        XingzhiVoiceAudioResult stop_result = xingzhi_voice_audio_stop();
        if (stop_result.ok) {
            mark_voice_audio_ready(stop_result.message);
            start_voice_transport_if_ready();
        } else {
            mark_voice_audio_error(stop_result.message);
        }
    }
    if (result.changed) {
        draw_meter();
        Serial.printf(
            "Voice state: phase=%s detail=%s duration_ms=%lu\n",
            xingzhi_voice_phase_name(voice_state.phase),
            voice_state.detail && voice_state.detail[0] ? voice_state.detail : "-",
            static_cast<unsigned long>(voice_state.duration_ms)
        );
    } else if (audio_result.changed && now_ms - last_audio_log_ms >= 1000) {
        last_audio_log_ms = now_ms;
        XingzhiVoiceAudioStatus audio = xingzhi_voice_audio_status();
        Serial.printf(
            "Voice audio: state=%s ms=%lu samples=%lu peak=%d rms=%d\n",
            xingzhi_voice_audio_phase_name(audio.phase),
            static_cast<unsigned long>(audio.duration_ms),
            static_cast<unsigned long>(audio.samples),
            audio.peak,
            audio.rms
        );
    }
}

void poll_voice_transport(uint32_t now_ms) {
    if (xingzhi_ble_has_voice_control()) {
        XingzhiVoiceTransportResult result = xingzhi_voice_transport_handle_control(
            xingzhi_ble_take_voice_control()
        );
        if (result.ok && strcmp(result.message, "done") == 0) {
            voice_state.phase = XingzhiVoicePhase::Done;
            voice_state.detail = "done";
            voice_state.error = "";
            draw_meter();
        } else if (!result.ok) {
            mark_voice_transport_error(result.message);
            draw_meter();
        }
    }

    XingzhiVoiceTransportStatus status = xingzhi_voice_transport_status();
    if (status.phase == XingzhiVoiceTransportPhase::Sending) {
        XingzhiVoiceFrame frame = {};
        XingzhiVoiceTransportResult next = xingzhi_voice_transport_next_frame(&frame);
        if (!next.ok) {
            mark_voice_transport_error(next.message);
            draw_meter();
            return;
        }
        if (frame.valid && !xingzhi_ble_voice_notify(frame.data, frame.len)) {
            mark_voice_transport_error(xingzhi_ble_last_error());
            draw_meter();
            return;
        }
        if (frame.complete) {
            voice_state.phase = XingzhiVoicePhase::WaitingAck;
            voice_state.detail = "waiting_ack";
            draw_meter();
        }
    }

    XingzhiVoiceTransportResult tick = xingzhi_voice_transport_tick(now_ms);
    if (!tick.ok) {
        mark_voice_transport_error(tick.message);
        draw_meter();
    }
}

void log_config() {
    Serial.println("Xingzhi parity firmware");
    Serial.printf("Panel: ST7789 %dx%d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Serial.println("Payload: newline-delimited JSON on USB serial");
    Serial.println("Debug: XDBG STATUS, XDBG SCREENSHOT, XDBG PROBE imu|audio");
    Serial.println("Buttons: GPIO0 cycle, GPIO40 Voice, GPIO39 Shift+Tab");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(800);

    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    set_backlight(true);

    usage.session_reset_mins = -1;
    usage.weekly_reset_mins = -1;
    usage.ok = false;
    usage.valid = false;
    usage_rate_reset();
    xingzhi_actions_init(&action_state);
    xingzhi_splash_anim_init(&splash_anim, millis());
    xingzhi_voice_init(&voice_state);
    xingzhi_voice_audio_init();
    xingzhi_voice_transport_init();
    xingzhi_buttons_begin();
    xingzhi_power_init();

    Serial.println();
    log_config();
    xingzhi_ble_init();

    display_ready = canvas.begin(DISPLAY_SPI_FREQUENCY);
    if (!display_ready) {
        Serial.println("Display or framebuffer init failed.");
    } else {
        panel.invertDisplay(DISPLAY_INVERT_COLOR);
        canvas.setTextWrap(false);
        draw_meter();
    }

    Serial.println("Xingzhi parity ready.");
}

void loop() {
    static uint32_t last_log_ms = 0;
    poll_serial();
    poll_ble();
    poll_buttons();

    const uint32_t now = millis();
    poll_power(now);
    poll_splash(now);
    poll_voice(now);
    poll_voice_transport(now);
    if (now - last_log_ms > 10000) {
        last_log_ms = now;
        Serial.println("Xingzhi parity alive.");
    }
    delay(5);
}
