#include "muninn/audio_extractor.h"
#include "audio/audio_decoder.h"
#include <iostream>

namespace muninn {

// =======================
// AudioExtractor::Impl
// =======================

class AudioExtractor::Impl {
public:
    audio::AudioDecoder decoder;
    static constexpr int WHISPER_SAMPLE_RATE = 16000;

    // Cached file info
    std::string current_file;
    int stream_count = 0;
    int64_t duration_ms = 0;
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
        // Open file with Whisper sample rate (16kHz)
        if (!pimpl_->decoder.open(file_path, Impl::WHISPER_SAMPLE_RATE)) {
            last_error_ = "Failed to open audio file";
            std::cerr << "[Muninn] " << last_error_ << ": " << file_path << "\n";
            pimpl_->is_open = false;
            return false;
        }

        pimpl_->current_file = file_path;
        pimpl_->stream_count = pimpl_->decoder.get_stream_count();
        pimpl_->duration_ms = pimpl_->decoder.get_duration_ms();
        pimpl_->is_open = true;

        std::cout << "[Muninn] Opened file with " << pimpl_->stream_count
                  << " audio track(s), duration: " << (pimpl_->duration_ms / 1000.0f) << "s\n";

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
    if (pimpl_->is_open) {
        pimpl_->decoder.close();
    }
    pimpl_->is_open = false;
    pimpl_->current_file.clear();
    pimpl_->stream_count = 0;
    pimpl_->duration_ms = 0;
}

int AudioExtractor::get_track_count() const
{
    if (!pimpl_->is_open) return 0;
    return pimpl_->stream_count;
}

float AudioExtractor::get_duration() const
{
    if (!pimpl_->is_open) return 0.0f;
    return static_cast<float>(pimpl_->duration_ms) / 1000.0f;
}

bool AudioExtractor::extract_track(int track_index, std::vector<float>& samples)
{
    last_error_.clear();

    if (!pimpl_->is_open) {
        last_error_ = "No file is open";
        return false;
    }

    if (track_index < 0 || track_index >= pimpl_->stream_count) {
        last_error_ = "Invalid track index: " + std::to_string(track_index);
        return false;
    }

    try {
        // Extract audio using internal audio decoder
        // Request specific stream at 16kHz with full quality
        std::vector<int> stream_indices = { track_index };
        std::map<int, std::vector<float>> tracks;

        int extracted = pimpl_->decoder.extract_streams(
            stream_indices,
            tracks,
            100  // Full quality for transcription
        );

        if (extracted == 0 || tracks.find(track_index) == tracks.end() || tracks[track_index].empty()) {
            last_error_ = "Failed to extract audio from track " + std::to_string(track_index);
            std::cerr << "[Muninn] " << last_error_ << "\n";
            return false;
        }

        samples = std::move(tracks[track_index]);

        std::cout << "[Muninn] Track " << track_index << ": extracted " << samples.size()
                  << " samples (" << (samples.size() / static_cast<float>(Impl::WHISPER_SAMPLE_RATE)) << "s at 16kHz)\n";

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
        // Create temporary decoder for this operation
        audio::AudioDecoder temp_decoder;

        if (!temp_decoder.open(file_path, Impl::WHISPER_SAMPLE_RATE)) {
            last_error_ = "Failed to open audio file";
            std::cerr << "[Muninn] " << last_error_ << ": " << file_path << "\n";
            return false;
        }

        int stream_count = temp_decoder.get_stream_count();
        duration = static_cast<float>(temp_decoder.get_duration_ms()) / 1000.0f;

        if (stream_count == 0) {
            last_error_ = "No audio tracks in file";
            temp_decoder.close();
            return false;
        }

        // Extract first track at 16kHz with full quality
        std::vector<int> stream_indices = { 0 };
        std::map<int, std::vector<float>> tracks;

        int extracted = temp_decoder.extract_streams(
            stream_indices,
            tracks,
            100  // Full quality
        );

        temp_decoder.close();

        if (extracted == 0 || tracks.find(0) == tracks.end() || tracks[0].empty()) {
            last_error_ = "Failed to extract audio from track 0";
            return false;
        }

        samples = std::move(tracks[0]);

        std::cout << "[Muninn] Extracted " << samples.size() << " samples ("
                  << (samples.size() / static_cast<float>(Impl::WHISPER_SAMPLE_RATE)) << "s at 16kHz)\n";

        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to extract audio: ") + e.what();
        std::cerr << "[Muninn] " << last_error_ << "\n";
        return false;
    }
}

} // namespace muninn
