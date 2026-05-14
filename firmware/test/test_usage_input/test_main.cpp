#include <unity.h>

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
    return UNITY_END();
}
