#include "heimdall.h"
#include "audio_decoder.h"  // Must be included for unique_ptr to work with AudioDecoder
#include "peak_detector.h"  // Must be included for unique_ptr to work with PeakDetector
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

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

Heimdall::Heimdall()
    : decoder_(std::make_unique<AudioDecoder>())
    , peak_detector_(std::make_unique<PeakDetector>())
{
    TS_PRINT("[Heimdall] The guardian awakens...");
}

Heimdall::~Heimdall() {
    TS_PRINT("[Heimdall] The guardian rests.");
}

AudioInfo Heimdall::get_audio_info(const std::string& audio_file) {
    AudioInfo info = {0, 0, 0, 0};

    AudioDecoder temp_decoder;
    if (!temp_decoder.open(audio_file)) {
        std::cerr << "[Heimdall] Cannot open file: " << audio_file << std::endl;
        return info;
    }

    info.duration_ms = temp_decoder.get_duration_ms();
    info.stream_count = temp_decoder.get_stream_count();

    if (info.stream_count > 0) {
        info.sample_rate = temp_decoder.get_sample_rate(0);
        info.channels = temp_decoder.get_channels(0);
    }

    temp_decoder.close();

    std::ostringstream oss;
    oss << "[Heimdall] Audio info - Duration: " << info.duration_ms << "ms, "
        << "Streams: " << info.stream_count << ", "
        << "Rate: " << info.sample_rate << "Hz";
    TS_PRINT(oss.str());

    return info;
}

std::vector<float> Heimdall::generate_peaks(
    const std::string& audio_file,
    int stream_index,
    int width,
    int height,
    int samples_per_pixel,
    bool normalize
) {
    std::ostringstream oss;
    oss << "[Heimdall] Listening to stream " << stream_index << "...";
    TS_PRINT(oss.str());

    // Open audio file
    if (!decoder_->open(audio_file)) {
        TS_PRINT("[Heimdall] Failed to open audio file");
        return std::vector<float>();
    }

    // Check stream index
    if (stream_index >= decoder_->get_stream_count()) {
        oss.str("");
        oss << "[Heimdall] Stream index " << stream_index << " out of range (max: "
            << decoder_->get_stream_count() - 1 << ")";
        TS_PRINT(oss.str());
        decoder_->close();
        return std::vector<float>();
    }

    // Decode all audio samples
    std::vector<float> samples;
    int decoded = decoder_->decode_samples(stream_index, samples);

    oss.str("");
    oss << "[Heimdall] Decoded " << decoded << " samples from stream " << stream_index;
    TS_PRINT(oss.str());

    decoder_->close();

    if (samples.empty()) {
        TS_PRINT("[Heimdall] No samples decoded");
        return std::vector<float>();
    }

    // Compute peaks with SIMD acceleration
    TS_PRINT("[Heimdall] Computing peaks with acute precision...");
    std::vector<float> peaks = peak_detector_->compute_peaks(
        samples.data(),
        static_cast<int>(samples.size()),
        width,
        normalize
    );

    oss.str("");
    oss << "[Heimdall] Generated " << peaks.size() / 2 << " peak pairs for "
        << width << " pixel width";
    TS_PRINT(oss.str());

    return peaks;
}

std::map<int, std::vector<float>> Heimdall::generate_batch(
    const std::string& audio_file,
    const std::vector<int>& stream_indices,
    int width,
    int height,
    int target_sample_rate,
    int packet_quality
) {
    std::ostringstream oss;
    oss << "[Heimdall] Guardian watches over " << stream_indices.size() << " streams...";
    TS_PRINT(oss.str());

    std::map<int, std::vector<float>> result;

    // Use extract_audio as the core - this is the unified extraction path
    std::map<int, std::vector<float>> all_samples = extract_audio(
        audio_file,
        target_sample_rate,
        stream_indices,
        packet_quality  // quality maps directly now
    );

    if (all_samples.empty()) {
        TS_PRINT("[Heimdall] No audio extracted");
        return result;
    }

    TS_PRINT("[Heimdall] Computing peaks for all streams with SIMD acceleration...");

    // Compute peaks for each extracted stream (fast SIMD step)
    for (const auto& pair : all_samples) {
        int stream_idx = pair.first;
        const std::vector<float>& samples = pair.second;

        if (!samples.empty()) {
            oss.str("");
            oss << "[Heimdall] Stream " << stream_idx << ": Computing peaks from " << samples.size() << " samples...";
            TS_PRINT(oss.str());

            std::vector<float> peaks = peak_detector_->compute_peaks(
                samples.data(),
                static_cast<int>(samples.size()),
                width,
                true  // Always normalize in batch mode
            );

            result[stream_idx] = peaks;
            oss.str("");
            oss << "[Heimdall] Stream " << stream_idx << ": " << peaks.size() / 2 << " peaks";
            TS_PRINT(oss.str());
        }
    }

    oss.str("");
    oss << "[Heimdall] Batch complete - " << result.size() << " streams processed";
    TS_PRINT(oss.str());

    return result;
}

std::map<int, std::vector<float>> Heimdall::extract_audio(
    const std::string& audio_file,
    int sample_rate,
    const std::vector<int>& stream_indices,
    int quality
) {
    std::ostringstream oss;
    oss << "[Heimdall] Extracting audio at " << sample_rate << "Hz, quality=" << quality;
    TS_PRINT(oss.str());

    std::map<int, std::vector<float>> result;

    // Open file with requested sample rate
    if (!decoder_->open(audio_file, sample_rate)) {
        TS_PRINT("[Heimdall] Failed to open audio file");
        return result;
    }

    // Extract streams using unified method
    int extracted = decoder_->extract_streams(stream_indices, result, quality);

    decoder_->close();

    oss.str("");
    oss << "[Heimdall] Extraction complete - " << extracted << " streams extracted";
    TS_PRINT(oss.str());

    return result;
}

} // namespace heimdall
