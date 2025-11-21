#pragma once

#include "types.h"
#include <memory>
#include <string>
#include <vector>

namespace muninn {

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
class Transcriber {
public:
    /**
     * @brief Initialize Whisper transcriber
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
     * @brief Transcribe audio from file
     *
     * Supports formats: MP3, WAV, M4A, FLAC, MP4, MOV, etc.
     * Audio is automatically converted to 16kHz mono.
     *
     * @param audio_path Path to audio/video file
     * @param options Transcription configuration
     * @return Transcription result with segments and metadata
     *
     * @throws std::runtime_error if file cannot be read or transcription fails
     */
    TranscribeResult transcribe(
        const std::string& audio_path,
        const TranscribeOptions& options = {}
    );

    /**
     * @brief Transcribe audio from memory
     *
     * @param audio_samples Audio samples (mono, float32, normalized to [-1, 1])
     * @param sample_rate Sample rate (will be resampled to 16kHz if needed)
     * @param options Transcription configuration
     * @return Transcription result with segments and metadata
     *
     * @throws std::runtime_error if transcription fails
     */
    TranscribeResult transcribe(
        const std::vector<float>& audio_samples,
        int sample_rate = 16000,
        const TranscribeOptions& options = {}
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
