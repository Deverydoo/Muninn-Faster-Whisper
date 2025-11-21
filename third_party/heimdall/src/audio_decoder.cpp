#include "audio_decoder.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>

extern "C" {
    #include <libavutil/opt.h>
}

namespace heimdall {

// Helper function to get current timestamp in HH:MM:SS.mmm format
static std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt;
    localtime_s(&bt, &timer);

    std::ostringstream oss;
    oss << std::put_time(&bt, "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Macro for timestamped output
#define TS_PRINT(msg) std::cout << "[" << get_timestamp() << "] " << msg << std::endl

AudioDecoder::AudioDecoder()
    : format_ctx_(nullptr)
    , packet_(nullptr)
    , frame_(nullptr)
    , is_open_(false)
    , target_sample_rate_(48000)
{
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
}

AudioDecoder::~AudioDecoder() {
    close();
    if (packet_) av_packet_free(&packet_);
    if (frame_) av_frame_free(&frame_);
}

bool AudioDecoder::open(const std::string& filename, int target_sample_rate) {
    close();

    target_sample_rate_ = target_sample_rate;
    std::ostringstream oss;
    oss << "[Heimdall] Opening with target sample rate: " << target_sample_rate_ << " Hz";
    TS_PRINT(oss.str());

    // Open file
    if (avformat_open_input(&format_ctx_, filename.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[Heimdall] Cannot open file: " << filename << std::endl;
        return false;
    }

    // Get stream info
    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        std::cerr << "[Heimdall] Cannot find stream info" << std::endl;
        avformat_close_input(&format_ctx_);
        return false;
    }

    // Find all audio streams
    for (unsigned int i = 0; i < format_ctx_->nb_streams; i++) {
        if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (init_stream(i)) {
                oss.str("");  // Clear stream for reuse
                oss << "[Heimdall] Found audio stream " << audio_streams_.size() - 1
                    << " (index " << i << "): "
                    << get_sample_rate(audio_streams_.size() - 1) << "Hz, "
                    << get_channels(audio_streams_.size() - 1) << " channels";
                TS_PRINT(oss.str());
            }
        }
    }

    if (audio_streams_.empty()) {
        TS_PRINT("[Heimdall] No audio streams found");
        avformat_close_input(&format_ctx_);
        return false;
    }

    is_open_ = true;
    oss.str("");  // Clear stream for reuse
    oss << "[Heimdall] Guardian ready - watching over " << audio_streams_.size() << " audio streams";
    TS_PRINT(oss.str());
    return true;
}

bool AudioDecoder::init_stream(int stream_index) {
    AVStream* stream = format_ctx_->streams[stream_index];

    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "[Heimdall] Codec not found for stream " << stream_index << std::endl;
        return false;
    }

    // Allocate codec context
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "[Heimdall] Cannot allocate codec context" << std::endl;
        return false;
    }

    // Copy codec parameters
    if (avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0) {
        std::cerr << "[Heimdall] Cannot copy codec parameters" << std::endl;
        avcodec_free_context(&codec_ctx);
        return false;
    }

    // Enable multi-threaded decoding for faster processing
    AVDictionary* codec_opts = nullptr;
    av_dict_set(&codec_opts, "threads", "auto", 0);  // Use all available CPU cores
    codec_ctx->thread_count = 0;  // 0 = auto-detect optimal thread count
    codec_ctx->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;  // Enable both slice and frame threading

    // Open codec with threading options
    if (avcodec_open2(codec_ctx, codec, &codec_opts) < 0) {
        std::cerr << "[Heimdall] Cannot open codec" << std::endl;
        av_dict_free(&codec_opts);
        avcodec_free_context(&codec_ctx);
        return false;
    }
    av_dict_free(&codec_opts);

    // Create resampler to convert to float samples
    SwrContext* swr_ctx = swr_alloc();
    if (!swr_ctx) {
        std::cerr << "[Heimdall] Cannot allocate resampler" << std::endl;
        avcodec_free_context(&codec_ctx);
        return false;
    }

    // Set resampler options (convert to mono float for waveform)
    // CRITICAL: Use target_sample_rate_ for output to enable downsampling optimization
    AVChannelLayout mono_layout = AV_CHANNEL_LAYOUT_MONO;
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &codec_ctx->ch_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &mono_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", target_sample_rate_, 0);  // Use target rate!
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    if (swr_init(swr_ctx) < 0) {
        std::cerr << "[Heimdall] Cannot initialize resampler" << std::endl;
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        return false;
    }

    // Store stream info (use target_sample_rate_ as the effective output rate)
    StreamInfo info;
    info.stream_index = stream_index;
    info.codec_ctx = codec_ctx;
    info.swr_ctx = swr_ctx;
    info.sample_rate = target_sample_rate_;  // Store target rate, not input rate
    info.channels = codec_ctx->ch_layout.nb_channels;
    audio_streams_.push_back(info);

    return true;
}

int AudioDecoder::get_stream_count() const {
    return static_cast<int>(audio_streams_.size());
}

int64_t AudioDecoder::get_duration_ms() const {
    if (!format_ctx_) return 0;
    return format_ctx_->duration / 1000;  // Convert from microseconds
}

int AudioDecoder::get_sample_rate(int stream_index) const {
    if (stream_index < 0 || stream_index >= static_cast<int>(audio_streams_.size())) {
        return 0;
    }
    return audio_streams_[stream_index].sample_rate;
}

int AudioDecoder::get_channels(int stream_index) const {
    if (stream_index < 0 || stream_index >= static_cast<int>(audio_streams_.size())) {
        return 0;
    }
    return audio_streams_[stream_index].channels;
}

int AudioDecoder::decode_samples(int stream_index, std::vector<float>& output, int max_samples) {
    if (!is_open_ || stream_index < 0 || stream_index >= static_cast<int>(audio_streams_.size())) {
        return 0;
    }

    StreamInfo& stream_info = audio_streams_[stream_index];
    int total_samples = 0;
    bool decode_all = (max_samples < 0);

    // Seek to start
    av_seek_frame(format_ctx_, stream_info.stream_index, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(stream_info.codec_ctx);

    // Decode packets
    while (av_read_frame(format_ctx_, packet_) >= 0) {
        if (packet_->stream_index == stream_info.stream_index) {
            // Send packet to decoder
            if (avcodec_send_packet(stream_info.codec_ctx, packet_) >= 0) {
                // Receive frames
                while (avcodec_receive_frame(stream_info.codec_ctx, frame_) >= 0) {
                    // Convert to float samples
                    int out_samples = frame_->nb_samples;
                    uint8_t* out_buffer = nullptr;

                    av_samples_alloc(&out_buffer, nullptr, 1, out_samples, AV_SAMPLE_FMT_FLT, 0);

                    int converted = swr_convert(
                        stream_info.swr_ctx,
                        &out_buffer,
                        out_samples,
                        (const uint8_t**)frame_->data,
                        frame_->nb_samples
                    );

                    if (converted > 0) {
                        float* samples = reinterpret_cast<float*>(out_buffer);

                        // Copy samples to output
                        int samples_to_copy = converted;
                        if (!decode_all && total_samples + samples_to_copy > max_samples) {
                            samples_to_copy = max_samples - total_samples;
                        }

                        output.insert(output.end(), samples, samples + samples_to_copy);
                        total_samples += samples_to_copy;
                    }

                    av_freep(&out_buffer);

                    // Check if we've decoded enough
                    if (!decode_all && total_samples >= max_samples) {
                        av_packet_unref(packet_);
                        return total_samples;
                    }
                }
            }
        }
        av_packet_unref(packet_);
    }

    return total_samples;
}

int AudioDecoder::extract_streams(
    const std::vector<int>& stream_indices,
    std::map<int, std::vector<float>>& outputs,
    int quality
) {
    if (!is_open_) {
        return 0;
    }

    // Determine which streams to extract
    std::vector<int> indices_to_use;
    if (stream_indices.empty()) {
        // Extract all streams
        for (size_t i = 0; i < audio_streams_.size(); i++) {
            indices_to_use.push_back(static_cast<int>(i));
        }
    } else {
        indices_to_use = stream_indices;
    }

    // Calculate packet skip from quality (quality 100 = skip 1, quality 10 = skip 10)
    int packet_skip = (quality > 0) ? std::max(1, 100 / quality) : 1;
    bool full_quality = (packet_skip == 1);

    std::ostringstream oss;
    oss << "[Heimdall] Extracting " << indices_to_use.size() << " streams at "
        << target_sample_rate_ << "Hz (quality=" << quality << ", skip=" << packet_skip << ")";
    TS_PRINT(oss.str());

    // Validate stream indices and build lookup map
    std::map<int, int> file_index_to_logical;
    for (int logical_idx : indices_to_use) {
        if (logical_idx < 0 || logical_idx >= static_cast<int>(audio_streams_.size())) {
            continue;
        }
        int file_idx = audio_streams_[logical_idx].stream_index;
        file_index_to_logical[file_idx] = logical_idx;
        outputs[logical_idx] = std::vector<float>();
    }

    if (file_index_to_logical.empty()) {
        TS_PRINT("[Heimdall] ERROR: No valid streams to extract");
        return 0;
    }

    // Seek to start
    av_seek_frame(format_ctx_, -1, 0, AVSEEK_FLAG_BACKWARD);

    // Flush all codec buffers
    for (auto& stream : audio_streams_) {
        avcodec_flush_buffers(stream.codec_ctx);
    }

    int packets_read = 0;
    std::map<int, int> samples_per_stream;
    std::map<int, int> stream_packet_counters;

    // Single pass through file
    while (av_read_frame(format_ctx_, packet_) >= 0) {
        packets_read++;

        auto it = file_index_to_logical.find(packet_->stream_index);
        if (it != file_index_to_logical.end()) {
            int logical_idx = it->second;
            StreamInfo& stream_info = audio_streams_[logical_idx];

            // Skip packets based on quality setting
            if (!full_quality && (++stream_packet_counters[logical_idx] % packet_skip != 0)) {
                av_packet_unref(packet_);
                continue;
            }

            if (avcodec_send_packet(stream_info.codec_ctx, packet_) >= 0) {
                while (avcodec_receive_frame(stream_info.codec_ctx, frame_) >= 0) {
                    // Calculate output samples after resampling
                    int out_samples = swr_get_out_samples(stream_info.swr_ctx, frame_->nb_samples);
                    if (out_samples <= 0) {
                        out_samples = frame_->nb_samples;
                    }

                    uint8_t* out_buffer = nullptr;
                    av_samples_alloc(&out_buffer, nullptr, 1, out_samples, AV_SAMPLE_FMT_FLT, 0);

                    int converted = swr_convert(
                        stream_info.swr_ctx,
                        &out_buffer,
                        out_samples,
                        (const uint8_t**)frame_->data,
                        frame_->nb_samples
                    );

                    if (converted > 0) {
                        float* samples = reinterpret_cast<float*>(out_buffer);
                        outputs[logical_idx].insert(outputs[logical_idx].end(), samples, samples + converted);
                        samples_per_stream[logical_idx] += converted;
                    }

                    av_freep(&out_buffer);
                }
            }
        }
        av_packet_unref(packet_);
    }

    // Flush remaining samples from resampler (important for full quality)
    if (full_quality) {
        for (int logical_idx : indices_to_use) {
            if (logical_idx < 0 || logical_idx >= static_cast<int>(audio_streams_.size())) {
                continue;
            }
            StreamInfo& stream_info = audio_streams_[logical_idx];

            int out_samples = swr_get_out_samples(stream_info.swr_ctx, 0);
            if (out_samples > 0) {
                uint8_t* out_buffer = nullptr;
                av_samples_alloc(&out_buffer, nullptr, 1, out_samples, AV_SAMPLE_FMT_FLT, 0);

                int converted = swr_convert(stream_info.swr_ctx, &out_buffer, out_samples, nullptr, 0);
                if (converted > 0) {
                    float* samples = reinterpret_cast<float*>(out_buffer);
                    outputs[logical_idx].insert(outputs[logical_idx].end(), samples, samples + converted);
                    samples_per_stream[logical_idx] += converted;
                }
                av_freep(&out_buffer);
            }
        }
    }

    oss.str("");
    oss << "[Heimdall] Extraction complete: " << packets_read << " packets processed";
    TS_PRINT(oss.str());

    int successful_streams = 0;
    for (const auto& pair : outputs) {
        if (!pair.second.empty()) {
            successful_streams++;
            oss.str("");
            oss << "[Heimdall] Stream " << pair.first << ": " << pair.second.size()
                << " samples (" << std::fixed << std::setprecision(2)
                << (pair.second.size() / static_cast<double>(target_sample_rate_)) << "s)";
            TS_PRINT(oss.str());
        }
    }

    return successful_streams;
}

std::vector<int> AudioDecoder::get_all_stream_indices() const {
    std::vector<int> indices;
    for (size_t i = 0; i < audio_streams_.size(); i++) {
        indices.push_back(static_cast<int>(i));
    }
    return indices;
}

void AudioDecoder::close() {
    if (!is_open_) return;

    // Free stream resources
    for (auto& stream : audio_streams_) {
        if (stream.swr_ctx) swr_free(&stream.swr_ctx);
        if (stream.codec_ctx) avcodec_free_context(&stream.codec_ctx);
    }
    audio_streams_.clear();

    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }

    is_open_ = false;
}

} // namespace heimdall
