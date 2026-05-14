#pragma once

#include <stdint.h>

#include <Arduino_GFX_Library.h>

#include "data.h"
#include "meter_format.h"
#include "xingzhi_app_actions.h"
#include "xingzhi_splash_anim.h"

struct XingzhiUiState {
    XingzhiScreen screen = XingzhiScreen::Usage;
    const UsageData *data = nullptr;
    MeterPayloadState payload_state = MeterPayloadState::NoData;
    const char *detail = "";
    const char *last_source = "none";
    const char *ble_state = "disabled";
    const char *ble_detail = "";
    const char *last_error = "";
    const char *last_action = "none";
    uint32_t action_count = 0;
    bool power_valid = false;
    int battery_level = -1;
    bool charging = false;
    const char *power_detail = "unknown";
    const XingzhiSplashFrame *splash_frame = nullptr;
};

void xingzhi_meter_ui_draw(
    Arduino_GFX *display,
    const UsageData *data,
    MeterPayloadState payload_state,
    const char *detail
);

void xingzhi_meter_ui_draw_screen(Arduino_GFX *display, const XingzhiUiState *state);
