#pragma once

#include <stddef.h>
#include <stdint.h>

size_t xingzhi_wav_total_bytes(size_t pcm_bytes);
bool xingzhi_wav_write_header(
    uint8_t *dest,
    size_t dest_len,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    size_t pcm_bytes
);
