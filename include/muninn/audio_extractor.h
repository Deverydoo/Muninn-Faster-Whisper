#pragma once

#include <string>
#include <vector>

namespace muninn {

/**
 * @brief AudioExtractor - Wrapper around Heimdall's AudioDecoder
 *
 * Extracts audio from video files and prepares it for Whisper transcription.
 * Configured for Whisper requirements:
 * - 16kHz sample rate
 * - Mono channel
 * - Float32 samples normalized to [-1, 1]
 */
class AudioExtractor {
public:
    AudioExtractor();
    ~AudioExtractor();

    /**
     * @brief Extract audio from video/audio file
     *
     * Automatically handles:
     * - Multi-format support (MP3, WAV, M4A, MP4, MOV, etc.)
     * - Resampling to 16kHz
     * - Stereo to mono conversion
     * - Sample normalization
     *
     * @param file_path Path to audio/video file
     * @param samples Output buffer for float32 samples
     * @param duration Output duration in seconds
     * @return True if successful
     */
    bool extract_audio(const std::string& file_path,
                      std::vector<float>& samples,
                      float& duration);

    /**
     * @brief Get last error message
     */
    std::string get_last_error() const { return last_error_; }

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    std::string last_error_;
};

} // namespace muninn
