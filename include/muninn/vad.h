#pragma once

#include "types.h"
#include <vector>
#include <cstdint>

namespace muninn {

/**
 * @brief Speech segment with start/end times
 */
struct SpeechSegment {
    float start;    // Start time in seconds
    float end;      // End time in seconds

    SpeechSegment(float s = 0.0f, float e = 0.0f) : start(s), end(e) {}
};

/**
 * @brief Voice Activity Detection options
 */
struct VADOptions {
    float threshold = 0.02f;              // RMS energy threshold (0.0-1.0)
    int min_speech_duration_ms = 250;     // Minimum speech duration to keep
    int min_silence_duration_ms = 500;    // Minimum silence to split segments
    int speech_pad_ms = 100;              // Padding around speech segments
    bool adaptive_threshold = true;       // Auto-adjust threshold based on noise floor
    float noise_floor_percentile = 0.1f;  // Percentile for noise floor estimation
};

/**
 * @brief Energy-based Voice Activity Detector
 *
 * Detects speech segments in audio using RMS energy analysis.
 * Optimized for clear speech/silence distinction (podcasts, gaming commentary).
 *
 * For noisy environments, consider upgrading to Silero VAD (ONNX).
 */
class VAD {
public:
    VAD(const VADOptions& options = {});
    ~VAD() = default;

    /**
     * @brief Detect speech segments in audio
     *
     * @param samples Audio samples (mono, float32, normalized [-1, 1])
     * @param sample_rate Sample rate (typically 16000)
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
     * @return Filtered audio containing only speech
     */
    std::vector<float> filter_silence(
        const std::vector<float>& samples,
        int sample_rate,
        std::vector<SpeechSegment>& segments
    );

    /**
     * @brief Get duration of silence removed
     * @return Seconds of silence removed in last filter_silence() call
     */
    float get_silence_removed() const { return silence_removed_; }

private:
    VADOptions options_;
    float silence_removed_ = 0.0f;

    // Calculate RMS energy for a window
    float calculate_rms(const float* samples, int count);

    // Estimate noise floor from energy histogram
    float estimate_noise_floor(const std::vector<float>& energies);

    // Merge close segments and filter short ones
    std::vector<SpeechSegment> post_process_segments(
        const std::vector<SpeechSegment>& segments,
        int sample_rate
    );
};

/**
 * @brief Audio characteristics for VAD selection
 */
struct AudioCharacteristics {
    float noise_floor;      // 10th percentile of absolute amplitude
    float speech_level;     // 90th percentile of absolute amplitude
    float dynamic_range;    // Difference between speech and noise
    float max_amplitude;    // Maximum amplitude
    bool is_silent;         // True if max amplitude is near zero
};

/**
 * @brief Analyze audio characteristics for VAD selection
 *
 * @param samples Audio samples (mono, float32, normalized [-1, 1])
 * @return Audio characteristics
 */
AudioCharacteristics analyze_audio_characteristics(const std::vector<float>& samples);

/**
 * @brief Auto-detect best VAD type for audio
 *
 * Smart VAD selection heuristics (tested on gaming/streaming content):
 * 1. Multi-track Track 0 → Energy VAD (desktop/game audio with music)
 * 2. Very clean speech (noise floor < 0.0001) → Silero VAD (noise gates, studio mics)
 * 3. Clean speech (noise floor < 0.01, dynamic range > 0.15) → Silero VAD
 * 4. Mixed/noisy content → Energy VAD (robust fallback)
 *
 * @param samples Audio samples to analyze
 * @param track_id Track index (0 = first track)
 * @param total_tracks Total number of tracks in file
 * @return Recommended VAD type (Energy or Silero, never Auto/None)
 */
VADType auto_detect_vad_type(
    const std::vector<float>& samples,
    int track_id,
    int total_tracks
);

} // namespace muninn
