#include <unity.h>
#include <string.h>

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

void test_model_label_includes_full_effort_and_unknown() {
    char buf[26];
    format_model_with_effort("gpt-6.1-sol", "high", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("gpt-6.1-sol [high]", buf);
    format_model_with_effort("gpt-6.1-sol", "xhigh", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("gpt-6.1-sol [xhigh]", buf);
    format_model_with_effort("model", nullptr, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("model [--]", buf);
    format_model_with_effort(nullptr, nullptr, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("-- [--]", buf);
}

void test_long_model_label_preserves_effort() {
    char buf[26];
    format_model_with_effort("gpt-a-very-long-model-identifier", "xhigh", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(25, strlen(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "..."));
    TEST_ASSERT_EQUAL_STRING(" [xhigh]", buf + 17);
    char tiny[1] = {'x'};
    format_model_with_effort("model", "high", tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL('\0', tiny[0]);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_format_percent_handles_valid_and_empty);
    RUN_TEST(test_format_reset_time_uses_short_labels);
    RUN_TEST(test_meter_level_accounts_for_payload_and_usage);
    RUN_TEST(test_bar_width_is_clamped);
    RUN_TEST(test_model_label_includes_full_effort_and_unknown);
    RUN_TEST(test_long_model_label_preserves_effort);
    return UNITY_END();
}
