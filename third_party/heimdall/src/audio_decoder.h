#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libswresample/swresample.h>
}

namespace heimdall {

/**
 * AudioDecoder - Heimdall's Acute Hearing
 *
 * Extracts audio from video files with the precision of Heimdall's legendary senses.
 * Can detect and decode multiple audio streams simultaneously.
 */
class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    /**
     * Open audio file and prepare for decoding
     *
     * @param filename Path to audio/video file
     * @param target_sample_rate Target sample rate for resampling (default: 48000)
     * @return True if successful
     */
    bool open(const std::string& filename, int target_sample_rate = 48000);

    /**
     * Get number of audio streams in file
     */
    int get_stream_count() const;

    /**
     * Get duration in milliseconds
     */
    int64_t get_duration_ms() const;

    /**
     * Get sample rate for a stream
     */
    int get_sample_rate(int stream_index) const;

    /**
     * Get channel count for a stream
     */
    int get_channels(int stream_index) const;

    /**
     * Decode audio samples from a specific stream
     *
     * @param stream_index Audio stream to decode
     * @param output Buffer to store decoded float samples
     * @param max_samples Maximum samples to decode
     * @return Number of samples actually decoded
     */
    int decode_samples(int stream_index, std::vector<float>& output, int max_samples = -1);

    /**
     * Extract audio streams (UNIFIED CORE METHOD)
     *
     * Single-pass extraction for all use cases:
     * - Transcription: quality=100 (full decode)
     * - Fast waveforms: quality=10 (10% packets)
     * - Detailed waveforms: quality=100 at higher sample rates
     *
     * Each track is kept separate (no mixing/merging).
     *
     * @param stream_indices Stream indices to extract (empty = all streams)
     * @param outputs Map of stream_index -> sample buffer (mono float32 at target_sample_rate)
     * @param quality Decode quality 1-100 (100=full, 10=10% packets for speed)
     * @return Number of streams successfully extracted
     */
    int extract_streams(
        const std::vector<int>& stream_indices,
        std::map<int, std::vector<float>>& outputs,
        int quality = 100
    );

    /**
     * Get all stream indices
     */
    std::vector<int> get_all_stream_indices() const;

    /**
     * Close and cleanup
     */
    void close();

private:
    struct StreamInfo {
        int stream_index;
        AVCodecContext* codec_ctx;
        SwrContext* swr_ctx;
        int sample_rate;
        int channels;
    };

    AVFormatContext* format_ctx_;
    std::vector<StreamInfo> audio_streams_;
    AVPacket* packet_;
    AVFrame* frame_;
    bool is_open_;
    int target_sample_rate_;  // Target sample rate for resampling

    bool init_stream(int stream_index);
};

} // namespace heimdall
