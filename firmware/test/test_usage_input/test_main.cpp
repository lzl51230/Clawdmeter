#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "usage_input.h"

void test_valid_payload_maps_usage_fields() {
    UsageData data = {};

    UsageParseResult result = parse_usage_payload(
        "{\"s\":42.5,\"sr\":37,\"w\":18,\"wr\":720,\"st\":\"allowed\",\"ok\":true}",
        &data
    );

    TEST_ASSERT_EQUAL(UsageParseResult::Valid, result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.5f, data.session_pct);
    TEST_ASSERT_EQUAL(37, data.session_reset_mins);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.0f, data.weekly_pct);
    TEST_ASSERT_EQUAL(720, data.weekly_reset_mins);
    TEST_ASSERT_EQUAL_STRING("allowed", data.status);
    TEST_ASSERT_EQUAL_STRING("claude", data.provider);
    TEST_ASSERT_TRUE(data.ok);
    TEST_ASSERT_TRUE(data.valid);
}

void test_codex_source_is_parsed() {
    UsageData data = {};

    UsageParseResult result = parse_usage_payload(
        "{\"s\":11,\"sr\":5,\"w\":22,\"wr\":60,\"st\":\"allowed\",\"src\":\"codex\",\"ok\":true}",
        &data
    );

    TEST_ASSERT_EQUAL(UsageParseResult::Valid, result);
    TEST_ASSERT_EQUAL_STRING("codex", data.provider);
}

void test_unknown_source_defaults_to_claude() {
    UsageData data = {};

    UsageParseResult result = parse_usage_payload(
        "{\"s\":11,\"w\":22,\"st\":\"allowed\",\"src\":\"other\",\"ok\":true}",
        &data
    );

    TEST_ASSERT_EQUAL(UsageParseResult::Valid, result);
    TEST_ASSERT_EQUAL_STRING("claude", data.provider);
}

void test_missing_reset_fields_default_to_minus_one() {
    UsageData data = {};

    UsageParseResult result = parse_usage_payload(
        "{\"s\":10,\"w\":20,\"st\":\"allowed\",\"ok\":true}",
        &data
    );

    TEST_ASSERT_EQUAL(UsageParseResult::Valid, result);
    TEST_ASSERT_EQUAL(-1, data.session_reset_mins);
    TEST_ASSERT_EQUAL(-1, data.weekly_reset_mins);
}

void test_error_payload_is_not_valid_reading() {
    UsageData data = {};

    UsageParseResult result = parse_usage_payload(
        "{\"s\":0,\"w\":0,\"st\":\"error\",\"ok\":false}",
        &data
    );

    TEST_ASSERT_EQUAL(UsageParseResult::ErrorPayload, result);
    TEST_ASSERT_EQUAL_STRING("error", data.status);
    TEST_ASSERT_FALSE(data.ok);
    TEST_ASSERT_FALSE(data.valid);
}

void test_malformed_payload_is_invalid_json() {
    UsageData data = {};

    TEST_ASSERT_EQUAL(UsageParseResult::InvalidJson, parse_usage_payload("{nope", &data));
}

void test_line_reader_emits_on_newline() {
    UsageLineReader reader;

    TEST_ASSERT_EQUAL(UsageLineResult::None, reader.push('{'));
    TEST_ASSERT_EQUAL(UsageLineResult::None, reader.push('}'));
    TEST_ASSERT_EQUAL(UsageLineResult::Ready, reader.push('\n'));
    TEST_ASSERT_EQUAL_STRING("{}", reader.line());
}

void test_line_reader_reports_overflow_at_boundary() {
    UsageLineReader reader;

    for (size_t i = 0; i < UsageLineReader::kCapacity + 3; ++i) {
        reader.push('x');
    }

    TEST_ASSERT_EQUAL(UsageLineResult::Overflow, reader.push('\n'));
    TEST_ASSERT_EQUAL(UsageLineResult::None, reader.push('\n'));
}

void test_settings_are_parsed_with_usage() {
    UsageData data = {};
    CodexSettings settings = {};
    auto result = parse_usage_payload(
        "{\"s\":23,\"w\":42,\"ok\":true,\"cfg\":{\"model\":\"gpt-example\",\"plan_model\":\"gpt-plan\",\"service_tier\":\"ultrafast\"}}",
        &data, &settings
    );
    TEST_ASSERT_EQUAL(UsageParseResult::Valid, result);
    TEST_ASSERT_EQUAL_STRING("gpt-example", settings.model);
    TEST_ASSERT_EQUAL_STRING("gpt-plan", settings.plan_model);
    TEST_ASSERT_EQUAL_STRING("ultrafast", settings.service_tier);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23, data.session_pct);
}

void test_config_only_preserves_usage_and_legacy_preserves_settings() {
    UsageData data = {};
    data.session_pct = 37;
    data.valid = true;
    CodexSettings settings = {};
    auto result = parse_usage_payload("{\"cfg\":{\"model\":\"example\",\"service_tier\":\"fast\"}}", &data, &settings);
    TEST_ASSERT_EQUAL(UsageParseResult::ConfigOnly, result);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 37, data.session_pct);
    TEST_ASSERT_TRUE(data.valid);
    TEST_ASSERT_EQUAL_STRING("example", settings.model);
    result = parse_usage_payload("{\"s\":25,\"ok\":true}", &data, &settings);
    TEST_ASSERT_EQUAL(UsageParseResult::Valid, result);
    TEST_ASSERT_EQUAL_STRING("example", settings.model);
    TEST_ASSERT_EQUAL_STRING("fast", settings.service_tier);
    result = parse_usage_payload("{malformed", &data, &settings);
    TEST_ASSERT_EQUAL(UsageParseResult::InvalidJson, result);
    TEST_ASSERT_EQUAL_STRING("example", settings.model);
}

void test_settings_lengths_and_unknown_fields() {
    UsageData data = {};
    CodexSettings settings = {};
    char input[400];
    char long_model[100];
    memset(long_model, 'm', sizeof(long_model) - 1);
    long_model[sizeof(long_model) - 1] = '\0';
    snprintf(input, sizeof(input), "{\"cfg\":{\"model\":\"%s\",\"plan_model\":17}}", long_model);
    TEST_ASSERT_EQUAL(UsageParseResult::ConfigOnly, parse_usage_payload(input, &data, &settings));
    TEST_ASSERT_EQUAL(63, strlen(settings.model));
    TEST_ASSERT_EQUAL_STRING("", settings.plan_model);
    TEST_ASSERT_EQUAL_STRING("", settings.service_tier);
    TEST_ASSERT_EQUAL(UsageParseResult::ConfigOnly, parse_usage_payload("{\"cfg\":{}}", &data, &settings));
    TEST_ASSERT_EQUAL_STRING("", settings.model);
}

void test_full_capacity_serial_line() {
    UsageLineReader reader;
    for (size_t i = 0; i < UsageLineReader::kCapacity - 1; ++i) {
        TEST_ASSERT_EQUAL(UsageLineResult::None, reader.push('x'));
    }
    TEST_ASSERT_EQUAL(UsageLineResult::Ready, reader.push('\n'));
    TEST_ASSERT_EQUAL(511, strlen(reader.line()));
    reader.clear();
    for (size_t i = 0; i < UsageLineReader::kCapacity; ++i) {
        reader.push('x');
    }
    TEST_ASSERT_EQUAL(UsageLineResult::Overflow, reader.push('\n'));
    reader.push('{');
    reader.push('}');
    TEST_ASSERT_EQUAL(UsageLineResult::Ready, reader.push('\n'));
    TEST_ASSERT_EQUAL_STRING("{}", reader.line());
}

void test_missing_window_is_not_valid_zero_percent() {
    UsageData data = {};
    TEST_ASSERT_EQUAL(UsageParseResult::Valid, parse_usage_payload("{\"s\":null,\"w\":11,\"ok\":true}", &data));
    TEST_ASSERT_FALSE(data.session_valid);
    TEST_ASSERT_TRUE(data.weekly_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 11, data.weekly_pct);
    TEST_ASSERT_EQUAL(UsageParseResult::Valid, parse_usage_payload("{\"s\":0,\"w\":0.5,\"ok\":true}", &data));
    TEST_ASSERT_TRUE(data.session_valid);
    TEST_ASSERT_TRUE(data.weekly_valid);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_payload_maps_usage_fields);
    RUN_TEST(test_codex_source_is_parsed);
    RUN_TEST(test_unknown_source_defaults_to_claude);
    RUN_TEST(test_missing_reset_fields_default_to_minus_one);
    RUN_TEST(test_error_payload_is_not_valid_reading);
    RUN_TEST(test_malformed_payload_is_invalid_json);
    RUN_TEST(test_line_reader_emits_on_newline);
    RUN_TEST(test_line_reader_reports_overflow_at_boundary);
    RUN_TEST(test_settings_are_parsed_with_usage);
    RUN_TEST(test_config_only_preserves_usage_and_legacy_preserves_settings);
    RUN_TEST(test_settings_lengths_and_unknown_fields);
    RUN_TEST(test_full_capacity_serial_line);
    RUN_TEST(test_missing_window_is_not_valid_zero_percent);
    return UNITY_END();
}
