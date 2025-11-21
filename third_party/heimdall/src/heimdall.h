#pragma once

/**
 * Heimdall - The Vigilant Guardian of Audio Waveforms
 *
 * Named after the Norse god Heimdallr, who possesses incredibly acute hearing
 * and guards the rainbow bridge Bifröst. Just as Heimdall can hear grass growing
 * and wool on sheep, this module provides ultra-fast, crystal-clear audio
 * waveform visualization.
 *
 * Features:
 * - SIMD-optimized peak detection (AVX2/SSE)
 * - Multi-track audio extraction
 * - Hardware-accelerated decoding via FFmpeg
 * - Sub-second waveform generation
 *
 * Copyright (c) 2025 Loki Studio / NordIQ AI
 */

#include <string>
#include <vector>
#include <map>
#include <memory>

// DLL Export/Import macros for C++ consumers
#ifdef _WIN32
    #ifdef HEIMDALL_EXPORTS
        #define HEIMDALL_API __declspec(dllexport)
    #else
        #define HEIMDALL_API __declspec(dllimport)
    #endif
#else
    #define HEIMDALL_API
#endif

// Forward declarations (internal only, not exposed to DLL consumers)
#ifdef HEIMDALL_EXPORTS
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libswresample/swresample.h>
}
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
 * Heimdall - Fast waveform generator
 *
 * The vigilant guardian watches over your audio streams, providing
 * instant waveform visualization with incredible accuracy.
 */
class HEIMDALL_API Heimdall {
public:
    Heimdall();
    ~Heimdall();

    /**
     * Generate waveform peaks for visualization
     *
     * Like Heimdall's acute hearing detecting the faintest sounds,
     * this function extracts precise audio peaks for waveform display.
     *
     * @param audio_file Path to audio/video file
     * @param stream_index Audio stream index (0 for first stream)
     * @param width Output width in pixels
     * @param height Output height in pixels
     * @param samples_per_pixel Downsampling factor (higher = faster but less detail)
     * @param normalize Auto-normalize peaks to 0.0-1.0 range
     * @return Vector of peak values (width * 2 for min/max pairs)
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
     *
     * As Heimdall guards multiple realms, this function efficiently
     * processes multiple audio streams simultaneously.
     *
     * @param audio_file Path to audio/video file
     * @param stream_indices List of stream indices to process
     * @param width Output width in pixels
     * @param height Output height in pixels
     * @param target_sample_rate Target sample rate for resampling (8000, 16000, 44100, 48000)
     * @param packet_quality Percentage of packets to decode (10=10%, 100=all packets, default 10)
     * @return Map of stream_index -> peak values
     */
    std::map<int, std::vector<float>> generate_batch(
        const std::string& audio_file,
        const std::vector<int>& stream_indices,
        int width,
        int height,
        int target_sample_rate = 48000,
        int packet_quality = 10
    );

    /**
     * Get audio file information (fast metadata query)
     *
     * Heimdall's keen senses detect audio properties instantly.
     *
     * @param audio_file Path to audio/video file
     * @return Audio metadata
     */
    AudioInfo get_audio_info(const std::string& audio_file);

    /**
     * Extract audio from streams (core extraction method)
     *
     * Heimdall's acute hearing extracts every sound with perfect fidelity.
     * Each audio track is preserved separately - no mixing or merging.
     * Output is mono float32 samples ready for CTranslate2/Whisper or other consumers.
     *
     * @param audio_file Path to audio/video file
     * @param sample_rate Target sample rate (16000 for Whisper, up to 48000 for detailed waveforms)
     * @param stream_indices Specific streams to extract (empty = all streams)
     * @param quality Decode quality 1-100 (100=full for transcription, 10=fast for waveforms)
     * @return Map of stream_index -> mono float32 samples at requested sample rate
     */
    std::map<int, std::vector<float>> extract_audio(
        const std::string& audio_file,
        int sample_rate = 16000,
        const std::vector<int>& stream_indices = {},
        int quality = 100
    );

private:
    // Forward declarations need full definitions for unique_ptr
    // Include them from separate headers
    std::unique_ptr<class AudioDecoder> decoder_;
    std::unique_ptr<class PeakDetector> peak_detector_;
};

} // namespace heimdall
