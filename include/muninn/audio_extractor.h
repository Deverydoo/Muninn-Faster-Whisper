#pragma once

#include "muninn/export.h"
#include <string>
#include <vector>
#include <memory>

namespace muninn {

/**
 * @brief AudioExtractor - Internal Audio Extraction
 *
 * Extracts audio from video files and prepares it for Whisper transcription.
 * Uses internal audio decoder (no external dependencies).
 * Configured for Whisper requirements:
 * - 16kHz sample rate
 * - Mono channel
 * - Float32 samples normalized to [-1, 1]
 */
class MUNINN_API AudioExtractor {
public:
    AudioExtractor();
    ~AudioExtractor();

    /**
     * @brief Open a file for multi-track extraction
     * @param file_path Path to audio/video file
     * @return True if successful
     */
    bool open(const std::string& file_path);

    /**
     * @brief Close the currently open file
     */
    void close();

    /**
     * @brief Get number of audio tracks in file
     * @return Number of audio tracks (0 if no file open)
     */
    int get_track_count() const;

    /**
     * @brief Get duration in seconds
     */
    float get_duration() const;

    /**
     * @brief Extract audio from a specific track
     * @param track_index Track index (0-based)
     * @param samples Output buffer for float32 samples
     * @return True if successful
     */
    bool extract_track(int track_index, std::vector<float>& samples);

    /**
     * @brief Extract audio from video/audio file (convenience method - uses track 0)
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
