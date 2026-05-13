#include "xingzhi_debug_serial.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

const char *skip_space(const char *value) {
    while (value && *value && isspace(static_cast<unsigned char>(*value))) {
        ++value;
    }
    return value ? value : "";
}

bool read_token(const char **cursor, char *buf, size_t len) {
    if (!cursor || !*cursor || !buf || len == 0) {
        return false;
    }

    const char *value = skip_space(*cursor);
    size_t index = 0;
    while (*value && !isspace(static_cast<unsigned char>(*value))) {
        if (index + 1 < len) {
            buf[index++] = *value;
        }
        ++value;
    }
    buf[index] = '\0';
    *cursor = value;
    return index > 0;
}

bool token_equals(const char *left, const char *right) {
    return left && right && strcmp(left, right) == 0;
}

const char *safe_value(const char *value) {
    return value && value[0] ? value : "-";
}

void sanitize_value(const char *value, char *buf, size_t len) {
    if (!buf || len == 0) {
        return;
    }
    const char *source = safe_value(value);
    size_t index = 0;
    while (*source && index + 1 < len) {
        const char c = *source++;
        buf[index++] = isspace(static_cast<unsigned char>(c)) ? '_' : c;
    }
    buf[index] = '\0';
}

}  // namespace

bool xingzhi_debug_is_usage_payload(const char *line) {
    return skip_space(line)[0] == '{';
}

XingzhiDebugCommand xingzhi_debug_parse_command(const char *line) {
    XingzhiDebugCommand command;
    const char *cursor = line;
    char prefix[16] = {};
    char token[sizeof(command.token)] = {};

    if (!read_token(&cursor, prefix, sizeof(prefix))) {
        command.type = XingzhiDebugCommandType::None;
        return command;
    }
    if (!token_equals(prefix, "XDBG")) {
        command.type = XingzhiDebugCommandType::Unknown;
        snprintf(command.token, sizeof(command.token), "%s", prefix);
        return command;
    }
    if (!read_token(&cursor, token, sizeof(token))) {
        command.type = XingzhiDebugCommandType::Unknown;
        return command;
    }

    snprintf(command.token, sizeof(command.token), "%s", token);
    if (token_equals(token, "STATUS")) {
        command.type = XingzhiDebugCommandType::Status;
    } else if (token_equals(token, "SCREENSHOT")) {
        command.type = XingzhiDebugCommandType::Screenshot;
    } else if (token_equals(token, "BUTTON")) {
        command.type = XingzhiDebugCommandType::Button;
        read_token(&cursor, command.arg1, sizeof(command.arg1));
        read_token(&cursor, command.arg2, sizeof(command.arg2));
    } else {
        command.type = XingzhiDebugCommandType::Unknown;
    }
    return command;
}

int xingzhi_debug_format_status(const XingzhiDebugStatus *status, char *buf, size_t len) {
    if (!status || !buf || len == 0) {
        return 0;
    }

    char detail[40];
    char ble_name[40];
    char action_error[40];
    sanitize_value(status->detail, detail, sizeof(detail));
    sanitize_value(status->ble_name, ble_name, sizeof(ble_name));
    sanitize_value(status->action_error, action_error, sizeof(action_error));
    return snprintf(
        buf,
        len,
        "XDBG STATUS target=%s screen=%s width=%d height=%d payload=%s source=%s ble=%s ble_name=%s ble_mac=%s hid=%s uptime_ms=%lu framebuffer=%s detail=%s action=%s event=%s action_count=%lu action_error=%s",
        safe_value(status->target),
        safe_value(status->screen),
        status->width,
        status->height,
        safe_value(status->payload),
        safe_value(status->source),
        safe_value(status->ble),
        ble_name,
        safe_value(status->ble_mac),
        safe_value(status->hid),
        static_cast<unsigned long>(status->uptime_ms),
        safe_value(status->framebuffer),
        detail,
        safe_value(status->action),
        safe_value(status->event),
        static_cast<unsigned long>(status->action_count),
        action_error
    );
}

int xingzhi_debug_format_screenshot_start(
    int width,
    int height,
    const char *pixel_format,
    size_t byte_count,
    char *buf,
    size_t len
) {
    if (!buf || len == 0) {
        return 0;
    }
    return snprintf(
        buf,
        len,
        "XDBG SCREENSHOT_START width=%d height=%d format=%s bytes=%lu",
        width,
        height,
        safe_value(pixel_format),
        static_cast<unsigned long>(byte_count)
    );
}

int xingzhi_debug_format_error(const char *code, const char *message, char *buf, size_t len) {
    if (!buf || len == 0) {
        return 0;
    }

    char clean_message[56];
    sanitize_value(message, clean_message, sizeof(clean_message));
    return snprintf(
        buf,
        len,
        "XDBG ERROR code=%s message=%s",
        safe_value(code),
        clean_message
    );
}
