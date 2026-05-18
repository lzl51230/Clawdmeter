#include <unity.h>

#include "xingzhi_voice_transport.h"

namespace {

uint32_t read_u32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

uint16_t read_u16(const uint8_t *data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

}  // namespace

void test_transport_rejects_empty_audio() {
    xingzhi_voice_transport_init();

    XingzhiVoiceTransportResult result = xingzhi_voice_transport_begin(nullptr, 0, 100);

    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("empty_audio", result.message);
    TEST_ASSERT_EQUAL(XingzhiVoiceTransportPhase::Error, xingzhi_voice_transport_status().phase);
}

void test_transport_emits_metadata_chunks_and_complete() {
    uint8_t audio[200] = {};
    for (size_t index = 0; index < sizeof(audio); ++index) {
        audio[index] = static_cast<uint8_t>(index);
    }
    xingzhi_voice_transport_init();
    TEST_ASSERT_TRUE(xingzhi_voice_transport_begin(audio, sizeof(audio), 100).ok);

    XingzhiVoiceFrame frame = {};
    TEST_ASSERT_TRUE(xingzhi_voice_transport_next_frame(&frame).ok);
    TEST_ASSERT_TRUE(frame.valid);
    TEST_ASSERT_EQUAL_UINT8('M', frame.data[0]);
    TEST_ASSERT_EQUAL_UINT32(sizeof(audio), read_u32(frame.data + 2));
    TEST_ASSERT_EQUAL_UINT16(XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD, read_u16(frame.data + 6));
    TEST_ASSERT_EQUAL_UINT16(2, read_u16(frame.data + 8));

    TEST_ASSERT_TRUE(xingzhi_voice_transport_next_frame(&frame).ok);
    TEST_ASSERT_EQUAL_UINT8('C', frame.data[0]);
    TEST_ASSERT_EQUAL_UINT16(0, read_u16(frame.data + 2));
    TEST_ASSERT_EQUAL_UINT32(0, read_u32(frame.data + 4));
    TEST_ASSERT_EQUAL_UINT8(XINGZHI_VOICE_TRANSPORT_CHUNK_PAYLOAD, frame.data[8]);
    TEST_ASSERT_EQUAL_UINT8(0, frame.data[9]);
    TEST_ASSERT_EQUAL_UINT8(159, frame.data[9 + 159]);

    TEST_ASSERT_TRUE(xingzhi_voice_transport_next_frame(&frame).ok);
    TEST_ASSERT_EQUAL_UINT8('C', frame.data[0]);
    TEST_ASSERT_EQUAL_UINT16(1, read_u16(frame.data + 2));
    TEST_ASSERT_EQUAL_UINT32(160, read_u32(frame.data + 4));
    TEST_ASSERT_EQUAL_UINT8(40, frame.data[8]);

    TEST_ASSERT_TRUE(xingzhi_voice_transport_next_frame(&frame).ok);
    TEST_ASSERT_TRUE(frame.complete);
    TEST_ASSERT_EQUAL_UINT8('E', frame.data[0]);
    TEST_ASSERT_EQUAL_UINT16(2, read_u16(frame.data + 2));
    TEST_ASSERT_EQUAL_UINT32(sizeof(audio), read_u32(frame.data + 4));
    TEST_ASSERT_EQUAL(XingzhiVoiceTransportPhase::WaitingAck, xingzhi_voice_transport_status().phase);
}

void test_transport_ack_and_timeout() {
    uint8_t audio[1] = {1};
    XingzhiVoiceFrame frame = {};
    xingzhi_voice_transport_init();
    xingzhi_voice_transport_begin(audio, sizeof(audio), 100);
    xingzhi_voice_transport_next_frame(&frame);
    xingzhi_voice_transport_next_frame(&frame);
    xingzhi_voice_transport_next_frame(&frame);

    TEST_ASSERT_TRUE(xingzhi_voice_transport_handle_control("ack").ok);
    TEST_ASSERT_EQUAL(XingzhiVoiceTransportPhase::Done, xingzhi_voice_transport_status().phase);

    xingzhi_voice_transport_begin(audio, sizeof(audio), 100);
    xingzhi_voice_transport_next_frame(&frame);
    xingzhi_voice_transport_next_frame(&frame);
    xingzhi_voice_transport_next_frame(&frame);
    TEST_ASSERT_TRUE(xingzhi_voice_transport_tick(100).ok);
    XingzhiVoiceTransportResult result = xingzhi_voice_transport_tick(
        100 + XINGZHI_VOICE_TRANSPORT_ACK_TIMEOUT_MS
    );
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("ack_timeout", result.message);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_transport_rejects_empty_audio);
    RUN_TEST(test_transport_emits_metadata_chunks_and_complete);
    RUN_TEST(test_transport_ack_and_timeout);
    return UNITY_END();
}
