#pragma once

#include <stddef.h>
#include <stdint.h>

enum class XingzhiDebugCommandType {
    None,
    Status,
    Screenshot,
    Unknown,
};

struct XingzhiDebugCommand {
    XingzhiDebugCommandType type = XingzhiDebugCommandType::None;
    char token[24] = {};
};

struct XingzhiDebugStatus {
    const char *target = "";
    const char *screen = "";
    int width = 0;
    int height = 0;
    const char *payload = "";
    const char *source = "";
    const char *ble = "";
    uint32_t uptime_ms = 0;
    const char *framebuffer = "";
    const char *detail = "";
};

bool xingzhi_debug_is_usage_payload(const char *line);
XingzhiDebugCommand xingzhi_debug_parse_command(const char *line);
int xingzhi_debug_format_status(const XingzhiDebugStatus *status, char *buf, size_t len);
int xingzhi_debug_format_screenshot_start(
    int width,
    int height,
    const char *pixel_format,
    size_t byte_count,
    char *buf,
    size_t len
);
int xingzhi_debug_format_error(const char *code, const char *message, char *buf, size_t len);
