#pragma once

#include <stddef.h>
#include <stdint.h>

#include "data.h"

enum class MeterLevel {
    NoData,
    Normal,
    Warning,
    High,
    Invalid,
};

enum class MeterPayloadState {
    NoData,
    Valid,
    Invalid,
};

void format_percent(float pct, bool valid, char *buf, size_t len);
void format_reset_time(int mins, char *buf, size_t len);
void format_payload_state(MeterPayloadState state, char *buf, size_t len);
MeterLevel meter_level_for(const UsageData *data, MeterPayloadState payload_state);
int pct_to_bar_width(float pct, int max_width);
