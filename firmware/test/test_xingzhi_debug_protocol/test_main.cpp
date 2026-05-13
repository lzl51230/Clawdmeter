#include <unity.h>

#include <string.h>

#include "xingzhi_debug_serial.h"

void test_usage_payload_detection_uses_json_prefix() {
    TEST_ASSERT_TRUE(xingzhi_debug_is_usage_payload("{\"s\":42}"));
    TEST_ASSERT_TRUE(xingzhi_debug_is_usage_payload("  {\"s\":42}"));
    TEST_ASSERT_FALSE(xingzhi_debug_is_usage_payload(""));
    TEST_ASSERT_FALSE(xingzhi_debug_is_usage_payload("XDBG STATUS"));
}

void test_debug_command_parser_accepts_known_commands() {
    XingzhiDebugCommand command = xingzhi_debug_parse_command("XDBG STATUS");
    TEST_ASSERT_EQUAL(XingzhiDebugCommandType::Status, command.type);

    command = xingzhi_debug_parse_command("XDBG SCREENSHOT");
    TEST_ASSERT_EQUAL(XingzhiDebugCommandType::Screenshot, command.type);

    command = xingzhi_debug_parse_command("XDBG BUTTON cycle click");
    TEST_ASSERT_EQUAL(XingzhiDebugCommandType::Button, command.type);
    TEST_ASSERT_EQUAL_STRING("cycle", command.arg1);
    TEST_ASSERT_EQUAL_STRING("click", command.arg2);
}

void test_debug_command_parser_reports_unknown_commands() {
    XingzhiDebugCommand command = xingzhi_debug_parse_command("XDBG NOPE");

    TEST_ASSERT_EQUAL(XingzhiDebugCommandType::Unknown, command.type);
    TEST_ASSERT_EQUAL_STRING("NOPE", command.token);
}

void test_status_line_is_bounded_and_parseable() {
    XingzhiDebugStatus status = {};
    status.target = "xingzhi_parity";
    status.screen = "usage";
    status.width = 240;
    status.height = 240;
    status.payload = "valid";
    status.source = "serial";
    status.ble = "disabled";
    status.ble_name = "Claude Controller";
    status.hid = "unavailable";
    status.power = "valid";
    status.battery = "80";
    status.charging = "1";
    status.adc = "2338";
    status.samples = "3";
    status.uptime_ms = 1234;
    status.framebuffer = "ready";
    status.detail = "allowed";

    char line[560];
    int written = xingzhi_debug_format_status(&status, line, sizeof(line));

    TEST_ASSERT_GREATER_THAN(0, written);
    TEST_ASSERT_LESS_THAN(sizeof(line), static_cast<size_t>(written));
    TEST_ASSERT_NOT_NULL(strstr(line, "XDBG STATUS"));
    TEST_ASSERT_NOT_NULL(strstr(line, "target=xingzhi_parity"));
    TEST_ASSERT_NOT_NULL(strstr(line, "screen=usage"));
    TEST_ASSERT_NOT_NULL(strstr(line, "width=240"));
    TEST_ASSERT_NOT_NULL(strstr(line, "height=240"));
    TEST_ASSERT_NOT_NULL(strstr(line, "source=serial"));
    TEST_ASSERT_NOT_NULL(strstr(line, "ble_name=Claude_Controller"));
    TEST_ASSERT_NOT_NULL(strstr(line, "hid=unavailable"));
    TEST_ASSERT_NOT_NULL(strstr(line, "power=valid"));
    TEST_ASSERT_NOT_NULL(strstr(line, "battery=80"));
}

void test_screenshot_start_line_contains_frame_metadata() {
    char line[128];
    int written = xingzhi_debug_format_screenshot_start(240, 240, "RGB565LE", 115200, line, sizeof(line));

    TEST_ASSERT_GREATER_THAN(0, written);
    TEST_ASSERT_EQUAL_STRING(
        "XDBG SCREENSHOT_START width=240 height=240 format=RGB565LE bytes=115200",
        line
    );
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_usage_payload_detection_uses_json_prefix);
    RUN_TEST(test_debug_command_parser_accepts_known_commands);
    RUN_TEST(test_debug_command_parser_reports_unknown_commands);
    RUN_TEST(test_status_line_is_bounded_and_parseable);
    RUN_TEST(test_screenshot_start_line_contains_frame_metadata);
    return UNITY_END();
}
