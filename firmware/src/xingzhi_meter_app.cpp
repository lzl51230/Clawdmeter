#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "usage_input.h"
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

XingzhiST7789 display(
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

UsageLineReader line_reader;
UsageData usage = {};
MeterPayloadState payload_state = MeterPayloadState::NoData;
char payload_detail[32] = {};

void set_backlight(bool on) {
    const bool level = on != BACKLIGHT_OUTPUT_INVERT;
    digitalWrite(PIN_LCD_BACKLIGHT, level ? HIGH : LOW);
}

void draw_meter() {
    xingzhi_meter_ui_draw(&display, &usage, payload_state, payload_detail);
}

void set_detail(const char *message) {
    snprintf(payload_detail, sizeof(payload_detail), "%s", message ? message : "");
}

void handle_payload_line(const char *line) {
    UsageData parsed = {};
    UsageParseResult result = parse_usage_payload(line, &parsed);

    switch (result) {
    case UsageParseResult::Valid:
        usage = parsed;
        payload_state = MeterPayloadState::Valid;
        set_detail(parsed.status);
        Serial.printf("Usage update: session=%.1f weekly=%.1f status=%s\n", usage.session_pct, usage.weekly_pct, usage.status);
        break;
    case UsageParseResult::ErrorPayload:
        payload_state = MeterPayloadState::Invalid;
        set_detail(parsed.status);
        Serial.printf("Usage error payload: status=%s\n", parsed.status);
        break;
    case UsageParseResult::InvalidJson:
        payload_state = MeterPayloadState::Invalid;
        set_detail("malformed JSON");
        Serial.println("Usage payload invalid JSON");
        break;
    case UsageParseResult::Empty:
    default:
        return;
    }

    draw_meter();
}

void poll_serial() {
    while (Serial.available()) {
        UsageLineResult result = line_reader.push(static_cast<char>(Serial.read()));
        if (result == UsageLineResult::Ready) {
            handle_payload_line(line_reader.line());
            line_reader.clear();
        } else if (result == UsageLineResult::Overflow) {
            payload_state = MeterPayloadState::Invalid;
            set_detail("line too long");
            Serial.println("Usage payload line overflow");
            draw_meter();
        }
    }
}

void log_config() {
    Serial.println("Xingzhi serial meter");
    Serial.printf("Panel: ST7789 %dx%d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    Serial.printf(
        "SPI: SDA=%d SCL=%d DC=%d CS=%d RES=%d mode=%d freq=%ld\n",
        PIN_LCD_SDA,
        PIN_LCD_SCL,
        PIN_LCD_DC,
        PIN_LCD_CS,
        PIN_LCD_RES,
        DISPLAY_SPI_MODE,
        static_cast<long>(DISPLAY_SPI_FREQUENCY)
    );
    Serial.println("Payload: newline-delimited JSON on USB serial");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(800);

    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    set_backlight(true);

    Serial.println();
    log_config();

    if (!display.begin(DISPLAY_SPI_FREQUENCY)) {
        Serial.println("Display init failed.");
        return;
    }

    display.invertDisplay(DISPLAY_INVERT_COLOR);
    display.setTextWrap(false);
    usage.session_reset_mins = -1;
    usage.weekly_reset_mins = -1;
    usage.ok = false;
    usage.valid = false;
    draw_meter();
    Serial.println("Serial meter ready.");
}

void loop() {
    static uint32_t last_log_ms = 0;
    poll_serial();

    const uint32_t now = millis();
    if (now - last_log_ms > 10000) {
        last_log_ms = now;
        Serial.println("Serial meter alive.");
    }
    delay(5);
}
