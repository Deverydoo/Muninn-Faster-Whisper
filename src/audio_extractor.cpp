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
};

AudioExtractor::AudioExtractor()
    : pimpl_(std::make_unique<Impl>())
{
}

AudioExtractor::~AudioExtractor() = default;

bool AudioExtractor::extract_audio(const std::string& file_path,
                                   std::vector<float>& samples,
                                   float& duration)
{
    last_error_.clear();

    // Open video file with Heimdall
    if (!pimpl_->decoder.open(file_path, Impl::WHISPER_SAMPLE_RATE)) {
        last_error_ = "Failed to open file: " + file_path;
        std::cerr << "[Muninn] " << last_error_ << "\n";
        return false;
    }

    // Get audio stream info
    int stream_count = pimpl_->decoder.get_stream_count();
    if (stream_count == 0) {
        last_error_ = "No audio streams found in file";
        std::cerr << "[Muninn] " << last_error_ << "\n";
        pimpl_->decoder.close();
        return false;
    }

    std::cout << "[Muninn] Found " << stream_count << " audio stream(s) in file\n";

    // Get duration
    int64_t duration_ms = pimpl_->decoder.get_duration_ms();
    duration = static_cast<float>(duration_ms) / 1000.0f;

    std::cout << "[Muninn] Audio duration: " << duration << " seconds\n";

    // Extract audio from first stream
    int samples_decoded = pimpl_->decoder.decode_samples(0, samples);
    if (samples_decoded <= 0) {
        last_error_ = "Failed to decode audio samples";
        std::cerr << "[Muninn] " << last_error_ << "\n";
        pimpl_->decoder.close();
        return false;
    }

    std::cout << "[Muninn] Decoded " << samples_decoded << " samples\n";

    // Convert to mono if needed
    int channels = pimpl_->decoder.get_channels(0);
    if (channels > 1) {
        std::cout << "[Muninn] Converting " << channels << " channels to mono\n";

        std::vector<float> mono_samples;
        mono_samples.reserve(samples.size() / channels);

        for (size_t i = 0; i < samples.size(); i += channels) {
            float sum = 0.0f;
            for (int c = 0; c < channels; c++) {
                sum += samples[i + c];
            }
            mono_samples.push_back(sum / channels);
        }

        samples = std::move(mono_samples);
        std::cout << "[Muninn] Mono conversion complete: " << samples.size() << " samples\n";
    }

    pimpl_->decoder.close();
    return true;
}

#else  // !WITH_FFMPEG

// Stub implementation when FFmpeg is not available
class AudioExtractor::Impl {};

AudioExtractor::AudioExtractor()
    : pimpl_(std::make_unique<Impl>())
{
}

AudioExtractor::~AudioExtractor() = default;

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
