#pragma once

#include "export.h"
#include "types.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace muninn {

/**
 * @brief Audio file information (from Heimdall)
 */
struct MUNINN_API AudioInfo {
    float duration;              // Duration in seconds
    int sample_rate;             // Native sample rate
    int num_tracks;              // Number of audio tracks/streams
    std::vector<int> channels;   // Channels per track
};

/**
 * @brief Progress callback for GUI integration
 *
 * @param track_index Current track being processed (0-based)
 * @param total_tracks Total number of tracks
 * @param progress Progress within current track (0.0-1.0)
 * @param message Status message for display
 * @return false to cancel transcription, true to continue
 */
using ProgressCallback = std::function<bool(int track_index, int total_tracks, float progress, const std::string& message)>;

/**
 * @brief High-level Whisper transcription API
 *
 * Muninn Transcriber provides a production-ready C++ implementation of
 * faster-whisper's transcribe() functionality. Features include:
 *
 * - Voice Activity Detection (VAD) for automatic silence skipping
 * - Sliding window processing for long audio files
 * - Comprehensive hallucination filtering
 * - Automatic prompt management
 * - Language detection
 *
 * Example:
 * @code
 *   muninn::Transcriber transcriber("models/whisper-large-v3-turbo");
 *   auto result = transcriber.transcribe("audio.mp3");
 *   for (const auto& segment : result.segments) {
 *       std::cout << segment.text << std::endl;
 *   }
 * @endcode
 */
class MUNINN_API Transcriber {
public:
    /**
     * @brief Initialize Whisper transcriber with ModelOptions
     *
     * Recommended constructor for full control over model initialization.
     *
     * @param options Model configuration including device, compute type, threading
     * @throws std::runtime_error if model cannot be loaded
     */
    explicit Transcriber(const ModelOptions& options);

    /**
     * @brief Initialize Whisper transcriber (convenience overload)
     *
     * @param model_path Path to CTranslate2-converted Whisper model
     * @param device Device to use: "cuda", "cpu", "auto"
     * @param compute_type Precision: "float16", "int8", "float32"
     *
     * @throws std::runtime_error if model cannot be loaded
     */
    Transcriber(const std::string& model_path,
                const std::string& device = "cuda",
                const std::string& compute_type = "float16");

    /**
     * @brief Destructor
     */
    ~Transcriber();

    // Move-only (no copying)
    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;
    Transcriber(Transcriber&&) noexcept;
    Transcriber& operator=(Transcriber&&) noexcept;

    /**
     * @brief Get audio file information without transcribing
     *
     * Fast metadata query - useful for GUI to show track count before processing
     *
     * @param audio_path Path to audio/video file
     * @return Audio metadata including duration and track count
     */
    static AudioInfo get_audio_info(const std::string& audio_path);

    /**
     * @brief Transcribe audio from file
     *
     * Supports formats: MP3, WAV, M4A, FLAC, MP4, MOV, etc.
     * Audio is automatically converted to 16kHz mono.
     *
     * @param audio_path Path to audio/video file
     * @param options Transcription configuration
     * @param progress_callback Optional callback for progress updates (GUI integration)
     * @return Transcription result with segments and metadata
     *
     * @throws std::runtime_error if file cannot be read or transcription fails
     */
    TranscribeResult transcribe(
        const std::string& audio_path,
        const TranscribeOptions& options = {},
        ProgressCallback progress_callback = nullptr
    );

    /**
     * @brief Transcribe audio from memory (single track)
     *
     * @param audio_samples Audio samples (mono, float32, normalized to [-1, 1])
     * @param sample_rate Sample rate (will be resampled to 16kHz if needed)
     * @param options Transcription configuration
     * @param track_id Track identifier for multi-track results (default 0)
     * @return Transcription result with segments and metadata
     *
     * @throws std::runtime_error if transcription fails
     */
    TranscribeResult transcribe(
        const std::vector<float>& audio_samples,
        int sample_rate = 16000,
        const TranscribeOptions& options = {},
        int track_id = 0
    );

    /**
     * @brief Get model information
     *
     * @return Model metadata (language support, mel bins, etc.)
     */
    struct ModelInfo {
        bool is_multilingual;
        int n_mels;
        int num_languages;
        std::string model_type;  // "tiny", "base", "small", etc.
    };
    ModelInfo get_model_info() const;

private:
    class Impl;  // Forward declaration for pimpl idiom
    std::unique_ptr<Impl> pimpl_;
};

} // namespace muninn
