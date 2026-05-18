#include "xingzhi_voice_audio.h"

#include "xingzhi_audio_cfg.h"
#include "xingzhi_wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <driver/i2s_std.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#endif

namespace {

using namespace xingzhi_audio;

constexpr size_t READ_SAMPLES = 256;
constexpr uint32_t READ_TIMEOUT_MS = 20;
constexpr uint32_t MAX_CONSECUTIVE_TIMEOUTS = 50;

XingzhiVoiceAudioPhase phase = XingzhiVoiceAudioPhase::Uninitialized;
char detail[48] = "uninitialized";
char error[48] = "";
uint8_t *wav_buffer = nullptr;
size_t wav_bytes = 0;
uint32_t sample_count = 0;
uint32_t timeout_count = 0;
uint32_t consecutive_timeouts = 0;
int peak_value = 0;
uint64_t sum_squares = 0;
bool buffer_ready = false;
bool i2s_ready = false;
bool rx_enabled = false;

#ifdef ARDUINO
i2s_chan_handle_t rx_handle = nullptr;
#endif

void set_detail(const char *value) {
    snprintf(detail, sizeof(detail), "%s", value ? value : "");
}

void set_error(const char *value) {
    snprintf(error, sizeof(error), "%s", value ? value : "audio_error");
    set_detail(error);
    phase = XingzhiVoiceAudioPhase::Error;
}

int16_t clamp_i16(int32_t value) {
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(value);
}

int abs_i16(int16_t value) {
    if (value == INT16_MIN) {
        return INT16_MAX;
    }
    return value < 0 ? -value : value;
}

int current_rms() {
    if (sample_count == 0) {
        return 0;
    }
    return static_cast<int>(sqrt(static_cast<double>(sum_squares) / static_cast<double>(sample_count)));
}

int16_t *pcm_buffer() {
    if (!wav_buffer) {
        return nullptr;
    }
    return reinterpret_cast<int16_t *>(wav_buffer + kWavHeaderBytes);
}

void reset_capture() {
    wav_bytes = 0;
    sample_count = 0;
    timeout_count = 0;
    consecutive_timeouts = 0;
    peak_value = 0;
    sum_squares = 0;
    error[0] = '\0';
}

void store_sample(int16_t sample) {
    if (sample_count >= kMaxSamples) {
        return;
    }
    pcm_buffer()[sample_count++] = sample;
    const int magnitude = abs_i16(sample);
    if (magnitude > peak_value) {
        peak_value = magnitude;
    }
    sum_squares += static_cast<uint64_t>(magnitude) * static_cast<uint64_t>(magnitude);
}

bool finalize_wav(const char *message) {
    if (sample_count == 0) {
        set_error("empty_recording");
        return false;
    }
    const size_t pcm_bytes = static_cast<size_t>(sample_count) * sizeof(int16_t);
    wav_bytes = xingzhi_wav_total_bytes(pcm_bytes);
    if (!xingzhi_wav_write_header(wav_buffer, kMaxWavBytes, kInputSampleRate, kInputChannels, kBitsPerSample, pcm_bytes)) {
        set_error("wav_header_failed");
        return false;
    }
    phase = XingzhiVoiceAudioPhase::WavReady;
    set_detail(message);
    return true;
}

#ifdef ARDUINO
bool ensure_i2s() {
    if (i2s_ready) {
        return true;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.auto_clear_before_cb = false;
    chan_cfg.intr_priority = 0;
    esp_err_t err = i2s_new_channel(&chan_cfg, nullptr, &rx_handle);
    if (err != ESP_OK) {
        set_error("i2s_new_channel_failed");
        return false;
    }

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg.sample_rate_hz = kInputSampleRate;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
#ifdef I2S_HW_VERSION_2
    std_cfg.clk_cfg.ext_clk_freq_hz = 0;
#endif
    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.ws_pol = false;
    std_cfg.slot_cfg.bit_shift = true;
#ifdef I2S_HW_VERSION_2
    std_cfg.slot_cfg.left_align = true;
    std_cfg.slot_cfg.big_endian = false;
    std_cfg.slot_cfg.bit_order_lsb = false;
#endif
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(kMicSckPin);
    std_cfg.gpio_cfg.ws = static_cast<gpio_num_t>(kMicWsPin);
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = static_cast<gpio_num_t>(kMicDinPin);
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;

    err = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (err != ESP_OK) {
        set_error("i2s_init_failed");
        return false;
    }
    i2s_ready = true;
    return true;
}

void disable_rx() {
    if (rx_handle && rx_enabled) {
        i2s_channel_disable(rx_handle);
        rx_enabled = false;
    }
}
#else
bool ensure_i2s() {
    i2s_ready = false;
    set_error("i2s_unavailable");
    return false;
}

void disable_rx() {}
#endif

}  // namespace

void xingzhi_voice_audio_init() {
    if (!wav_buffer) {
#ifdef ARDUINO
        wav_buffer = static_cast<uint8_t *>(heap_caps_malloc(kMaxWavBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!wav_buffer) {
            wav_buffer = static_cast<uint8_t *>(heap_caps_malloc(kMaxWavBytes, MALLOC_CAP_8BIT));
        }
#else
        wav_buffer = static_cast<uint8_t *>(malloc(kMaxWavBytes));
#endif
    }
    buffer_ready = wav_buffer != nullptr;
    reset_capture();
    phase = buffer_ready ? XingzhiVoiceAudioPhase::Ready : XingzhiVoiceAudioPhase::Error;
    set_detail(buffer_ready ? "ready" : "buffer_unavailable");
    if (!buffer_ready) {
        snprintf(error, sizeof(error), "buffer_unavailable");
    }
}

XingzhiVoiceAudioResult xingzhi_voice_audio_start() {
    if (!buffer_ready) {
        set_error("buffer_unavailable");
        return {false, true, "buffer_unavailable"};
    }
    if (!ensure_i2s()) {
        return {false, true, error};
    }
    disable_rx();
    reset_capture();

#ifdef ARDUINO
    esp_err_t err = i2s_channel_enable(rx_handle);
    if (err != ESP_OK) {
        set_error("i2s_enable_failed");
        return {false, true, "i2s_enable_failed"};
    }
    rx_enabled = true;
#endif

    phase = XingzhiVoiceAudioPhase::Recording;
    set_detail("recording");
    return {true, true, "recording"};
}

XingzhiVoiceAudioResult xingzhi_voice_audio_tick() {
    if (phase != XingzhiVoiceAudioPhase::Recording) {
        return {true, false, "unchanged"};
    }
    if (sample_count >= kMaxSamples) {
        disable_rx();
        const bool ok = finalize_wav("capped");
        return {ok, true, ok ? "capped" : error};
    }

#ifdef ARDUINO
    int32_t raw[READ_SAMPLES] = {};
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_handle, raw, sizeof(raw), &bytes_read, READ_TIMEOUT_MS);
    if (err != ESP_OK) {
        timeout_count += 1;
        consecutive_timeouts += 1;
        if (consecutive_timeouts > MAX_CONSECUTIVE_TIMEOUTS) {
            disable_rx();
            set_error("i2s_read_timeout");
            return {false, true, "i2s_read_timeout"};
        }
        return {true, false, "timeout"};
    }

    consecutive_timeouts = 0;
    const size_t raw_samples = bytes_read / sizeof(int32_t);
    for (size_t index = 0; index < raw_samples && sample_count < kMaxSamples; ++index) {
        store_sample(clamp_i16(raw[index] >> 12));
    }
    if (sample_count >= kMaxSamples) {
        disable_rx();
        const bool ok = finalize_wav("capped");
        return {ok, true, ok ? "capped" : error};
    }
    return {true, raw_samples > 0, raw_samples > 0 ? "sampled" : "empty"};
#else
    set_error("i2s_unavailable");
    return {false, true, "i2s_unavailable"};
#endif
}

XingzhiVoiceAudioResult xingzhi_voice_audio_stop() {
    if (phase == XingzhiVoiceAudioPhase::WavReady) {
        return {true, false, "wav_ready"};
    }
    if (phase != XingzhiVoiceAudioPhase::Recording) {
        return {false, false, "not_recording"};
    }
    disable_rx();
    const bool ok = finalize_wav("wav_ready");
    return {ok, true, ok ? "wav_ready" : error};
}

void xingzhi_voice_audio_abort(const char *reason) {
    disable_rx();
    set_error(reason ? reason : "aborted");
}

XingzhiVoiceAudioStatus xingzhi_voice_audio_status() {
    XingzhiVoiceAudioStatus status = {};
    status.phase = phase;
    status.detail = detail;
    status.error = error;
    status.buffer_ready = buffer_ready;
    status.i2s_ready = i2s_ready;
    status.wav_ready = phase == XingzhiVoiceAudioPhase::WavReady;
    status.capacity_bytes = buffer_ready ? kMaxWavBytes : 0;
    status.wav_bytes = wav_bytes;
    status.sample_rate = kInputSampleRate;
    status.samples = sample_count;
    status.duration_ms = static_cast<uint32_t>((static_cast<uint64_t>(sample_count) * 1000ULL) / kInputSampleRate);
    status.timeouts = timeout_count;
    status.peak = peak_value;
    status.rms = current_rms();
    return status;
}

const char *xingzhi_voice_audio_phase_name(XingzhiVoiceAudioPhase value) {
    switch (value) {
    case XingzhiVoiceAudioPhase::Ready:
        return "ready";
    case XingzhiVoiceAudioPhase::Recording:
        return "recording";
    case XingzhiVoiceAudioPhase::WavReady:
        return "wav_ready";
    case XingzhiVoiceAudioPhase::Error:
        return "error";
    case XingzhiVoiceAudioPhase::Uninitialized:
    default:
        return "uninitialized";
    }
}

const uint8_t *xingzhi_voice_audio_wav_data() {
    return phase == XingzhiVoiceAudioPhase::WavReady ? wav_buffer : nullptr;
}

size_t xingzhi_voice_audio_wav_bytes() {
    return phase == XingzhiVoiceAudioPhase::WavReady ? wav_bytes : 0;
}
