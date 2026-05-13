#include "xingzhi_power.h"

#include <cstddef>

#ifdef ARDUINO
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#endif

namespace {

constexpr uint32_t POLL_MS = 1000;
constexpr uint8_t REQUIRED_SAMPLES = 3;
constexpr uint8_t SAMPLE_WINDOW = 3;

struct AdcBand {
    int adc;
    int level;
};

constexpr AdcBand LEVELS[] = {
    {1970, 0},
    {2062, 20},
    {2154, 40},
    {2246, 60},
    {2338, 80},
    {2430, 100},
};

bool initialized = false;
bool adc_ready = false;
bool read_error = false;
bool charging_now = false;
uint32_t last_poll_ms = 0;
int samples[SAMPLE_WINDOW] = {};
uint8_t sample_count = 0;
uint8_t sample_index = 0;
int raw_adc = -1;
int average_adc = -1;
int battery_level = -1;

#ifdef ARDUINO
adc_oneshot_unit_handle_t adc_handle = nullptr;
#endif

int sample_sum() {
    int total = 0;
    for (uint8_t index = 0; index < sample_count; ++index) {
        total += samples[index];
    }
    return total;
}

void clear_status() {
    sample_count = 0;
    sample_index = 0;
    raw_adc = -1;
    average_adc = -1;
    battery_level = -1;
}

}  // namespace

int xingzhi_power_level_from_adc(int adc_value) {
    if (adc_value <= LEVELS[0].adc) {
        return 0;
    }
    const size_t last = sizeof(LEVELS) / sizeof(LEVELS[0]) - 1;
    if (adc_value >= LEVELS[last].adc) {
        return 100;
    }

    for (size_t index = 0; index < last; ++index) {
        const AdcBand low = LEVELS[index];
        const AdcBand high = LEVELS[index + 1];
        if (adc_value >= low.adc && adc_value < high.adc) {
            const float ratio = static_cast<float>(adc_value - low.adc) / static_cast<float>(high.adc - low.adc);
            return low.level + static_cast<int>(ratio * static_cast<float>(high.level - low.level));
        }
    }
    return 0;
}

void xingzhi_power_reset_samples() {
    clear_status();
    read_error = false;
}

void xingzhi_power_record_sample(int adc_value, bool charging) {
    if (adc_value < 0 || adc_value > 4095) {
        read_error = true;
        return;
    }

    raw_adc = adc_value;
    charging_now = charging;
    samples[sample_index] = adc_value;
    sample_index = (sample_index + 1) % SAMPLE_WINDOW;
    if (sample_count < SAMPLE_WINDOW) {
        sample_count += 1;
    }
    average_adc = sample_sum() / sample_count;
    battery_level = xingzhi_power_level_from_adc(average_adc);
    read_error = false;
}

void xingzhi_power_init() {
    initialized = true;
    adc_ready = false;
    read_error = false;
    clear_status();

#ifdef ARDUINO
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << GPIO_NUM_38;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    if (gpio_config(&io_conf) != ESP_OK) {
        read_error = true;
        return;
    }

    adc_oneshot_unit_init_cfg_t init_config = {};
    init_config.unit_id = ADC_UNIT_2;
    init_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    if (adc_oneshot_new_unit(&init_config, &adc_handle) != ESP_OK) {
        read_error = true;
        return;
    }

    adc_oneshot_chan_cfg_t chan_config = {};
    chan_config.atten = ADC_ATTEN_DB_12;
    chan_config.bitwidth = ADC_BITWIDTH_12;
    if (adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_6, &chan_config) != ESP_OK) {
        read_error = true;
        return;
    }
    adc_ready = true;
#else
    adc_ready = true;
#endif
}

void xingzhi_power_tick(uint32_t now_ms) {
    if (!initialized || !adc_ready || read_error) {
        return;
    }
    if (now_ms - last_poll_ms < POLL_MS) {
        return;
    }
    last_poll_ms = now_ms;

#ifdef ARDUINO
    int adc_value = 0;
    esp_err_t err = adc_oneshot_read(adc_handle, ADC_CHANNEL_6, &adc_value);
    if (err != ESP_OK) {
        read_error = true;
        return;
    }
    xingzhi_power_record_sample(adc_value, gpio_get_level(GPIO_NUM_38) == 1);
#endif
}

XingzhiPowerStatus xingzhi_power_status() {
    XingzhiPowerStatus status = {};
    status.charging = charging_now;
    status.raw_adc = raw_adc;
    status.average_adc = average_adc;
    status.level = battery_level;
    status.sample_count = sample_count;

    if (read_error) {
        status.state = "error";
        return status;
    }
    if (!initialized || !adc_ready) {
        status.state = "unavailable";
        return status;
    }
    if (sample_count < REQUIRED_SAMPLES) {
        status.state = "sampling";
        return status;
    }

    status.state = "valid";
    status.valid = true;
    return status;
}
