#pragma once

#include <stddef.h>
#include <stdint.h>

enum class XingzhiVoiceAudioPhase {
    Uninitialized,
    Ready,
    Recording,
    WavReady,
    Error,
};

struct XingzhiVoiceAudioStatus {
    XingzhiVoiceAudioPhase phase = XingzhiVoiceAudioPhase::Uninitialized;
    const char *detail = "";
    const char *error = "";
    bool buffer_ready = false;
    bool i2s_ready = false;
    bool wav_ready = false;
    size_t capacity_bytes = 0;
    size_t wav_bytes = 0;
    uint32_t sample_rate = 0;
    uint32_t duration_ms = 0;
    uint32_t samples = 0;
    uint32_t timeouts = 0;
    int peak = 0;
    int rms = 0;
};

struct XingzhiVoiceAudioResult {
    bool ok = false;
    bool changed = false;
    const char *message = "";
};

void xingzhi_voice_audio_init();
XingzhiVoiceAudioResult xingzhi_voice_audio_start();
XingzhiVoiceAudioResult xingzhi_voice_audio_tick();
XingzhiVoiceAudioResult xingzhi_voice_audio_stop();
void xingzhi_voice_audio_abort(const char *reason);
XingzhiVoiceAudioStatus xingzhi_voice_audio_status();
const char *xingzhi_voice_audio_phase_name(XingzhiVoiceAudioPhase phase);
const uint8_t *xingzhi_voice_audio_wav_data();
size_t xingzhi_voice_audio_wav_bytes();
