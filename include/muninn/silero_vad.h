#pragma once

#include "vad.h"
#include <memory>
#include <string>

namespace muninn {

/**
 * @brief Silero VAD options
 */
struct SileroVADOptions {
    std::string model_path;                 // Path to silero_vad.onnx model
    float threshold = 0.5f;                 // Speech probability threshold (0.0-1.0)
    int min_speech_duration_ms = 250;       // Minimum speech duration to keep
    int min_silence_duration_ms = 100;      // Minimum silence duration for split
    int speech_pad_ms = 30;                 // Padding around speech segments
    int max_speech_duration_s = 30;         // Force split long segments (for Whisper)

    // GPU acceleration
    // NOTE: CPU is typically FASTER for Silero VAD due to small inference windows (512 samples).
    // GPU has memory transfer overhead that dominates for such tiny batch sizes.
    // Only enable GPU if you have very specific performance requirements.
    bool use_gpu = false;                   // Use CUDA (default: false - CPU is faster for VAD)
    int gpu_device_id = 0;                  // CUDA device ID

    // Internal parameters (usually don't need to change)
    int window_size_samples = 512;          // 32ms at 16kHz
    int sample_rate = 16000;                // Only 8kHz or 16kHz supported
};

/**
 * @brief Silero VAD - Neural Voice Activity Detection
 *
 * Uses Silero VAD ONNX model for accurate speech detection.
 * More accurate than energy-based VAD, especially for:
 * - Noisy environments
 * - Music/sound effects (won't trigger on non-speech)
 * - Low volume speech
 *
 * Requirements:
 * - ONNX Runtime library
 * - silero_vad.onnx model file (~2MB)
 *
 * Model download:
 * https://github.com/snakers4/silero-vad/raw/master/files/silero_vad.onnx
 */
class SileroVAD {
public:
    /**
     * @brief Initialize Silero VAD with model
     * @param options Configuration including model path
     * @throws std::runtime_error if model cannot be loaded
     */
    explicit SileroVAD(const SileroVADOptions& options);
    ~SileroVAD();

    // Non-copyable, movable
    SileroVAD(const SileroVAD&) = delete;
    SileroVAD& operator=(const SileroVAD&) = delete;
    SileroVAD(SileroVAD&&) noexcept;
    SileroVAD& operator=(SileroVAD&&) noexcept;

    /**
     * @brief Check if model is loaded and ready
     */
    bool is_ready() const;

    /**
     * @brief Detect speech segments in audio
     *
     * @param samples Audio samples (mono, float32, normalized [-1, 1])
     * @param sample_rate Sample rate (8000 or 16000)
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
 * @brief Check if ONNX Runtime is available for Silero VAD
 * @return true if ONNX Runtime is linked and available
 */
bool is_silero_vad_available();

} // namespace muninn
