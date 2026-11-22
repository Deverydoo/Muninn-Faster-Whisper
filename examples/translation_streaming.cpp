/**
 * @file translation_streaming.cpp
 * @brief Example: live English captions from speech in any language
 *
 * Plays an audio file into StreamingTranscriber in real time with
 * language = "auto" and task = "translate": Whisper detects the spoken
 * language on the first speech and writes English captions as it goes.
 *
 * Whisper's translate task only goes into English. For other target
 * languages, transcribe and then use muninn::Translator (see
 * translation_example.cpp).
 *
 * Usage: translation_streaming <model_dir> <audio_file> [cpu|cuda|auto]
 *
 * The model must be a multilingual one (not *.en), and not large-v3-turbo,
 * which was fine-tuned without translation data and mostly transcribes.
 */

#include <muninn/audio_extractor.h>
#include <muninn/logging.h>
#include <muninn/streaming_transcriber.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string language_name(const std::string& code) {
    static const std::map<std::string, std::string> names = {
        {"en", "English"}, {"es", "Spanish"}, {"fr", "French"},
        {"de", "German"}, {"it", "Italian"}, {"pt", "Portuguese"},
        {"ru", "Russian"}, {"ja", "Japanese"}, {"ko", "Korean"},
        {"zh", "Chinese"}, {"ar", "Arabic"}, {"hi", "Hindi"},
        {"nl", "Dutch"}, {"pl", "Polish"}, {"tr", "Turkish"}};
    auto it = names.find(code);
    return it != names.end() ? it->second : code;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <audio_file> [cpu|cuda|auto]\n";
        return 1;
    }
    const std::string model_path = argv[1];
    const std::string audio_path = argv[2];
    const std::string device = argc > 3 ? argv[3] : "auto";

    muninn::Logger::set_callback([](muninn::LogLevel level, const std::string& message) {
        if (level >= muninn::LogLevel::Warning) std::cerr << message << "\n";
    });

    try {
        std::vector<float> audio;
        float duration = 0.0f;
        muninn::AudioExtractor extractor;
        if (!extractor.extract_audio(audio_path, audio, duration)) {
            std::cerr << "Could not read " << audio_path << ": " << extractor.get_last_error() << "\n";
            return 1;
        }

        std::cout << "Loading model: " << model_path << "...\n";
        muninn::StreamingTranscriber transcriber(model_path, device, "float16");

        muninn::StreamingOptions options;
        options.language = "auto";      // detected on the first speech
        options.task = "translate";     // English captions
        options.chunk_length_s = 3.0f;
        options.overlap_s = 0.6f;
        options.enable_vad = true;
        options.word_timestamps = false;

        bool announced = false;
        transcriber.set_error_callback([](const std::string& message) {
            std::cerr << "streaming error: " << message << "\n";
        });
        transcriber.start(options, [&announced](const muninn::Segment& seg) {
            if (!announced) {
                std::cout << "Spoken language: " << language_name(seg.language) << " ("
                          << std::fixed << std::setprecision(0) << seg.language_probability * 100.0f
                          << "%)\n\n";
                announced = true;
            }
            std::cout << "[" << std::fixed << std::setprecision(1) << std::setw(6) << seg.start << " - "
                      << std::setw(6) << seg.end << "] " << seg.text << "\n";
            return true;
        });

        // Feed the file in 20 ms blocks at real-time speed.
        const size_t block = 16000 / 50;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < audio.size(); pos += block) {
            const size_t len = std::min(block, audio.size() - pos);
            transcriber.push_audio(audio.data() + pos, len, 16000, 1);
            const float audio_time = static_cast<float>(pos + len) / 16000.0f;
            std::this_thread::sleep_until(t0 + std::chrono::duration<float>(audio_time));
        }

        const auto segments = transcriber.stop();
        const auto stats = transcriber.get_stats();
        std::cout << "\nDone: " << segments.size() << " segments, average decode "
                  << std::setprecision(0) << stats.average_latency_ms << " ms per window.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
