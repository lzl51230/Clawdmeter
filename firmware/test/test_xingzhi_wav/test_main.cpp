#include <unity.h>

#include "xingzhi_wav.h"

namespace {

uint16_t read_u16(const uint8_t *data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t read_u32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

}  // namespace

void test_wav_header_matches_16khz_mono_pcm() {
    uint8_t buffer[44 + 320] = {};

    TEST_ASSERT_TRUE(xingzhi_wav_write_header(buffer, sizeof(buffer), 16000, 1, 16, 320));
    TEST_ASSERT_EQUAL_MEMORY("RIFF", buffer + 0, 4);
    TEST_ASSERT_EQUAL_UINT32(36U + 320U, read_u32(buffer + 4));
    TEST_ASSERT_EQUAL_MEMORY("WAVE", buffer + 8, 4);
    TEST_ASSERT_EQUAL_MEMORY("fmt ", buffer + 12, 4);
    TEST_ASSERT_EQUAL_UINT32(16U, read_u32(buffer + 16));
    TEST_ASSERT_EQUAL_UINT16(1U, read_u16(buffer + 20));
    TEST_ASSERT_EQUAL_UINT16(1U, read_u16(buffer + 22));
    TEST_ASSERT_EQUAL_UINT32(16000U, read_u32(buffer + 24));
    TEST_ASSERT_EQUAL_UINT32(32000U, read_u32(buffer + 28));
    TEST_ASSERT_EQUAL_UINT16(2U, read_u16(buffer + 32));
    TEST_ASSERT_EQUAL_UINT16(16U, read_u16(buffer + 34));
    TEST_ASSERT_EQUAL_MEMORY("data", buffer + 36, 4);
    TEST_ASSERT_EQUAL_UINT32(320U, read_u32(buffer + 40));
}

void test_wav_total_bytes_includes_header() {
    TEST_ASSERT_EQUAL_UINT32(44U, xingzhi_wav_total_bytes(0));
    TEST_ASSERT_EQUAL_UINT32(364U, xingzhi_wav_total_bytes(320));
}

void test_wav_header_rejects_small_destination() {
    uint8_t buffer[43] = {};
    TEST_ASSERT_FALSE(xingzhi_wav_write_header(buffer, sizeof(buffer), 16000, 1, 16, 0));
}

void test_wav_header_rejects_destination_without_pcm_room() {
    uint8_t buffer[44] = {};
    TEST_ASSERT_FALSE(xingzhi_wav_write_header(buffer, sizeof(buffer), 16000, 1, 16, 2));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_wav_header_matches_16khz_mono_pcm);
    RUN_TEST(test_wav_total_bytes_includes_header);
    RUN_TEST(test_wav_header_rejects_small_destination);
    RUN_TEST(test_wav_header_rejects_destination_without_pcm_room);
    return UNITY_END();
}
