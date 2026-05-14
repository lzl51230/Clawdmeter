#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "xingzhi_display_cfg.h"

using namespace xingzhi_display;

namespace {

constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_BLUE = 0x001F;
constexpr uint16_t COLOR_CYAN = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;
constexpr uint16_t COLOR_YELLOW = 0xFFE0;
constexpr uint16_t COLOR_DARK = 0x2104;
constexpr uint16_t COLOR_GRAY = 0x8410;

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

void set_backlight(bool on) {
    const bool level = on != BACKLIGHT_OUTPUT_INVERT;
    digitalWrite(PIN_LCD_BACKLIGHT, level ? HIGH : LOW);
}

void draw_label(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size = 1) {
    display.setTextSize(size);
    display.setTextColor(color);
    display.setCursor(x, y);
    display.print(text);
}

void draw_test_screen() {
    display.fillScreen(COLOR_BLACK);
    display.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COLOR_WHITE);
    display.drawRect(2, 2, DISPLAY_WIDTH - 4, DISPLAY_HEIGHT - 4, COLOR_GRAY);

    display.drawLine(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, COLOR_YELLOW);
    display.drawLine(DISPLAY_WIDTH - 1, 0, 0, DISPLAY_HEIGHT - 1, COLOR_CYAN);
    display.drawFastHLine(0, DISPLAY_HEIGHT / 2, DISPLAY_WIDTH, COLOR_DARK);
    display.drawFastVLine(DISPLAY_WIDTH / 2, 0, DISPLAY_HEIGHT, COLOR_DARK);

    draw_label(5, 6, "TL", COLOR_YELLOW);
    draw_label(220, 6, "TR", COLOR_CYAN);
    draw_label(5, 226, "BL", COLOR_MAGENTA);
    draw_label(220, 226, "BR", COLOR_GREEN);

    draw_label(30, 25, "XINGZHI ST7789", COLOR_WHITE, 2);
    draw_label(66, 47, "240 x 240", COLOR_GRAY);

    const int block_y = 64;
    const int block_w = 44;
    const int block_h = 30;
    display.fillRect(18, block_y, block_w, block_h, COLOR_RED);
    display.fillRect(70, block_y, block_w, block_h, COLOR_GREEN);
    display.fillRect(122, block_y, block_w, block_h, COLOR_BLUE);
    display.fillRect(174, block_y, block_w, block_h, COLOR_WHITE);

    display.fillRect(18, 107, block_w, block_h, COLOR_BLACK);
    display.drawRect(18, 107, block_w, block_h, COLOR_GRAY);
    display.fillRect(70, 107, block_w, block_h, COLOR_DARK);
    display.fillRect(122, 107, block_w, block_h, COLOR_GRAY);
    display.fillRect(174, 107, block_w, block_h, COLOR_WHITE);

    draw_label(45, 152, "SPI MODE 3", COLOR_WHITE, 2);
    draw_label(57, 176, "BL GPIO13", COLOR_GRAY);
    draw_label(48, 193, DISPLAY_INVERT_COLOR ? "Invert color: on" : "Invert color: off", COLOR_GRAY);
}

void log_config() {
    Serial.println("Xingzhi display bring-up");
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
    Serial.printf("Backlight: GPIO%d invert=%s\n", PIN_LCD_BACKLIGHT, BACKLIGHT_OUTPUT_INVERT ? "true" : "false");
    Serial.printf("Display invert color: %s\n", DISPLAY_INVERT_COLOR ? "true" : "false");
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
    draw_test_screen();
    Serial.println("Display test screen drawn.");
}

void loop() {
    static uint32_t last_log_ms = 0;
    const uint32_t now = millis();
    if (now - last_log_ms > 5000) {
        last_log_ms = now;
        Serial.println("Display bring-up alive.");
    }
    delay(10);
}
