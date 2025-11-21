#include "muninn/transcriber.h"
#include <iostream>
#include <fstream>
#include <sstream>
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

std::string format_timestamp(float seconds) {
    int hours = static_cast<int>(seconds) / 3600;
    int minutes = (static_cast<int>(seconds) % 3600) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setw(2) << minutes << ":"
       << std::setw(2) << secs << "."
       << std::setw(3) << ms;
    return ss.str();
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

    // Group segments by track
    int current_track = -1;
    for (const auto& segment : result.segments) {
        // Print track header when track changes
        if (segment.track_id != current_track) {
            if (current_track != -1) {
                file << "\n";  // Blank line between tracks
            }
            file << "[Track " << segment.track_id << "]\n";
            current_track = segment.track_id;
        }

        // Format: [HH:MM:SS.mmm] text
        file << "[" << format_timestamp(segment.start) << "] " << segment.text << "\n";

        // Show word timestamps if available
        if (!segment.words.empty()) {
            file << "    Words: ";
            for (size_t i = 0; i < segment.words.size(); ++i) {
                const auto& w = segment.words[i];
                file << "[" << w.start << "s] " << w.word;
                if (i < segment.words.size() - 1) file << " ";
            }
            file << "\n";
        }
    }

    std::cout << "\n[Muninn] Transcript saved to: " << output_path << "\n";
}

int main(int argc, char* argv[]) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Faster-Whisper Test Application\n";
    std::cout << "Version: 0.5.0-alpha\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";
    std::cout.flush();

    // Hardcoded test paths (relative to executable location)
    std::string model_path = "models/faster-whisper-large-v3-turbo";
    std::string audio_path = "test.mp4";

    std::cout << "[Test] Model: " << model_path << "\n";
    std::cout << "[Test] Audio: " << audio_path << "\n\n";
    std::cout.flush();

    std::cout << "[DEBUG] Step 1: Entering try block\n";
    std::cout.flush();

    try {
        // Initialize transcriber
        std::cout << "[DEBUG] Step 2: About to construct Transcriber...\n";
        std::cout.flush();

        std::cout << "[Muninn] Loading model...\n";
        std::cout.flush();

        muninn::Transcriber transcriber(model_path, "cuda", "float16");

        std::cout << "[DEBUG] Step 3: Transcriber constructed successfully\n";
        std::cout.flush();

        // Get model info
        std::cout << "[DEBUG] Step 4: Getting model info...\n";
        std::cout.flush();

        auto model_info = transcriber.get_model_info();
        std::cout << "\n[Muninn] Model Information:\n";
        std::cout << "  Multilingual: " << (model_info.is_multilingual ? "Yes" : "No") << "\n";
        std::cout << "  Languages: " << model_info.num_languages << "\n";
        std::cout << "  Mel bins: " << model_info.n_mels << "\n\n";
        std::cout.flush();

        // Transcribe audio file
        std::cout << "[DEBUG] Step 5: Setting up transcription options...\n";
        std::cout.flush();

        std::cout << "[Muninn] Starting transcription...\n\n";
        std::cout.flush();

        muninn::TranscribeOptions options;
        options.language = "auto";  // Auto-detect language
        options.beam_size = 5;
        options.temperature = 0.0f;
        options.vad_filter = true;  // Enable VAD filtering
        options.vad_type = muninn::VADType::Silero;
        options.silero_model_path = "models/silero_vad.onnx";
        options.word_timestamps = false;  // Word-level timestamps (off by default)

        std::cout << "[DEBUG] Step 6: About to call transcribe()...\n";
        std::cout.flush();

        auto start_time = std::chrono::high_resolution_clock::now();
        auto result = transcriber.transcribe(audio_path, options);
        auto end_time = std::chrono::high_resolution_clock::now();

        std::cout << "[DEBUG] Step 7: Transcription completed\n";
        std::cout.flush();

        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n[Muninn] Transcription complete!\n";
        std::cout << "[Muninn] Audio duration: " << result.duration << "s\n";
        std::cout << "[Muninn] Segments: " << result.segments.size() << "\n";
        std::cout << "[Muninn] Processing time: " << (duration_ms.count() / 1000.0) << "s\n";
        if (result.duration > 0) {
            std::cout << "[Muninn] Real-time factor: " << (duration_ms.count() / 1000.0 / result.duration) << "x\n";
        }

        // Print transcript grouped by track
        std::cout << "\n═══════════════════════════════════════════════════════════\n";
        std::cout << "TRANSCRIPT\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";
        int current_track = -1;
        for (const auto& segment : result.segments) {
            if (segment.track_id != current_track) {
                if (current_track != -1) std::cout << "\n";
                std::cout << "[Track " << segment.track_id << "]\n";
                current_track = segment.track_id;
            }
            std::cout << "[" << format_timestamp(segment.start) << "] " << segment.text << "\n";
        }
        std::cout << "═══════════════════════════════════════════════════════════\n";

        // Save to file
        std::string output_path = audio_path + ".transcript.txt";
        save_transcript(output_path, result);

        return 0;

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
