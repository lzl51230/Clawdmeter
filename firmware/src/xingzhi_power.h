#pragma once

#include <stdint.h>

struct XingzhiPowerStatus {
    const char *state = "unavailable";
    bool valid = false;
    bool charging = false;
    int raw_adc = -1;
    int average_adc = -1;
    int level = -1;
    int hid_level = -1;
    uint8_t sample_count = 0;
};

void xingzhi_power_init();
void xingzhi_power_tick(uint32_t now_ms);
XingzhiPowerStatus xingzhi_power_status();

void xingzhi_power_reset_samples();
void xingzhi_power_record_sample(int adc_value, bool charging);
int xingzhi_power_level_from_adc(int average_adc);
