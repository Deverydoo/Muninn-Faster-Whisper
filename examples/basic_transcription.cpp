#include "muninn/transcriber.h"
#include <iostream>
#include <vector>
#include <cmath>

// Generate a simple sine wave for testing (1 second @ 16kHz)
std::vector<float> generate_test_audio(float frequency = 440.0f, float duration = 1.0f) {
    const int sample_rate = 16000;
    int num_samples = static_cast<int>(duration * sample_rate);
    std::vector<float> samples(num_samples);

    for (int i = 0; i < num_samples; i++) {
        float t = static_cast<float>(i) / sample_rate;
        samples[i] = 0.5f * std::sin(2.0f * M_PI * frequency * t);
    }

    return samples;
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Faster-Whisper - Basic Transcription Example\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    try {
        // Path to your Whisper model (adjust as needed)
        std::string model_path = "models/whisper-large-v3-turbo";

        std::cout << "[Example] Loading Whisper model...\n";
        muninn::Transcriber transcriber(model_path, "cuda", "float16");

        std::cout << "\n[Example] Generating test audio (sine wave)...\n";
        auto audio_samples = generate_test_audio(440.0f, 5.0f);  // 5 seconds of 440Hz tone

        std::cout << "[Example] Transcribing audio...\n\n";

        // Configure transcription options
        muninn::TranscribeOptions options;
        options.language = "en";
        options.beam_size = 5;
        options.vad_filter = true;
        options.compression_ratio_threshold = 2.4f;
        options.no_speech_threshold = 0.6f;

        // Transcribe
        auto result = transcriber.transcribe(audio_samples, 16000, options);

        // Display results
        std::cout << "\n═══════════════════════════════════════════════════════════\n";
        std::cout << "TRANSCRIPTION RESULT\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "Language: " << result.language << "\n";
        std::cout << "Duration: " << result.duration << "s\n";
        std::cout << "Segments: " << result.segments.size() << "\n\n";

        if (result.segments.empty()) {
            std::cout << "No speech detected (expected for sine wave test audio)\n";
        } else {
            for (const auto& segment : result.segments) {
                std::cout << "[" << segment.start << "s - " << segment.end << "s] "
                         << segment.text << "\n";
            }
        }

        std::cout << "═══════════════════════════════════════════════════════════\n\n";

        std::cout << "[Example] Next steps:\n";
        std::cout << "  1. Integrate Heimdall audio decoder for real audio files\n";
        std::cout << "  2. Test with actual speech audio\n";
        std::cout << "  3. Compare with Python faster-whisper output\n\n";

    } catch (const std::exception& e) {
        std::cerr << "[Example] ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
