#include "xingzhi_meter_ui.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t COLOR_BG = 0x0000;
constexpr uint16_t COLOR_PANEL = 0x18E3;
constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_DIM = 0x9CF3;
constexpr uint16_t COLOR_BAR_BG = 0x39E7;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_AMBER = 0xFD20;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_BLUE = 0x3D9F;

uint16_t level_color(MeterLevel level) {
    switch (level) {
    case MeterLevel::High:
    case MeterLevel::Invalid:
        return COLOR_RED;
    case MeterLevel::Warning:
        return COLOR_AMBER;
    case MeterLevel::NoData:
        return COLOR_BLUE;
    case MeterLevel::Normal:
    default:
        return COLOR_GREEN;
    }
}

const char *level_label(MeterLevel level) {
    switch (level) {
    case MeterLevel::High:
        return "HIGH";
    case MeterLevel::Invalid:
        return "ERROR";
    case MeterLevel::Warning:
        return "WATCH";
    case MeterLevel::NoData:
        return "NO DATA";
    case MeterLevel::Normal:
    default:
        return "NORMAL";
    }
}

void draw_text(Arduino_GFX *display, int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size = 1) {
    display->setTextSize(size);
    display->setTextColor(color);
    display->setCursor(x, y);
    display->print(text);
}

void draw_bar(Arduino_GFX *display, int16_t x, int16_t y, int16_t w, int16_t h, float pct, bool valid, uint16_t color) {
    display->drawRect(x, y, w, h, COLOR_DIM);
    display->fillRect(x + 1, y + 1, w - 2, h - 2, COLOR_BAR_BG);
    if (valid) {
        int fill_w = pct_to_bar_width(pct, w - 2);
        if (fill_w > 0) {
            display->fillRect(x + 1, y + 1, fill_w, h - 2, color);
        }
    }
}

void draw_usage_row(
    Arduino_GFX *display,
    int16_t y,
    const char *label,
    float pct,
    int reset_mins,
    bool valid,
    uint16_t color
) {
    char pct_buf[8];
    char reset_buf[24];
    format_percent(pct, valid, pct_buf, sizeof(pct_buf));
    format_reset_time(reset_mins, reset_buf, sizeof(reset_buf));

    draw_text(display, 18, y, label, COLOR_DIM);
    draw_text(display, 18, y + 17, pct_buf, COLOR_TEXT, 3);
    draw_text(display, 123, y + 22, reset_buf, COLOR_DIM);
    draw_bar(display, 18, y + 50, 204, 16, pct, valid, color);
}

}  // namespace

void xingzhi_meter_ui_draw(
    Arduino_GFX *display,
    const UsageData *data,
    MeterPayloadState payload_state,
    const char *detail
) {
    const bool has_data = data && data->valid;
    const MeterLevel level = meter_level_for(data, payload_state);
    const uint16_t accent = level_color(level);

    UsageData empty = {};
    empty.session_reset_mins = -1;
    empty.weekly_reset_mins = -1;
    const UsageData *view = data ? data : &empty;

    char payload_buf[24];
    format_payload_state(payload_state, payload_buf, sizeof(payload_buf));

    display->fillScreen(COLOR_BG);
    display->drawRect(0, 0, 240, 240, accent);
    display->drawRect(2, 2, 236, 236, COLOR_PANEL);

    draw_text(display, 16, 12, "Claude", COLOR_TEXT, 2);
    display->fillRect(156, 12, 66, 19, accent);
    draw_text(display, 162, 17, level_label(level), COLOR_BG);

    draw_usage_row(display, 48, "SESSION", view->session_pct, view->session_reset_mins, has_data, accent);
    draw_usage_row(display, 122, "WEEKLY", view->weekly_pct, view->weekly_reset_mins, has_data, accent);

    display->drawFastHLine(16, 198, 208, COLOR_PANEL);
    draw_text(display, 18, 207, payload_buf, payload_state == MeterPayloadState::Invalid ? COLOR_RED : COLOR_DIM);

    if (detail && detail[0]) {
        char clipped[25];
        snprintf(clipped, sizeof(clipped), "%.24s", detail);
        draw_text(display, 18, 222, clipped, COLOR_DIM);
    } else if (payload_state == MeterPayloadState::NoData) {
        draw_text(display, 18, 222, "Send JSON line on COM7", COLOR_DIM);
    }
}
