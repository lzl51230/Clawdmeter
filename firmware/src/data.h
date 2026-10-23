#pragma once

struct CodexSettings {
    char model[64] = {};
    char plan_model[64] = {};
    char reasoning_effort[16] = {};
    char plan_reasoning_effort[16] = {};
    char service_tier[16] = {};
};

struct UsageData {
    float session_pct;       // 5-hour window utilization (0-100)
    int session_reset_mins;  // minutes until session resets
    float weekly_pct;        // 7-day window utilization (0-100)
    int weekly_reset_mins;   // minutes until weekly resets
    char status[16];         // "allowed" or "limited"
    char provider[12];       // "claude" or "codex"
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
    bool session_valid;      // false when this usage window is unavailable
    bool weekly_valid;
};
