/**
 * @file karaoke_captions.cpp
 * @brief Example: live captions with the current word highlighted
 *
 * Plays an audio file into StreamingTranscriber in real time (as a microphone
 * would deliver it) and draws the latest captions in the terminal, lighting up
 * each word as it is spoken, using the word timestamps streaming returns.
 *
 * Captions arrive a little after the speech (one window plus the overlap), so
 * the display runs on a clock delayed by that much, the way a broadcast
 * caption delay works.
 *
 * Usage: karaoke_captions <model_dir> <audio_file> [cpu|cuda|auto]
 */

#include <muninn/audio_extractor.h>
#include <muninn/logging.h>
#include <muninn/streaming_transcriber.h>
#include <muninn/word_styling.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// The last few segments, shared between the streaming callback and the
// drawing loop.
class CaptionBoard {
public:
    void add(const muninn::Segment& seg) {
        std::lock_guard<std::mutex> lock(mutex_);
        segments_.push_back(seg);
        if (segments_.size() > 3) segments_.erase(segments_.begin());
    }

    void draw(float display_time) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "\033[2J\033[H";  // clear the screen, cursor home
        std::cout << "Live captions (display clock " << std::fixed << std::setprecision(1)
                  << display_time << " s)\n\n";
        for (const auto& seg : segments_) {
            if (seg.start > display_time) continue;  // not due on screen yet
            if (seg.words.empty()) {
                std::cout << seg.text << "\n";
            } else {
                std::cout << muninn::ANSIFormatter::build_karaoke_ansi(seg, display_time) << "\n";
            }
        }
        std::cout << std::flush;
    }

private:
    std::mutex mutex_;
    std::vector<muninn::Segment> segments_;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <audio_file> [cpu|cuda|auto]\n";
        return 1;
    }
    const std::string model_path = argv[1];
    const std::string audio_path = argv[2];
    const std::string device = argc > 3 ? argv[3] : "auto";

    // Muninn's info lines would scroll the captions away; keep warnings only.
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
        options.language = "auto";
        options.chunk_length_s = 3.0f;
        options.overlap_s = 0.6f;
        options.enable_vad = true;
        options.word_timestamps = true;

        CaptionBoard board;
        transcriber.set_error_callback([](const std::string& message) {
            std::cerr << "streaming error: " << message << "\n";
        });
        transcriber.start(options, [&board](const muninn::Segment& seg) {
            board.add(seg);
            return true;
        });

        // Feed the file in 20 ms blocks at real-time speed, and redraw.
        const float delay_s = options.chunk_length_s + options.overlap_s + 1.0f;
        const size_t block = 16000 / 50;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < audio.size(); pos += block) {
            const size_t len = std::min(block, audio.size() - pos);
            transcriber.push_audio(audio.data() + pos, len, 16000, 1);

            const float audio_time = static_cast<float>(pos + len) / 16000.0f;
            std::this_thread::sleep_until(t0 + std::chrono::duration<float>(audio_time));
            if ((pos / block) % 3 == 0) board.draw(audio_time - delay_s);
        }

        const auto segments = transcriber.stop();
        board.draw(duration);
        std::cout << "\nDone: " << segments.size() << " segments.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
