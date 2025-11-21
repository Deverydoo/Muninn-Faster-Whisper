#include "muninn/audio_extractor.h"
#include <iostream>

#ifdef WITH_FFMPEG
#include "audio_decoder.h"  // Heimdall's AudioDecoder
#endif

namespace muninn {

// =======================
// AudioExtractor::Impl
// =======================

#ifdef WITH_FFMPEG

class AudioExtractor::Impl {
public:
    heimdall::AudioDecoder decoder;
    static constexpr int WHISPER_SAMPLE_RATE = 16000;
    bool is_open = false;
    float duration_sec = 0.0f;
};

AudioExtractor::AudioExtractor()
    : pimpl_(std::make_unique<Impl>())
{
}

AudioExtractor::~AudioExtractor() = default;

bool AudioExtractor::open(const std::string& file_path)
{
    last_error_.clear();

    if (pimpl_->is_open) {
        pimpl_->decoder.close();
        pimpl_->is_open = false;
    }

    if (!pimpl_->decoder.open(file_path, Impl::WHISPER_SAMPLE_RATE)) {
        last_error_ = "Failed to open file: " + file_path;
        std::cerr << "[Muninn] " << last_error_ << "\n";
        return false;
    }

    int64_t duration_ms = pimpl_->decoder.get_duration_ms();
    pimpl_->duration_sec = static_cast<float>(duration_ms) / 1000.0f;
    pimpl_->is_open = true;

    int stream_count = pimpl_->decoder.get_stream_count();
    std::cout << "[Muninn] Opened file with " << stream_count << " audio track(s), duration: "
              << pimpl_->duration_sec << "s\n";

    return true;
}

void AudioExtractor::close()
{
    if (pimpl_->is_open) {
        pimpl_->decoder.close();
        pimpl_->is_open = false;
        pimpl_->duration_sec = 0.0f;
    }
}

int AudioExtractor::get_track_count() const
{
    if (!pimpl_->is_open) return 0;
    return pimpl_->decoder.get_stream_count();
}

float AudioExtractor::get_duration() const
{
    return pimpl_->duration_sec;
}

bool AudioExtractor::extract_track(int track_index, std::vector<float>& samples)
{
    last_error_.clear();

    if (!pimpl_->is_open) {
        last_error_ = "No file is open";
        return false;
    }

    int stream_count = pimpl_->decoder.get_stream_count();
    if (track_index < 0 || track_index >= stream_count) {
        last_error_ = "Invalid track index: " + std::to_string(track_index);
        return false;
    }

    // IMPORTANT: Clear the output vector before decoding
    // The Heimdall decoder appends to the vector, so we need to clear it first
    samples.clear();

    // Decode samples from the specified track
    int samples_decoded = pimpl_->decoder.decode_samples(track_index, samples);
    if (samples_decoded <= 0) {
        last_error_ = "Failed to decode audio from track " + std::to_string(track_index);
        std::cerr << "[Muninn] " << last_error_ << "\n";
        return false;
    }

    std::cout << "[Muninn] Track " << track_index << ": decoded " << samples_decoded << " samples ("
              << (samples_decoded / 16000.0f) << "s at 16kHz)\n";

    // NOTE: Heimdall's resampler is configured to output mono (AV_CHANNEL_LAYOUT_MONO)
    // so the samples are already mono. The get_channels() returns the INPUT channel count,
    // not the output. So we do NOT need to convert to mono here.

    return true;
}

bool AudioExtractor::extract_audio(const std::string& file_path,
                                   std::vector<float>& samples,
                                   float& duration)
{
    // Convenience method: open, extract track 0, close
    if (!open(file_path)) {
        return false;
    }

    duration = pimpl_->duration_sec;

    bool success = extract_track(0, samples);

    close();
    return success;
}

#else  // !WITH_FFMPEG

// Stub implementation when FFmpeg is not available
class AudioExtractor::Impl {};

AudioExtractor::AudioExtractor()
    : pimpl_(std::make_unique<Impl>())
{
}

AudioExtractor::~AudioExtractor() = default;

bool AudioExtractor::open(const std::string& file_path)
{
    last_error_ = "FFmpeg not available";
    return false;
}

void AudioExtractor::close() {}

int AudioExtractor::get_track_count() const { return 0; }

float AudioExtractor::get_duration() const { return 0.0f; }

bool AudioExtractor::extract_track(int track_index, std::vector<float>& samples)
{
    last_error_ = "FFmpeg not available";
    return false;
}

bool AudioExtractor::extract_audio(const std::string& file_path,
                                   std::vector<float>& samples,
                                   float& duration)
{
    last_error_ = "FFmpeg not available. Muninn was built without audio file loading support.";
    std::cerr << "[Muninn] ERROR: " << last_error_ << "\n";
    std::cerr << "[Muninn] Use transcribe(samples, sample_rate) API instead.\n";
    return false;
}

#endif  // WITH_FFMPEG

} // namespace muninn
