#include <unity.h>

#include "xingzhi_power.h"

void test_adc_level_clamps_to_known_bands() {
    TEST_ASSERT_EQUAL(0, xingzhi_power_level_from_adc(1200));
    TEST_ASSERT_EQUAL(100, xingzhi_power_level_from_adc(3000));
    TEST_ASSERT_EQUAL(20, xingzhi_power_level_from_adc(2062));
    TEST_ASSERT_EQUAL(50, xingzhi_power_level_from_adc(2200));
}

void test_status_remains_sampling_until_required_samples() {
    xingzhi_power_init();
    xingzhi_power_reset_samples();

    xingzhi_power_record_sample(2062, true);
    XingzhiPowerStatus status = xingzhi_power_status();
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL_STRING("sampling", status.state);
    TEST_ASSERT_EQUAL(1, status.sample_count);

    xingzhi_power_record_sample(2154, true);
    status = xingzhi_power_status();
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL_STRING("sampling", status.state);
    TEST_ASSERT_EQUAL(2, status.sample_count);
}

void test_three_samples_produce_valid_average_level() {
    xingzhi_power_init();
    xingzhi_power_reset_samples();

    xingzhi_power_record_sample(2062, true);
    xingzhi_power_record_sample(2154, true);
    xingzhi_power_record_sample(2246, true);
    XingzhiPowerStatus status = xingzhi_power_status();

    TEST_ASSERT_TRUE(status.valid);
    TEST_ASSERT_EQUAL_STRING("valid", status.state);
    TEST_ASSERT_TRUE(status.charging);
    TEST_ASSERT_EQUAL(2154, status.average_adc);
    TEST_ASSERT_EQUAL(40, status.level);
    TEST_ASSERT_EQUAL(3, status.sample_count);
}

void test_invalid_sample_reports_error() {
    xingzhi_power_init();
    xingzhi_power_reset_samples();

    xingzhi_power_record_sample(5000, false);
    XingzhiPowerStatus status = xingzhi_power_status();

    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL_STRING("error", status.state);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_adc_level_clamps_to_known_bands);
    RUN_TEST(test_status_remains_sampling_until_required_samples);
    RUN_TEST(test_three_samples_produce_valid_average_level);
    RUN_TEST(test_invalid_sample_reports_error);
    return UNITY_END();
}
