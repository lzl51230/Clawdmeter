#include "xingzhi_wav.h"

#include <limits.h>
#include <string.h>

namespace {

constexpr size_t WAV_HEADER_BYTES = 44;

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

}  // namespace

size_t xingzhi_wav_total_bytes(size_t pcm_bytes) {
    if (pcm_bytes > SIZE_MAX - WAV_HEADER_BYTES) {
        return 0;
    }
    return WAV_HEADER_BYTES + pcm_bytes;
}

bool xingzhi_wav_write_header(
    uint8_t *dest,
    size_t dest_len,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    size_t pcm_bytes
) {
    if (!dest || dest_len < WAV_HEADER_BYTES || channels == 0 || bits_per_sample == 0) {
        return false;
    }
    if (pcm_bytes > UINT32_MAX - 36U) {
        return false;
    }
    const size_t total_bytes = xingzhi_wav_total_bytes(pcm_bytes);
    if (total_bytes == 0 || dest_len < total_bytes) {
        return false;
    }

    const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8U;
    const uint16_t block_align = static_cast<uint16_t>(channels * bits_per_sample / 8U);

    memcpy(dest + 0, "RIFF", 4);
    write_u32(dest + 4, static_cast<uint32_t>(36U + pcm_bytes));
    memcpy(dest + 8, "WAVE", 4);
    memcpy(dest + 12, "fmt ", 4);
    write_u32(dest + 16, 16);
    write_u16(dest + 20, 1);
    write_u16(dest + 22, channels);
    write_u32(dest + 24, sample_rate);
    write_u32(dest + 28, byte_rate);
    write_u16(dest + 32, block_align);
    write_u16(dest + 34, bits_per_sample);
    memcpy(dest + 36, "data", 4);
    write_u32(dest + 40, static_cast<uint32_t>(pcm_bytes));
    return true;
}
