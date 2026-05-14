#include "usage_input.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

bool is_blank(const char *value) {
    if (!value) {
        return true;
    }
    while (*value) {
        if (!isspace(static_cast<unsigned char>(*value))) {
            return false;
        }
        ++value;
    }
    return true;
}

void copy_status(char *dest, size_t len, const char *source) {
    if (!dest || len == 0) {
        return;
    }
    snprintf(dest, len, "%s", source ? source : "unknown");
}

void copy_provider(char *dest, size_t len, const char *source) {
    if (!dest || len == 0) {
        return;
    }
    snprintf(dest, len, "%s", source && strcmp(source, "codex") == 0 ? "codex" : "claude");
}

}  // namespace

UsageLineResult UsageLineReader::push(char c) {
    if (c == '\r') {
        return UsageLineResult::None;
    }

    if (c == '\n') {
        if (overflow_) {
            clear();
            return UsageLineResult::Overflow;
        }
        if (length_ == 0) {
            return UsageLineResult::None;
        }
        line_[length_] = '\0';
        return UsageLineResult::Ready;
    }

    if (overflow_) {
        return UsageLineResult::None;
    }

    if (length_ >= kCapacity - 1) {
        overflow_ = true;
        line_[0] = '\0';
        length_ = 0;
        return UsageLineResult::None;
    }

    line_[length_++] = c;
    line_[length_] = '\0';
    return UsageLineResult::None;
}

void UsageLineReader::clear() {
    line_[0] = '\0';
    length_ = 0;
    overflow_ = false;
}

UsageParseResult parse_usage_payload(const char *payload, UsageData *out) {
    if (!out || is_blank(payload)) {
        return UsageParseResult::Empty;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        return UsageParseResult::InvalidJson;
    }

    out->session_pct = doc["s"] | 0.0f;
    out->session_reset_mins = doc["sr"] | -1;
    out->weekly_pct = doc["w"] | 0.0f;
    out->weekly_reset_mins = doc["wr"] | -1;
    copy_status(out->status, sizeof(out->status), doc["st"] | "unknown");
    copy_provider(out->provider, sizeof(out->provider), doc["src"] | "claude");
    out->ok = doc["ok"] | false;
    out->valid = out->ok;

    if (!out->ok) {
        return UsageParseResult::ErrorPayload;
    }
    return UsageParseResult::Valid;
}
