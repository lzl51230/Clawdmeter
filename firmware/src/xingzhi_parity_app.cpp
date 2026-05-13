#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <string.h>

#include "usage_input.h"
#include "xingzhi_app_actions.h"
#include "xingzhi_debug_serial.h"
#include "xingzhi_display_cfg.h"
#include "xingzhi_meter_ui.h"

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
bool display_ready = false;

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
    state.ble_state = "disabled";
    state.ble_detail = "BLE stage pending";
    state.last_error = action_state.last_error;
    state.last_action = xingzhi_action_name(action_state.last_action);
    state.action_count = action_state.action_count;
    xingzhi_meter_ui_draw_screen(&canvas, &state);
    canvas.flush();
}

void send_status() {
    char line[320];
    XingzhiDebugStatus status = {};
    status.target = "xingzhi_parity";
    status.screen = xingzhi_screen_name(action_state.current_screen);
    status.width = DISPLAY_WIDTH;
    status.height = DISPLAY_HEIGHT;
    status.payload = payload_state_name();
    status.source = last_payload_source;
    status.ble = "disabled";
    status.uptime_ms = millis();
    status.framebuffer = framebuffer_state_name();
    status.detail = payload_detail;
    status.action = xingzhi_action_name(action_state.last_action);
    status.event = xingzhi_event_name(action_state.last_event);
    status.action_count = action_state.action_count;
    status.action_error = action_state.last_error;
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

void handle_payload_line(const char *line) {
    UsageData parsed = {};
    UsageParseResult result = parse_usage_payload(line, &parsed);

    switch (result) {
    case UsageParseResult::Valid:
        usage = parsed;
        payload_state = MeterPayloadState::Valid;
        last_payload_source = "serial";
        set_detail(parsed.status);
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

bool parse_action(const char *token, XingzhiAction *action) {
    if (!token || !action) {
        return false;
    }
    if (strcmp(token, "cycle") == 0 || strcmp(token, "screen") == 0 || strcmp(token, "1") == 0) {
        *action = XingzhiAction::CycleScreen;
        return true;
    }
    if (strcmp(token, "space") == 0 || strcmp(token, "2") == 0) {
        *action = XingzhiAction::HidSpace;
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

    XingzhiActionResult result = xingzhi_actions_dispatch(&action_state, action, event);
    draw_meter();
    send_action_result(action, event, result);
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

void log_config() {
    Serial.println("Xingzhi parity firmware");
    Serial.printf("Panel: ST7789 %dx%d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Serial.println("Payload: newline-delimited JSON on USB serial");
    Serial.println("Debug: XDBG STATUS, XDBG SCREENSHOT");
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
    xingzhi_actions_init(&action_state);

    Serial.println();
    log_config();

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

    const uint32_t now = millis();
    if (now - last_log_ms > 10000) {
        last_log_ms = now;
        Serial.println("Xingzhi parity alive.");
    }
    delay(5);
}
