#include <unity.h>

#include "meter_format.h"

void test_format_percent_handles_valid_and_empty() {
    char buf[8];

    format_percent(42.4f, true, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("42%", buf);

    format_percent(42.4f, false, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("--%", buf);
}

void test_format_reset_time_uses_short_labels() {
    char buf[24];

    format_reset_time(-1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Reset --", buf);

    format_reset_time(37, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Reset 37m", buf);

    format_reset_time(125, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Reset 2h 5m", buf);

    format_reset_time(1500, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("Reset 1d 1h", buf);
}

void test_meter_level_accounts_for_payload_and_usage() {
    UsageData data = {};
    data.valid = true;
    data.session_pct = 25;
    data.weekly_pct = 35;
    TEST_ASSERT_EQUAL(MeterLevel::Normal, meter_level_for(&data, MeterPayloadState::Valid));

    data.weekly_pct = 55;
    TEST_ASSERT_EQUAL(MeterLevel::Warning, meter_level_for(&data, MeterPayloadState::Valid));

    data.session_pct = 81;
    TEST_ASSERT_EQUAL(MeterLevel::High, meter_level_for(&data, MeterPayloadState::Valid));

    TEST_ASSERT_EQUAL(MeterLevel::Invalid, meter_level_for(&data, MeterPayloadState::Invalid));
}

void test_bar_width_is_clamped() {
    TEST_ASSERT_EQUAL(0, pct_to_bar_width(-10, 100));
    TEST_ASSERT_EQUAL(50, pct_to_bar_width(50, 100));
    TEST_ASSERT_EQUAL(100, pct_to_bar_width(150, 100));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_format_percent_handles_valid_and_empty);
    RUN_TEST(test_format_reset_time_uses_short_labels);
    RUN_TEST(test_meter_level_accounts_for_payload_and_usage);
    RUN_TEST(test_bar_width_is_clamped);
    return UNITY_END();
}
