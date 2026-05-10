/**
 * @file karaoke_captions.cpp
 * @brief Example: Karaoke-style word highlighting with intensity-based coloring
 *
 * This example demonstrates how to use Muninn's streaming API with word timestamps
 * and audio intensity analysis to create dynamic karaoke-style captions where:
 * - Words light up as they're spoken
 * - Color changes based on volume/emphasis:
 *   - Whispered words: dim gray
 *   - Normal speech: white/yellow
 *   - Shouted words: bright red
 *
 * Perfect for:
 * - Live streaming captions with visual dynamics
 * - Video subtitles with word highlighting
 * - Language learning applications
 * - Accessibility features with emphasis indication
 */

#include <muninn/streaming_transcriber.h>
#include <muninn/word_styling.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>

/**
 * @brief Render a caption line with intensity-based karaoke highlighting
 *
 * @param segment The transcription segment with word timestamps
 * @param current_time Current playback time in seconds
 */
void render_karaoke_caption(const muninn::Segment& segment, float current_time) {
    if (segment.words.empty()) {
        // No word timestamps - show full text
        std::cout << segment.text << std::endl;
        return;
    }

    // Use the built-in ANSI formatter with intensity-based coloring
    std::string formatted = muninn::ANSIFormatter::build_karaoke_ansi(segment, current_time);

    // Render the caption line
    std::cout << "\r" << formatted << std::flush;
}

/**
 * @brief Render with confidence-based coloring (alternative mode)
 */
void render_confidence_caption(const muninn::Segment& segment, float current_time) {
    if (segment.words.empty()) {
        std::cout << segment.text << std::endl;
        return;
    }

    std::string formatted = muninn::ANSIFormatter::build_confidence_ansi(segment, current_time);
    std::cout << "\r" << formatted << std::flush;
}

/**
 * @brief Simulated microphone audio source
 *
 * In a real application, this would capture from a microphone or audio device.
 */
class SimulatedMicrophone {
public:
    // Simulate 48kHz mono audio
    std::vector<float> get_next_chunk(size_t samples) {
        // In real app: capture from audio device
        // For demo: return silence
        return std::vector<float>(samples, 0.0f);
    }
};

/**
 * @brief Caption display manager with word highlighting
 */
class KaraokeCaptionDisplay {
public:
    void update(const muninn::Segment& new_segment) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Keep last 3 segments visible
        segments_.push_back(new_segment);
        if (segments_.size() > 3) {
            segments_.erase(segments_.begin());
        }

        // Update start time for playback
        if (segments_.size() == 1) {
            playback_start_ = std::chrono::steady_clock::now();
        }
    }

    void render() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (segments_.empty()) {
            return;
        }

        // Calculate current time relative to first segment
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - playback_start_).count();

        // Clear screen and render caption block
        std::cout << "\033[2J\033[H";  // Clear screen, move to home

        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "                 LIVE KARAOKE CAPTIONS\n";
        std::cout << "═══════════════════════════════════════════════════════════\n\n";

        // Render each visible segment
        for (const auto& seg : segments_) {
            float seg_time = elapsed - (seg.start - segments_[0].start);
            render_karaoke_caption(seg, seg_time);
            std::cout << "\n";
        }

        std::cout << "\n═══════════════════════════════════════════════════════════\n";
        std::cout << "Time: " << std::fixed << std::setprecision(1) << elapsed << "s"
                  << " | Segments: " << segments_.size() << "\n";
    }

private:
    std::mutex mutex_;
    std::vector<muninn::Segment> segments_;
    std::chrono::steady_clock::time_point playback_start_;
};

int main(int argc, char** argv) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Karaoke Captions Example\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model_path>\n";
        std::cerr << "Example: " << argv[0] << " models/faster-whisper-large-v3-turbo\n";
        return 1;
    }

    std::string model_path = argv[1];

    try {
        // Initialize streaming transcriber
        std::cout << "Loading model: " << model_path << "...\n";
        muninn::StreamingTranscriber transcriber(model_path, "cuda", "float16");

        // Configure for low latency and word timestamps
        muninn::StreamingOptions options;
        options.language = "en";
        options.chunk_length_s = 1.5f;      // Process every 1.5 seconds
        options.overlap_s = 0.3f;           // 300ms overlap
        options.enable_vad = true;          // Skip silence
        options.word_timestamps = true;     // ENABLE WORD TIMESTAMPS

        // Caption display
        KaraokeCaptionDisplay display;

        // Start streaming with callback
        std::cout << "Starting live captions with word highlighting...\n";
        std::cout << "Press Ctrl+C to stop.\n\n";

        transcriber.start(options, [&display](const muninn::Segment& seg) {
            // New segment received - update display
            display.update(seg);
            return true;  // Continue
        });

        // Simulated audio source
        SimulatedMicrophone mic;

        // Main loop: push audio and update display
        auto last_render = std::chrono::steady_clock::now();

        while (true) {
            // Get audio from microphone (1024 samples = ~21ms at 48kHz)
            auto audio_chunk = mic.get_next_chunk(1024);

            // Push to transcriber
            transcriber.push_audio(audio_chunk.data(), audio_chunk.size(), 48000, 1);

            // Render display at 30 FPS for smooth word highlighting
            auto now = std::chrono::steady_clock::now();
            auto frame_time = std::chrono::duration<float>(now - last_render).count();

            if (frame_time >= 1.0f / 30.0f) {  // 30 FPS
                display.render();
                last_render = now;
            }

            // Small sleep to prevent busy-wait
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Stop transcription
        auto final_segments = transcriber.stop();

        std::cout << "\n\nTranscription complete!\n";
        std::cout << "Total segments: " << final_segments.size() << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
