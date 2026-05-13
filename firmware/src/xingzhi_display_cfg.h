#pragma once

#include <Arduino.h>

namespace xingzhi_display {

// Xingzhi board profile: xingzhi-cube-1.54tft-wifi.
constexpr int DISPLAY_WIDTH = 240;
constexpr int DISPLAY_HEIGHT = 240;

constexpr int PIN_LCD_SDA = 10;
constexpr int PIN_LCD_SCL = 9;
constexpr int PIN_LCD_DC = 8;
constexpr int PIN_LCD_CS = 14;
constexpr int PIN_LCD_RES = 18;
constexpr int PIN_LCD_BACKLIGHT = 13;

constexpr bool BACKLIGHT_OUTPUT_INVERT = false;
constexpr bool DISPLAY_INVERT_COLOR = true;
constexpr uint8_t DISPLAY_ROTATION = 0;
constexpr int32_t DISPLAY_SPI_FREQUENCY = 80000000;
constexpr int8_t DISPLAY_SPI_MODE = SPI_MODE3;

}  // namespace xingzhi_display
