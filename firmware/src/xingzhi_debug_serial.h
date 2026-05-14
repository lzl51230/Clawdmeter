#pragma once

#include <stddef.h>
#include <stdint.h>

enum class XingzhiDebugCommandType {
    None,
    Status,
    Screenshot,
    Button,
    Ble,
    Probe,
    Unknown,
};

struct XingzhiDebugCommand {
    XingzhiDebugCommandType type = XingzhiDebugCommandType::None;
    char token[24] = {};
    char arg1[24] = {};
    char arg2[24] = {};
};

struct XingzhiDebugStatus {
    const char *target = "";
    const char *screen = "";
    int width = 0;
    int height = 0;
    const char *payload = "";
    const char *source = "";
    const char *ble = "";
    const char *ble_name = "";
    const char *ble_mac = "";
    const char *hid = "";
    const char *hid_battery = "";
    const char *power = "";
    const char *battery = "";
    const char *charging = "";
    const char *adc = "";
    const char *samples = "";
    uint32_t uptime_ms = 0;
    const char *framebuffer = "";
    const char *detail = "";
    const char *action = "";
    const char *event = "";
    uint32_t action_count = 0;
    const char *action_error = "";
    const char *splash = "";
    const char *splash_group = "";
    const char *splash_category = "";
    const char *splash_frame = "";
};

struct XingzhiDebugProbeResult {
    const char *target = "";
    const char *probe = "";
    bool ok = false;
    const char *status = "";
    const char *method = "";
    const char *detail = "";
    const char *checked = "";
};

bool xingzhi_debug_is_usage_payload(const char *line);
XingzhiDebugCommand xingzhi_debug_parse_command(const char *line);
int xingzhi_debug_format_status(const XingzhiDebugStatus *status, char *buf, size_t len);
int xingzhi_debug_format_probe(const XingzhiDebugProbeResult *result, char *buf, size_t len);
int xingzhi_debug_format_screenshot_start(
    int width,
    int height,
    const char *pixel_format,
    size_t byte_count,
    char *buf,
    size_t len
);
int xingzhi_debug_format_error(const char *code, const char *message, char *buf, size_t len);
