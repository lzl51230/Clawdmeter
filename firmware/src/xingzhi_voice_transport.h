#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD = 160;
constexpr uint32_t XINGZHI_VOICE_TRANSPORT_ACK_TIMEOUT_MS = 15000;

enum class XingzhiVoiceTransportPhase {
    Idle,
    Sending,
    WaitingAck,
    Done,
    Error,
};

struct XingzhiVoiceTransportStatus {
    XingzhiVoiceTransportPhase phase = XingzhiVoiceTransportPhase::Idle;
    const char *detail = "idle";
    const char *error = "";
    size_t total_bytes = 0;
    size_t sent_bytes = 0;
    uint16_t total_chunks = 0;
    uint16_t sent_chunks = 0;
};

struct XingzhiVoiceFrame {
    size_t len = 0;
    bool valid = false;
    bool complete = false;
    uint8_t data[180] = {};
};

struct XingzhiVoiceTransportResult {
    bool ok = false;
    bool changed = false;
    const char *message = "";
};

void xingzhi_voice_transport_init();
XingzhiVoiceTransportResult xingzhi_voice_transport_begin(const uint8_t *data, size_t len, uint32_t now_ms);
XingzhiVoiceTransportResult xingzhi_voice_transport_next_frame(XingzhiVoiceFrame *frame);
XingzhiVoiceTransportResult xingzhi_voice_transport_handle_control(const char *message);
XingzhiVoiceTransportResult xingzhi_voice_transport_tick(uint32_t now_ms);
void xingzhi_voice_transport_set_error(const char *message);
XingzhiVoiceTransportStatus xingzhi_voice_transport_status();
const char *xingzhi_voice_transport_phase_name(XingzhiVoiceTransportPhase phase);
