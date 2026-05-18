#pragma once

#include <stddef.h>
#include <stdint.h>

namespace xingzhi_audio {

constexpr uint32_t kInputSampleRate = 16000;
constexpr uint8_t kInputChannels = 1;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint32_t kMaxRecordingMs = 10000;
constexpr size_t kWavHeaderBytes = 44;
constexpr size_t kMaxSamples = (kInputSampleRate * kMaxRecordingMs) / 1000;
constexpr size_t kMaxPcmBytes = kMaxSamples * sizeof(int16_t);
constexpr size_t kMaxWavBytes = kWavHeaderBytes + kMaxPcmBytes;

constexpr int kMicWsPin = 4;
constexpr int kMicSckPin = 5;
constexpr int kMicDinPin = 6;

constexpr int kSpeakerDoutPin = 7;
constexpr int kSpeakerBclkPin = 15;
constexpr int kSpeakerLrckPin = 16;
constexpr int kSpeakerCtrlPin = 17;

}  // namespace xingzhi_audio
