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

void draw_clipped_text(
    Arduino_GFX *display,
    int16_t x,
    int16_t y,
    const char *text,
    uint16_t color,
    size_t max_chars,
    uint8_t size = 1
) {
    char clipped[36];
    snprintf(clipped, sizeof(clipped), "%.*s", static_cast<int>(max_chars), text ? text : "-");
    draw_text(display, x, y, clipped, color, size);
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

void draw_frame(Arduino_GFX *display, uint16_t accent) {
    display->fillScreen(COLOR_BG);
    display->drawRect(0, 0, 240, 240, accent);
    display->drawRect(2, 2, 236, 236, COLOR_PANEL);
}

void draw_header(Arduino_GFX *display, const char *title, const char *label, uint16_t accent) {
    draw_text(display, 16, 12, title, COLOR_TEXT, 2);
    display->fillRect(116, 12, 54, 19, accent);
    draw_clipped_text(display, 122, 17, label, COLOR_BG, 8);
}

uint16_t battery_color(int level, bool charging) {
    if (charging) {
        return COLOR_AMBER;
    }
    if (level < 20) {
        return COLOR_RED;
    }
    if (level < 50) {
        return COLOR_AMBER;
    }
    return COLOR_GREEN;
}

void draw_charging_bolt(Arduino_GFX *display, int16_t x, int16_t y, uint16_t color) {
    display->drawLine(x + 8, y + 2, x + 5, y + 7, color);
    display->drawLine(x + 5, y + 7, x + 9, y + 7, color);
    display->drawLine(x + 9, y + 7, x + 6, y + 12, color);
    display->drawLine(x + 9, y + 2, x + 6, y + 7, color);
    display->drawLine(x + 6, y + 7, x + 10, y + 7, color);
    display->drawLine(x + 10, y + 7, x + 7, y + 12, color);
}

void draw_power_badge(Arduino_GFX *display, const XingzhiUiState *state) {
    const bool valid = state && state->power_valid;
    const bool charging = state && state->charging;
    const int level = valid ? state->battery_level : -1;
    const uint16_t color = valid || charging ? battery_color(level, charging) : COLOR_DIM;

    char label[8];
    if (valid) {
        snprintf(label, sizeof(label), "%d%%", level);
    } else {
        snprintf(label, sizeof(label), "--");
    }

    constexpr int16_t badge_x = 172;
    constexpr int16_t badge_y = 8;
    constexpr int16_t icon_x = 211;
    constexpr int16_t icon_y = 11;
    constexpr int16_t body_w = 18;
    constexpr int16_t body_h = 11;
    constexpr int16_t inner_w = body_w - 4;

    display->fillRect(badge_x, badge_y, 62, 20, COLOR_BG);

    const int16_t text_w = static_cast<int16_t>(strlen(label) * 6);
    draw_text(display, icon_x - text_w - 5, icon_y + 2, label, valid ? COLOR_TEXT : COLOR_DIM);

    display->drawRect(icon_x, icon_y, body_w, body_h, color);
    display->fillRect(icon_x + body_w, icon_y + 3, 3, 5, color);

    if (valid) {
        int fill_w = (level * inner_w + 99) / 100;
        if (fill_w < 0) {
            fill_w = 0;
        } else if (fill_w > inner_w) {
            fill_w = inner_w;
        }
        if (fill_w > 0) {
            display->fillRect(icon_x + 2, icon_y + 2, fill_w, body_h - 4, color);
        }
    }

    if (charging) {
        draw_charging_bolt(display, icon_x + 3, icon_y - 1, COLOR_AMBER);
    }
}

const char *usage_title_for(const UsageData *data) {
    return data && strcmp(data->provider, "codex") == 0 ? "Codex" : "Claude";
}

void draw_key_value(
    Arduino_GFX *display,
    int16_t y,
    const char *key,
    const char *value,
    uint16_t value_color = COLOR_TEXT
) {
    draw_text(display, 18, y, key, COLOR_DIM);
    draw_clipped_text(display, 92, y, value, value_color, 20);
}

void draw_usage_screen(
    Arduino_GFX *display,
    const UsageData *data,
    MeterPayloadState payload_state,
    const char *detail,
    const XingzhiUiState *state
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

    draw_frame(display, accent);
    draw_header(display, usage_title_for(view), level_label(level), accent);

    draw_usage_row(display, 48, "SESSION", view->session_pct, view->session_reset_mins, has_data, accent);
    draw_usage_row(display, 122, "WEEKLY", view->weekly_pct, view->weekly_reset_mins, has_data, accent);

    display->drawFastHLine(16, 198, 208, COLOR_PANEL);
    draw_text(display, 18, 207, payload_buf, payload_state == MeterPayloadState::Invalid ? COLOR_RED : COLOR_DIM);

    if (detail && detail[0]) {
        draw_clipped_text(display, 18, 222, detail, COLOR_DIM, 24);
    } else if (payload_state == MeterPayloadState::NoData) {
        draw_text(display, 18, 222, "Send JSON line on COM7", COLOR_DIM);
    }

    draw_power_badge(display, state);
}

void draw_status_screen(Arduino_GFX *display, const XingzhiUiState *state) {
    const bool has_error = state && state->last_error && state->last_error[0];
    const uint16_t accent = has_error ? COLOR_RED : COLOR_BLUE;
    draw_frame(display, accent);
    draw_header(display, "Claude", "STATUS", accent);

    draw_text(display, 18, 48, "Bluetooth", COLOR_TEXT, 2);
    draw_key_value(display, 76, "state", state ? state->ble_state : "unknown", COLOR_TEXT);
    draw_key_value(display, 96, "source", state ? state->last_source : "none", COLOR_TEXT);
    draw_key_value(display, 116, "payload", state ? state->detail : "-", COLOR_TEXT);
    draw_key_value(display, 136, "action", state ? state->last_action : "none", COLOR_TEXT);

    char count_buf[16];
    snprintf(count_buf, sizeof(count_buf), "%lu", static_cast<unsigned long>(state ? state->action_count : 0));
    draw_key_value(display, 156, "count", count_buf, COLOR_TEXT);

    char power_buf[24];
    if (state && state->power_valid) {
        snprintf(power_buf, sizeof(power_buf), "%d%% %s", state->battery_level, state->charging ? "charging" : "battery");
    } else {
        snprintf(power_buf, sizeof(power_buf), "%s", state && state->power_detail ? state->power_detail : "unknown");
    }
    draw_key_value(display, 176, "battery", power_buf, state && state->power_valid ? COLOR_TEXT : COLOR_DIM);

    display->drawFastHLine(16, 196, 208, COLOR_PANEL);
    if (has_error) {
        draw_clipped_text(display, 18, 207, state->last_error, COLOR_RED, 26);
        draw_text(display, 18, 224, "Use serial fallback", COLOR_DIM);
    } else {
        draw_clipped_text(display, 18, 207, state && state->ble_detail ? state->ble_detail : "BLE not enabled yet", COLOR_DIM, 26);
        draw_text(display, 18, 224, "Pair after BLE stage", COLOR_DIM);
    }

    draw_power_badge(display, state);
}

void draw_splash_placeholder(Arduino_GFX *display, const XingzhiUiState *state) {
    draw_frame(display, COLOR_AMBER);
    draw_text(display, 26, 38, "Clawdmeter", COLOR_TEXT, 3);
    draw_text(display, 39, 82, "Xingzhi", COLOR_AMBER, 2);
    display->drawCircle(120, 138, 34, COLOR_DIM);
    display->drawCircle(120, 138, 22, COLOR_BLUE);
    display->fillCircle(108, 132, 4, COLOR_TEXT);
    display->fillCircle(132, 132, 4, COLOR_TEXT);
    display->drawFastHLine(107, 151, 26, COLOR_TEXT);
    draw_text(display, 32, 194, "cycle: usage/status/splash", COLOR_DIM);
    draw_clipped_text(display, 32, 214, state ? state->last_action : "none", COLOR_DIM, 24);
}

void draw_splash_frame(Arduino_GFX *display, const XingzhiSplashFrame *frame) {
    if (!frame || !frame->cells || !frame->palette || frame->width == 0 || frame->height == 0) {
        draw_splash_placeholder(display, nullptr);
        return;
    }

    const int16_t cell_w = 240 / frame->width;
    const int16_t cell_h = 240 / frame->height;
    if (cell_w <= 0 || cell_h <= 0) {
        draw_splash_placeholder(display, nullptr);
        return;
    }

    const int16_t rendered_w = frame->width * cell_w;
    const int16_t rendered_h = frame->height * cell_h;
    const int16_t x0 = (240 - rendered_w) / 2;
    const int16_t y0 = (240 - rendered_h) / 2;

    display->fillScreen(COLOR_BG);
    for (uint8_t y = 0; y < frame->height; ++y) {
        for (uint8_t x = 0; x < frame->width; ++x) {
            const uint8_t code = frame->cells[y * frame->width + x];
            const uint16_t color = code < XINGZHI_SPLASH_PALETTE_SIZE ? frame->palette[code] : COLOR_BG;
            display->fillRect(x0 + x * cell_w, y0 + y * cell_h, cell_w, cell_h, color);
        }
    }
}

void draw_splash_screen(Arduino_GFX *display, const XingzhiUiState *state) {
    if (state && state->splash_frame) {
        draw_splash_frame(display, state->splash_frame);
        draw_power_badge(display, state);
        return;
    }
    draw_splash_placeholder(display, state);
    draw_power_badge(display, state);
}

}  // namespace

void xingzhi_meter_ui_draw(
    Arduino_GFX *display,
    const UsageData *data,
    MeterPayloadState payload_state,
    const char *detail
) {
    draw_usage_screen(display, data, payload_state, detail, nullptr);
}

void xingzhi_meter_ui_draw_screen(Arduino_GFX *display, const XingzhiUiState *state) {
    if (!display || !state) {
        return;
    }

    switch (state->screen) {
    case XingzhiScreen::Status:
        draw_status_screen(display, state);
        break;
    case XingzhiScreen::Splash:
        draw_splash_screen(display, state);
        break;
    case XingzhiScreen::Usage:
    default:
        draw_usage_screen(display, state->data, state->payload_state, state->detail, state);
        break;
    }
}
