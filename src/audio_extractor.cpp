#include "muninn/audio_extractor.h"
#include <iostream>

#ifdef WITH_HEIMDALL
#include "heimdall.h"  // Heimdall DLL API
#endif

namespace muninn {

// =======================
// AudioExtractor::Impl
// =======================

#ifdef WITH_HEIMDALL

class AudioExtractor::Impl {
public:
    heimdall::Heimdall heimdall;
    static constexpr int WHISPER_SAMPLE_RATE = 16000;

    // Cached file info
    std::string current_file;
    heimdall::AudioInfo info = {};
    bool is_open = false;
};

AudioExtractor::AudioExtractor()
    : pimpl_(std::make_unique<Impl>())
{
}

AudioExtractor::~AudioExtractor() = default;

bool AudioExtractor::open(const std::string& file_path)
{
    last_error_.clear();

    try {
        pimpl_->info = pimpl_->heimdall.get_audio_info(file_path);
        pimpl_->current_file = file_path;
        pimpl_->is_open = true;

        std::cout << "[Muninn] Opened file with " << pimpl_->info.stream_count
                  << " audio track(s), duration: " << (pimpl_->info.duration_ms / 1000.0f) << "s\n";

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to open file: ") + e.what();
        std::cerr << "[Muninn] " << last_error_ << "\n";
        pimpl_->is_open = false;
        return false;
    }
}

void AudioExtractor::close()
{
    pimpl_->is_open = false;
    pimpl_->current_file.clear();
    pimpl_->info = {};
}

int AudioExtractor::get_track_count() const
{
    if (!pimpl_->is_open) return 0;
    return pimpl_->info.stream_count;
}

float AudioExtractor::get_duration() const
{
    if (!pimpl_->is_open) return 0.0f;
    return static_cast<float>(pimpl_->info.duration_ms) / 1000.0f;
}

bool AudioExtractor::extract_track(int track_index, std::vector<float>& samples)
{
    last_error_.clear();

    if (!pimpl_->is_open) {
        last_error_ = "No file is open";
        return false;
    }

    if (track_index < 0 || track_index >= pimpl_->info.stream_count) {
        last_error_ = "Invalid track index: " + std::to_string(track_index);
        return false;
    }

    try {
        // Extract audio using Heimdall DLL
        // Request specific stream at 16kHz with full quality
        std::vector<int> stream_indices = { track_index };
        auto tracks = pimpl_->heimdall.extract_audio(
            pimpl_->current_file,
            Impl::WHISPER_SAMPLE_RATE,
            stream_indices,
            100  // Full quality for transcription
        );

        if (tracks.find(track_index) == tracks.end() || tracks[track_index].empty()) {
            last_error_ = "Failed to extract audio from track " + std::to_string(track_index);
            std::cerr << "[Muninn] " << last_error_ << "\n";
            return false;
        }

        samples = std::move(tracks[track_index]);

        std::cout << "[Muninn] Track " << track_index << ": extracted " << samples.size()
                  << " samples (" << (samples.size() / 16000.0f) << "s at 16kHz)\n";

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to extract audio: ") + e.what();
        std::cerr << "[Muninn] " << last_error_ << "\n";
        return false;
    }
}

bool AudioExtractor::extract_audio(const std::string& file_path,
                                   std::vector<float>& samples,
                                   float& duration)
{
    // Convenience method: extract track 0 directly
    last_error_.clear();

    try {
        auto info = pimpl_->heimdall.get_audio_info(file_path);
        duration = static_cast<float>(info.duration_ms) / 1000.0f;

        if (info.stream_count == 0) {
            last_error_ = "No audio tracks in file";
            return false;
        }

        // Extract first track at 16kHz with full quality
        std::vector<int> stream_indices = { 0 };
        auto tracks = pimpl_->heimdall.extract_audio(
            file_path,
            Impl::WHISPER_SAMPLE_RATE,
            stream_indices,
            100
        );

        if (tracks.find(0) == tracks.end() || tracks[0].empty()) {
            last_error_ = "Failed to extract audio from track 0";
            return false;
        }

        samples = std::move(tracks[0]);

        std::cout << "[Muninn] Extracted " << samples.size() << " samples ("
                  << (samples.size() / 16000.0f) << "s at 16kHz)\n";

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to extract audio: ") + e.what();
        std::cerr << "[Muninn] " << last_error_ << "\n";
        return false;
    }
}

#else  // !WITH_HEIMDALL

// Stub implementation when Heimdall is not available
class AudioExtractor::Impl {};

AudioExtractor::AudioExtractor()
    : pimpl_(std::make_unique<Impl>())
{
}

AudioExtractor::~AudioExtractor() = default;

bool AudioExtractor::open(const std::string& file_path)
{
    last_error_ = "Heimdall not available";
    return false;
}

void AudioExtractor::close() {}

int AudioExtractor::get_track_count() const { return 0; }

float AudioExtractor::get_duration() const { return 0.0f; }

bool AudioExtractor::extract_track(int track_index, std::vector<float>& samples)
{
    last_error_ = "Heimdall not available";
    return false;
}

bool AudioExtractor::extract_audio(const std::string& file_path,
                                   std::vector<float>& samples,
                                   float& duration)
{
    last_error_ = "Heimdall not available. Muninn was built without audio file loading support.";
    std::cerr << "[Muninn] ERROR: " << last_error_ << "\n";
    std::cerr << "[Muninn] Use transcribe(samples, sample_rate) API instead.\n";
    return false;
}

#endif  // WITH_HEIMDALL

} // namespace muninn
