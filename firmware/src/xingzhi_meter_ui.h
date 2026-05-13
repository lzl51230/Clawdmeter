#pragma once

#include <Arduino_GFX_Library.h>

#include "data.h"
#include "meter_format.h"

void xingzhi_meter_ui_draw(
    Arduino_GFX *display,
    const UsageData *data,
    MeterPayloadState payload_state,
    const char *detail
);
