/**
 * @file translation_streaming.cpp
 * @brief Example: Real-time translation for live streaming
 *
 * This example demonstrates Muninn's instant translation mode:
 * - Detect any language automatically
 * - Translate to English in real-time
 * - Display bilingual captions (original + translation)
 * - Perfect for international live streams
 *
 * Use cases:
 * - Live streaming to global audience
 * - International conferences/meetings
 * - Multilingual gaming streams
 * - Language learning content
 */

#include <muninn/streaming_transcriber.h>
#include <muninn/word_styling.h>
#include <iostream>
#include <iomanip>

/**
 * @brief Display bilingual captions with styling
 */
void render_bilingual_caption(const muninn::Segment& seg, bool show_language_tag = true) {
    // Language tag
    if (show_language_tag && !seg.language.empty() && seg.language != "en") {
        std::cout << "\033[2m[" << seg.language << " → en]\033[0m ";
    }

    // Translated English text (main display)
    std::cout << "\033[1;97m" << seg.text << "\033[0m" << std::endl;

    // Show timing
    std::cout << "\033[2m(" << std::fixed << std::setprecision(1)
              << seg.start << "s - " << seg.end << "s)\033[0m" << std::endl;
    std::cout << std::endl;
}

/**
 * @brief Build HTML for OBS with bilingual captions
 */
std::string build_bilingual_obs_html(const muninn::Segment& seg) {
    std::ostringstream html;

    // English translation (large, bold, white)
    html << "<font size='+2' color='#FFFFFF'><b>";
    html << seg.text;
    html << "</b></font>";

    // Language indicator (small, dim, if not English)
    if (!seg.language.empty() && seg.language != "en") {
        html << "<br><font size='-1' color='#888888'>";
        html << "(" << seg.language << " → en)";
        html << "</font>";
    }

    return html.str();
}

/**
 * @brief Language name lookup for better UX
 */
std::string get_language_name(const std::string& code) {
    static const std::map<std::string, std::string> names = {
        {"en", "English"}, {"es", "Spanish"}, {"fr", "French"},
        {"de", "German"}, {"it", "Italian"}, {"pt", "Portuguese"},
        {"ru", "Russian"}, {"ja", "Japanese"}, {"ko", "Korean"},
        {"zh", "Chinese"}, {"ar", "Arabic"}, {"hi", "Hindi"},
        {"nl", "Dutch"}, {"pl", "Polish"}, {"tr", "Turkish"}
    };

    auto it = names.find(code);
    return it != names.end() ? it->second : code;
}

int main(int argc, char** argv) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Real-Time Translation Example\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model_path> [audio_file]\n";
        std::cerr << "Example: " << argv[0] << " models/faster-whisper-large-v3-turbo\n";
        return 1;
    }

    std::string model_path = argv[1];

    try {
        // Initialize streaming transcriber
        std::cout << "Loading model: " << model_path << "...\n";
        muninn::StreamingTranscriber transcriber(model_path, "cuda", "float16");

        // Configure for real-time translation
        muninn::StreamingOptions options;
        options.language = "auto";          // Auto-detect any language
        options.task = "translate";         // TRANSLATE to English!
        options.chunk_length_s = 1.5f;      // Low latency
        options.overlap_s = 0.3f;
        options.enable_vad = true;          // Skip silence
        options.word_timestamps = true;     // For karaoke highlighting

        std::cout << "\n";
        std::cout << "Configuration:\n";
        std::cout << "  Mode: Real-time Translation (any language → English)\n";
        std::cout << "  Latency: " << options.chunk_length_s << "s\n";
        std::cout << "  VAD: Enabled\n";
        std::cout << "  Word Timestamps: Enabled\n";
        std::cout << "\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "Starting live translation...\n";
        std::cout << "Speak in ANY language - it will be translated to English!\n";
        std::cout << "═══════════════════════════════════════════════════════════\n\n";

        // Track detected languages
        std::set<std::string> detected_languages;

        // Start streaming with callback
        transcriber.start(options, [&](const muninn::Segment& seg) {
            // Track language
            if (!seg.language.empty() && seg.language != "auto") {
                if (detected_languages.insert(seg.language).second) {
                    // New language detected!
                    std::cout << "\033[1;33m✓ Detected: "
                              << get_language_name(seg.language)
                              << "\033[0m\n\n";
                }
            }

            // Display bilingual caption
            render_bilingual_caption(seg, true);

            // Optional: Show word-level timing for English
            if (seg.words.size() > 0 && seg.words.size() <= 10) {
                std::cout << "\033[2mWords: ";
                for (const auto& word : seg.words) {
                    std::cout << word.word << " ";
                }
                std::cout << "\033[0m\n\n";
            }

            return true;  // Continue
        });

        // Simulated audio input
        // In real use: replace with microphone/OBS audio capture
        std::cout << "Note: This is a demo. Connect real audio source to use.\n\n";

        // For demo purposes, just wait for user interrupt
        std::cout << "Press Ctrl+C to stop.\n";
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Stop transcription
        auto final_segments = transcriber.stop();

        std::cout << "\n\n═══════════════════════════════════════════════════════════\n";
        std::cout << "Translation complete!\n";
        std::cout << "Total segments: " << final_segments.size() << "\n";
        std::cout << "Languages detected: ";
        for (const auto& lang : detected_languages) {
            std::cout << get_language_name(lang) << " ";
        }
        std::cout << "\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
