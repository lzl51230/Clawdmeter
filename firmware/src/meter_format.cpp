#include "meter_format.h"

#include <math.h>
#include <stdio.h>

namespace {

float clamp_pct(float pct) {
    if (isnan(pct) || pct < 0.0f) {
        return 0.0f;
    }
    if (pct > 100.0f) {
        return 100.0f;
    }
    return pct;
}

}  // namespace

void format_percent(float pct, bool valid, char *buf, size_t len) {
    if (!buf || len == 0) {
        return;
    }
    if (!valid) {
        snprintf(buf, len, "--%%");
        return;
    }
    snprintf(buf, len, "%.0f%%", clamp_pct(pct));
}

void format_reset_time(int mins, char *buf, size_t len) {
    if (!buf || len == 0) {
        return;
    }
    if (mins < 0) {
        snprintf(buf, len, "Reset --");
    } else if (mins < 60) {
        snprintf(buf, len, "Reset %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Reset %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Reset %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

void format_payload_state(MeterPayloadState state, char *buf, size_t len) {
    if (!buf || len == 0) {
        return;
    }
    switch (state) {
    case MeterPayloadState::Valid:
        snprintf(buf, len, "Payload OK");
        break;
    case MeterPayloadState::Invalid:
        snprintf(buf, len, "Payload invalid");
        break;
    case MeterPayloadState::NoData:
    default:
        snprintf(buf, len, "Waiting for serial");
        break;
    }
}

MeterLevel meter_level_for(const UsageData *data, MeterPayloadState payload_state) {
    if (payload_state == MeterPayloadState::Invalid) {
        return MeterLevel::Invalid;
    }
    if (!data || !data->valid) {
        return MeterLevel::NoData;
    }

    float peak = data->session_pct > data->weekly_pct ? data->session_pct : data->weekly_pct;
    if (peak >= 80.0f) {
        return MeterLevel::High;
    }
    if (peak >= 50.0f) {
        return MeterLevel::Warning;
    }
    return MeterLevel::Normal;
}

int pct_to_bar_width(float pct, int max_width) {
    if (max_width <= 0) {
        return 0;
    }
    return static_cast<int>((clamp_pct(pct) * max_width) / 100.0f + 0.5f);
}
