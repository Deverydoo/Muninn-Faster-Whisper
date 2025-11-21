#include "muninn/transcriber.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <model_path> <audio_file>\n";
    std::cout << "\n";
    std::cout << "Example:\n";
    std::cout << "  " << program_name << " models/whisper-large-v3-turbo audio.mp3\n";
    std::cout << "\n";
    std::cout << "Note: Audio file loading not yet implemented.\n";
    std::cout << "      Currently requires passing audio samples programmatically.\n";
}

void save_transcript(const std::string& output_path, const muninn::TranscribeResult& result) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        return;
    }

    // Write header
    file << "═══════════════════════════════════════════════════════════\n";
    file << "Muninn Faster-Whisper Transcription\n";
    file << "═══════════════════════════════════════════════════════════\n";
    file << "Language: " << result.language << "\n";
    file << "Duration: " << std::fixed << std::setprecision(2) << result.duration << "s\n";
    file << "Segments: " << result.segments.size() << "\n";
    file << "═══════════════════════════════════════════════════════════\n\n";

    // Write segments with timestamps
    for (const auto& segment : result.segments) {
        // Format timestamp as [HH:MM:SS]
        int hours = static_cast<int>(segment.start) / 3600;
        int minutes = (static_cast<int>(segment.start) % 3600) / 60;
        int seconds = static_cast<int>(segment.start) % 60;

        file << "[" << std::setfill('0') << std::setw(2) << hours << ":"
             << std::setw(2) << minutes << ":"
             << std::setw(2) << seconds << "] "
             << segment.text << "\n";
    }

    std::cout << "\n[Muninn] Transcript saved to: " << output_path << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Faster-Whisper Test Application\n";
    std::cout << "Version: 0.5.0-alpha\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string model_path = argv[1];
    std::string audio_path = argv[2];

    try {
        // Initialize transcriber
        std::cout << "[Muninn] Loading model from: " << model_path << "\n";
        muninn::Transcriber transcriber(model_path, "cuda", "float16");

        // Get model info
        auto model_info = transcriber.get_model_info();
        std::cout << "\n[Muninn] Model Information:\n";
        std::cout << "  Multilingual: " << (model_info.is_multilingual ? "Yes" : "No") << "\n";
        std::cout << "  Languages: " << model_info.num_languages << "\n";
        std::cout << "  Mel bins: " << model_info.n_mels << "\n\n";

        // TODO: Load audio file via Heimdall
        std::cerr << "[Muninn] ERROR: Audio file loading not yet implemented!\n";
        std::cerr << "[Muninn] Next step: Integrate Heimdall audio decoder DLL\n";
        std::cerr << "[Muninn] For now, pass audio samples programmatically:\n\n";
        std::cerr << "  std::vector<float> samples = load_audio(\"" << audio_path << "\");\n";
        std::cerr << "  auto result = transcriber.transcribe(samples, 16000);\n\n";

        return 1;

        // Example of how to use once audio loading is implemented:
        /*
        std::vector<float> audio_samples = load_audio(audio_path);

        muninn::TranscribeOptions options;
        options.language = "en";
        options.vad_filter = true;
        options.compression_ratio_threshold = 2.4f;
        options.no_speech_threshold = 0.6f;

        std::cout << "[Muninn] Starting transcription...\n\n";
        auto start_time = std::chrono::high_resolution_clock::now();

        auto result = transcriber.transcribe(audio_samples, 16000, options);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n[Muninn] Transcription complete!\n";
        std::cout << "[Muninn] Duration: " << result.duration << "s\n";
        std::cout << "[Muninn] Segments: " << result.segments.size() << "\n";
        std::cout << "[Muninn] Processing time: " << (duration.count() / 1000.0) << "s\n";
        std::cout << "[Muninn] Real-time factor: " << (duration.count() / 1000.0 / result.duration) << "x\n\n";

        // Print transcript
        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "TRANSCRIPT\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";
        for (const auto& segment : result.segments) {
            std::cout << segment.text << "\n";
        }
        std::cout << "═══════════════════════════════════════════════════════════\n";

        // Save to file
        std::string output_path = audio_path + ".transcript.txt";
        save_transcript(output_path, result);
        */

    } catch (const std::exception& e) {
        std::cerr << "[Muninn] ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
