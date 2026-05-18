#include "xingzhi_voice_transport.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t FRAME_VERSION = 1;
constexpr size_t META_FRAME_BYTES = 16;
constexpr size_t CHUNK_HEADER_BYTES = 9;
constexpr size_t COMPLETE_FRAME_BYTES = 8;

const uint8_t *audio_data = nullptr;
size_t audio_len = 0;
size_t sent_bytes = 0;
uint16_t sent_chunks = 0;
uint16_t total_chunks = 0;
uint32_t waiting_since_ms = 0;
XingzhiVoiceTransportPhase phase = XingzhiVoiceTransportPhase::Idle;
char detail[48] = "idle";
char error[48] = "";

void set_detail(const char *value) {
    snprintf(detail, sizeof(detail), "%s", value ? value : "");
}

void set_error(const char *value) {
    snprintf(error, sizeof(error), "%s", value ? value : "transport_error");
    set_detail(error);
    phase = XingzhiVoiceTransportPhase::Error;
}

void write_u16(uint8_t *dest, uint16_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xff);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void write_u32(uint8_t *dest, uint32_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xff);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    dest[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    dest[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

bool starts_with(const char *value, const char *prefix) {
    return value && prefix && strncmp(value, prefix, strlen(prefix)) == 0;
}

}  // namespace

void xingzhi_voice_transport_init() {
    audio_data = nullptr;
    audio_len = 0;
    sent_bytes = 0;
    sent_chunks = 0;
    total_chunks = 0;
    waiting_since_ms = 0;
    phase = XingzhiVoiceTransportPhase::Idle;
    set_detail("idle");
    error[0] = '\0';
}

XingzhiVoiceTransportResult xingzhi_voice_transport_begin(const uint8_t *data, size_t len, uint32_t now_ms) {
    if (!data || len == 0) {
        set_error("empty_audio");
        return {false, true, "empty_audio"};
    }
    if (len > UINT32_MAX) {
        set_error("audio_too_large");
        return {false, true, "audio_too_large"};
    }
    audio_data = data;
    audio_len = len;
    sent_bytes = 0;
    sent_chunks = 0;
    total_chunks = static_cast<uint16_t>((len + XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD - 1) / XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD);
    waiting_since_ms = now_ms;
    phase = XingzhiVoiceTransportPhase::Sending;
    set_detail("metadata");
    error[0] = '\0';
    return {true, true, "sending"};
}

XingzhiVoiceTransportResult xingzhi_voice_transport_next_frame(XingzhiVoiceFrame *frame) {
    if (!frame) {
        return {false, false, "missing_frame"};
    }
    memset(frame, 0, sizeof(*frame));
    if (phase != XingzhiVoiceTransportPhase::Sending) {
        return {true, false, "unchanged"};
    }

    if (sent_chunks == 0 && sent_bytes == 0) {
        frame->data[0] = 'M';
        frame->data[1] = FRAME_VERSION;
        write_u32(frame->data + 2, static_cast<uint32_t>(audio_len));
        write_u16(frame->data + 6, static_cast<uint16_t>(XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD));
        write_u16(frame->data + 8, total_chunks);
        write_u32(frame->data + 10, 16000);
        write_u16(frame->data + 14, 16);
        frame->len = META_FRAME_BYTES;
        frame->valid = true;
        set_detail("metadata");
        sent_chunks = 1;
        return {true, true, "metadata"};
    }

    if (sent_bytes < audio_len) {
        const size_t remaining = audio_len - sent_bytes;
        const size_t payload = remaining < XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD
            ? remaining
            : XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD;
        frame->data[0] = 'C';
        frame->data[1] = FRAME_VERSION;
        write_u16(frame->data + 2, static_cast<uint16_t>(sent_chunks - 1));
        write_u32(frame->data + 4, static_cast<uint32_t>(sent_bytes));
        frame->data[8] = static_cast<uint8_t>(payload);
        memcpy(frame->data + CHUNK_HEADER_BYTES, audio_data + sent_bytes, payload);
        frame->len = CHUNK_HEADER_BYTES + payload;
        frame->valid = true;
        sent_bytes += payload;
        sent_chunks += 1;
        set_detail("chunk");
        return {true, true, "chunk"};
    }

    frame->data[0] = 'E';
    frame->data[1] = FRAME_VERSION;
    write_u16(frame->data + 2, total_chunks);
    write_u32(frame->data + 4, static_cast<uint32_t>(audio_len));
    frame->len = COMPLETE_FRAME_BYTES;
    frame->valid = true;
    frame->complete = true;
    phase = XingzhiVoiceTransportPhase::WaitingAck;
    waiting_since_ms = 0;
    set_detail("waiting_ack");
    return {true, true, "complete"};
}

XingzhiVoiceTransportResult xingzhi_voice_transport_handle_control(const char *message) {
    if (starts_with(message, "ack") || starts_with(message, "done")) {
        phase = XingzhiVoiceTransportPhase::Done;
        set_detail("done");
        error[0] = '\0';
        return {true, true, "done"};
    }
    if (starts_with(message, "error") || starts_with(message, "err")) {
        set_error("host_error");
        return {false, true, "host_error"};
    }
    return {true, false, "ignored"};
}

XingzhiVoiceTransportResult xingzhi_voice_transport_tick(uint32_t now_ms) {
    if (phase != XingzhiVoiceTransportPhase::WaitingAck) {
        return {true, false, "unchanged"};
    }
    if (waiting_since_ms == 0) {
        waiting_since_ms = now_ms;
        return {true, false, "waiting_ack"};
    }
    if (now_ms - waiting_since_ms >= XINGZHI_VOICE_TRANSPORT_ACK_TIMEOUT_MS) {
        set_error("ack_timeout");
        return {false, true, "ack_timeout"};
    }
    return {true, false, "waiting_ack"};
}

void xingzhi_voice_transport_set_error(const char *message) {
    set_error(message && message[0] ? message : "transport_error");
}

XingzhiVoiceTransportStatus xingzhi_voice_transport_status() {
    XingzhiVoiceTransportStatus status = {};
    status.phase = phase;
    status.detail = detail;
    status.error = error;
    status.total_bytes = audio_len;
    status.sent_bytes = sent_bytes;
    status.total_chunks = total_chunks;
    status.sent_chunks = sent_chunks > 0 ? sent_chunks - 1 : 0;
    return status;
}

const char *xingzhi_voice_transport_phase_name(XingzhiVoiceTransportPhase value) {
    switch (value) {
    case XingzhiVoiceTransportPhase::Sending:
        return "sending";
    case XingzhiVoiceTransportPhase::WaitingAck:
        return "waiting_ack";
    case XingzhiVoiceTransportPhase::Done:
        return "done";
    case XingzhiVoiceTransportPhase::Error:
        return "error";
    case XingzhiVoiceTransportPhase::Idle:
    default:
        return "idle";
    }
}
