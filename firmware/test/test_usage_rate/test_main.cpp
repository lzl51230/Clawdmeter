#include <unity.h>

#include "usage_rate.h"

void setUp(void) {
    usage_rate_reset();
    usage_rate_set_now_fn(nullptr);
}

void tearDown(void) {
    usage_rate_reset();
    usage_rate_set_now_fn(nullptr);
}

void test_usage_rate_reports_idle_while_window_is_too_short() {
    usage_rate_sample_at(10.0f, 0);
    usage_rate_sample_at(20.0f, 60000);

    TEST_ASSERT_EQUAL(0, usage_rate_group());
    TEST_ASSERT_EQUAL_STRING("idle", usage_rate_group_name(usage_rate_group()));
}

void test_usage_rate_groups_stable_growth_by_percent_per_minute() {
    usage_rate_sample_at(10.0f, 0);
    usage_rate_sample_at(10.6f, 300000);
    TEST_ASSERT_EQUAL(1, usage_rate_group());
    TEST_ASSERT_EQUAL_STRING("normal", usage_rate_group_name(usage_rate_group()));

    usage_rate_reset();
    usage_rate_sample_at(10.0f, 0);
    usage_rate_sample_at(11.25f, 300000);
    TEST_ASSERT_EQUAL(2, usage_rate_group());
    TEST_ASSERT_EQUAL_STRING("active", usage_rate_group_name(usage_rate_group()));

    usage_rate_reset();
    usage_rate_sample_at(10.0f, 0);
    usage_rate_sample_at(12.0f, 300000);
    TEST_ASSERT_EQUAL(3, usage_rate_group());
    TEST_ASSERT_EQUAL_STRING("heavy", usage_rate_group_name(usage_rate_group()));
}

void test_usage_rate_resets_after_session_percentage_drop() {
    usage_rate_sample_at(30.0f, 0);
    usage_rate_sample_at(34.0f, 300000);
    TEST_ASSERT_EQUAL(3, usage_rate_group());

    usage_rate_sample_at(20.0f, 360000);
    TEST_ASSERT_EQUAL(0, usage_rate_group());
}

void test_usage_rate_default_time_fn_can_be_injected() {
    static uint32_t now_ms = 0;
    auto now_fn = []() -> uint32_t { return now_ms; };

    usage_rate_set_now_fn(now_fn);
    now_ms = 0;
    usage_rate_sample(10.0f);
    now_ms = 300000;
    usage_rate_sample(12.0f);

    TEST_ASSERT_EQUAL(3, usage_rate_group());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_usage_rate_reports_idle_while_window_is_too_short);
    RUN_TEST(test_usage_rate_groups_stable_growth_by_percent_per_minute);
    RUN_TEST(test_usage_rate_resets_after_session_percentage_drop);
    RUN_TEST(test_usage_rate_default_time_fn_can_be_injected);
    return UNITY_END();
}
