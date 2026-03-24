#pragma once

#include "vad.h"
#include <memory>
#include <string>

namespace muninn {

/**
 * @brief TEN VAD options
 *
 * TEN VAD is a lightweight neural VAD model with:
 * - Small model size (~308KB vs 2.3MB for Silero)
 * - No delay issues (simpler architecture)
 * - Fast inference
 */
struct TenVADOptions {
    std::string model_path;                 // Path to ten-vad.onnx model
    float threshold = 0.5f;                 // Speech probability threshold (0.0-1.0)
    int min_speech_duration_ms = 250;       // Minimum speech duration to keep
    int min_silence_duration_ms = 100;      // Minimum silence duration for split
    int speech_pad_ms = 300;                // Padding around speech segments (300ms to capture gradual onset)
    int max_speech_duration_s = 30;         // Force split long segments (for Whisper)
    int audio_preamble_ms = 2000;           // Always include first N ms of audio (handles VAD warm-up)

    // GPU acceleration
    bool use_gpu = false;                   // Use CUDA (default: false - CPU is fast enough)
    int gpu_device_id = 0;                  // CUDA device ID

    // Internal parameters
    int window_size_samples = 512;          // Window size in samples (32ms at 16kHz)
    int sample_rate = 16000;                // Target sample rate (16kHz)
};

/**
 * @brief TEN VAD - Lightweight Neural Voice Activity Detection
 *
 * TEN VAD is a lightweight alternative to Silero VAD with:
 * - ~10x smaller model size (308KB vs 2.3MB)
 * - Simpler architecture with no delay issues
 * - Comparable accuracy for speech detection
 *
 * Model source: https://github.com/rhasspy/wyoming-satellite
 *
 * Requirements:
 * - ONNX Runtime library
 * - ten-vad.onnx model file (~308KB)
 */
class TenVAD {
public:
    /**
     * @brief Initialize TEN VAD with model
     * @param options Configuration including model path
     * @throws std::runtime_error if model cannot be loaded
     */
    explicit TenVAD(const TenVADOptions& options);
    ~TenVAD();

    // Non-copyable, movable
    TenVAD(const TenVAD&) = delete;
    TenVAD& operator=(const TenVAD&) = delete;
    TenVAD(TenVAD&&) noexcept;
    TenVAD& operator=(TenVAD&&) noexcept;

    /**
     * @brief Check if model is loaded and ready
     */
    bool is_ready() const;

    /**
     * @brief Detect speech segments in audio
     *
     * @param samples Audio samples (mono, float32, normalized [-1, 1])
     * @param sample_rate Sample rate (should be 16000)
     * @return Vector of speech segments with start/end times
     */
    std::vector<SpeechSegment> detect_speech(
        const std::vector<float>& samples,
        int sample_rate = 16000
    );

    /**
     * @brief Filter audio to only speech portions
     *
     * @param samples Input audio samples
     * @param sample_rate Sample rate
     * @param segments Output: detected speech segments
     * @return Filtered audio containing only speech (empty if silent track)
     */
    std::vector<float> filter_silence(
        const std::vector<float>& samples,
        int sample_rate,
        std::vector<SpeechSegment>& segments
    );

    /**
     * @brief Get duration of silence removed
     */
    float get_silence_removed() const { return silence_removed_; }

    /**
     * @brief Reset internal state (call between different audio files)
     */
    void reset_state();

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    float silence_removed_ = 0.0f;
};

/**
 * @brief Check if ONNX Runtime is available for TEN VAD
 * @return true if ONNX Runtime is linked and available
 */
bool is_ten_vad_available();

} // namespace muninn
