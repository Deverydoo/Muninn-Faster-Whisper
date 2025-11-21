#pragma once

/**
 * Heimdall 2.0 - The Vigilant Guardian of Audio
 *
 * Public header for DLL consumers.
 * FFmpeg is statically linked inside the DLL - no external dependencies.
 *
 * Copyright (c) 2025 Loki Studio / NordIQ AI
 */

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

#ifdef _WIN32
    #ifdef HEIMDALL_EXPORTS
        #define HEIMDALL_API __declspec(dllexport)
    #else
        #define HEIMDALL_API __declspec(dllimport)
    #endif
#else
    #define HEIMDALL_API
#endif

namespace heimdall {

/**
 * Audio stream information
 */
struct HEIMDALL_API AudioInfo {
    int64_t duration_ms;
    int sample_rate;
    int channels;
    int stream_count;
};

/**
 * Heimdall - Fast audio extraction and waveform generation
 *
 * Main API class. Create an instance and use extract_audio() for transcription
 * or generate_peaks()/generate_batch() for waveform visualization.
 */
class HEIMDALL_API Heimdall {
public:
    Heimdall();
    ~Heimdall();

    // Non-copyable, non-movable (unique_ptr members with forward-declared types)
    Heimdall(const Heimdall&) = delete;
    Heimdall& operator=(const Heimdall&) = delete;
    Heimdall(Heimdall&&) = delete;
    Heimdall& operator=(Heimdall&&) = delete;

    /**
     * Get audio file information (fast metadata query, no decoding)
     *
     * @param audio_file Path to audio/video file
     * @return AudioInfo with duration, sample_rate, channels, stream_count
     */
    AudioInfo get_audio_info(const std::string& audio_file);

    /**
     * Extract audio from streams for transcription
     *
     * Each track is kept separate (no mixing). Output is mono float32 samples
     * resampled to the target sample rate, ready for CTranslate2/Whisper.
     *
     * @param audio_file Path to audio/video file
     * @param sample_rate Target sample rate (16000 for Whisper)
     * @param stream_indices Specific streams to extract (empty = all streams)
     * @param quality Decode quality 1-100 (100=full for transcription, 10=fast for waveforms)
     * @return Map of stream_index -> mono float32 samples
     */
    std::map<int, std::vector<float>> extract_audio(
        const std::string& audio_file,
        int sample_rate = 16000,
        const std::vector<int>& stream_indices = {},
        int quality = 100
    );

    /**
     * Generate waveform peaks for visualization (single stream)
     */
    std::vector<float> generate_peaks(
        const std::string& audio_file,
        int stream_index,
        int width,
        int height,
        int samples_per_pixel = 512,
        bool normalize = true
    );

    /**
     * Generate waveforms for multiple streams in one pass
     */
    std::map<int, std::vector<float>> generate_batch(
        const std::string& audio_file,
        const std::vector<int>& stream_indices,
        int width,
        int height,
        int target_sample_rate = 48000,
        int packet_quality = 10
    );

private:
    // Must match the actual DLL implementation layout
    std::unique_ptr<class AudioDecoder> decoder_;
    std::unique_ptr<class PeakDetector> peak_detector_;
};

} // namespace heimdall
