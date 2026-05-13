#pragma once

#include <stddef.h>

#include "data.h"

enum class UsageParseResult {
    Empty,
    Valid,
    ErrorPayload,
    InvalidJson,
};

enum class UsageLineResult {
    None,
    Ready,
    Overflow,
};

class UsageLineReader {
public:
    static constexpr size_t kCapacity = 192;

    UsageLineResult push(char c);
    const char *line() const { return line_; }
    void clear();

private:
    char line_[kCapacity] = {};
    size_t length_ = 0;
    bool overflow_ = false;
};

UsageParseResult parse_usage_payload(const char *payload, UsageData *out);
