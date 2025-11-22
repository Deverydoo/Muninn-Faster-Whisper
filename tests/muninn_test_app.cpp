#include "muninn/transcriber.h"
#include "muninn/subtitle_export.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>

// Helper: Auto-detect best VAD for a track based on audio characteristics
muninn::VADType detect_best_vad(const std::vector<float>& samples, int track_id, int total_tracks) {
    // Multi-track: Track 0 (desktop/game audio) usually has mixed content
    if (total_tracks > 1 && track_id == 0) {
        std::cout << "[Auto-VAD] Track " << track_id << ": Multi-track game audio detected → Energy VAD\n";
        return muninn::VADType::Energy;
    }

    // Single track or microphone tracks: Analyze noise characteristics
    std::vector<float> abs_samples;
    abs_samples.reserve(samples.size());
    for (float s : samples) {
        abs_samples.push_back(std::abs(s));
    }

    // Sample every 1000th value for speed
    std::vector<float> sampled;
    for (size_t i = 0; i < abs_samples.size(); i += 1000) {
        sampled.push_back(abs_samples[i]);
    }
    std::sort(sampled.begin(), sampled.end());

    if (sampled.empty()) {
        std::cout << "[Auto-VAD] Track " << track_id << ": Empty audio → Energy VAD\n";
        return muninn::VADType::Energy;
    }

    // Estimate noise floor (10th percentile) and speech level (90th percentile)
    size_t p10 = static_cast<size_t>(sampled.size() * 0.1);
    size_t p90 = static_cast<size_t>(sampled.size() * 0.9);
    float noise_floor = sampled[p10];
    float speech_level = sampled[p90];
    float dynamic_range = speech_level - noise_floor;

    std::cout << "[Auto-VAD] Track " << track_id << ": Noise floor=" << noise_floor
              << ", Speech level=" << speech_level << ", Dynamic range=" << dynamic_range << "\n";

    // Decision logic
    if (dynamic_range > 0.15f && noise_floor < 0.01f) {
        std::cout << "[Auto-VAD] Track " << track_id << ": Clean speech detected → Silero VAD\n";
        return muninn::VADType::Silero;
    } else {
        std::cout << "[Auto-VAD] Track " << track_id << ": Mixed/noisy content detected → Energy VAD\n";
        return muninn::VADType::Energy;
    }
}

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

        // Format: [HH:MM:SS.mmm] [Speaker X] text (if speaker detected)
        file << "[" << format_timestamp(segment.start) << "] ";
        if (segment.speaker_id >= 0 && !segment.speaker_label.empty()) {
            file << "[" << segment.speaker_label << "] ";
        }
        file << segment.text << "\n";

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

    // Parse command-line arguments or use defaults
    std::string model_path = (argc > 1) ? argv[1] : "models/faster-whisper-large-v3-turbo";
    std::string audio_path = (argc > 2) ? argv[2] : "test.mp4";

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
        // Use auto-detection to select best VAD per track
        options.vad_type = muninn::VADType::Auto;
        // Silero model path (needed if auto-detection chooses Silero)
        std::string silero_path = model_path + "/../silero_vad.onnx";
        options.silero_model_path = silero_path;
        options.word_timestamps = false;  // Word-level timestamps (off by default)

        // ═══════════════════════════════════════════════════════════
        // Speaker Diarization (optional)
        // ═══════════════════════════════════════════════════════════
        // NOTE: Requires ONNX Runtime providers DLLs and pyannote embedding model
        // Set to true to enable speaker detection
        options.enable_diarization = false;  // Disabled by default (opt-in feature)
        std::string diarization_path = model_path + "/../pyannote_embedding.onnx";
        options.diarization_model_path = diarization_path;
        options.diarization_threshold = 0.5f;  // Lower threshold = fewer speakers (0.5-0.9)
        options.diarization_min_speakers = 1;
        options.diarization_max_speakers = 5;   // Cap at 5 speakers for testing

        std::cout << "[Muninn] Using auto-detection to select best VAD per track\n";
        if (options.enable_diarization) {
            std::cout << "[Muninn] Speaker diarization: ENABLED\n";
        }

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

            // Show speaker label if available
            if (segment.speaker_id >= 0 && !segment.speaker_label.empty()) {
                std::cout << "[" << format_timestamp(segment.start) << "] "
                          << "[" << segment.speaker_label << "] " << segment.text << "\n";
            } else {
                std::cout << "[" << format_timestamp(segment.start) << "] " << segment.text << "\n";
            }
        }
        std::cout << "═══════════════════════════════════════════════════════════\n";

        // Save to file
        std::string output_path = audio_path + ".transcript.txt";
        save_transcript(output_path, result);

        // ═══════════════════════════════════════════════════════════
        // Export subtitles and metadata
        // ═══════════════════════════════════════════════════════════
        std::cout << "\n[Muninn] Exporting subtitles and metadata...\n";

        muninn::SubtitleExporter exporter;

        // Export SRT (native language)
        try {
            std::string srt_path = exporter.export_srt(result.segments, audio_path);
            std::cout << "[Muninn] ✓ Created SRT: " << srt_path << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[Muninn] Failed to export SRT: " << e.what() << "\n";
        }

        // Export metadata JSON (Loki Studio format)
        try {
            std::string json_path = muninn::SubtitleMetadata::generate_metadata_json(
                result.segments,
                audio_path,
                "large-v3-turbo",
                result.language,
                result.duration
            );
            std::cout << "[Muninn] ✓ Created metadata JSON: " << json_path << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[Muninn] Failed to export metadata JSON: " << e.what() << "\n";
        }

        // ═══════════════════════════════════════════════════════════
        // Translation test (translate to Japanese)
        // ═══════════════════════════════════════════════════════════
        if (result.language != "ja") {
            std::cout << "\n[Muninn] Translating to Japanese...\n";

            muninn::TranscribeOptions translate_options;
            translate_options.task = "translate";  // Translation mode
            translate_options.language = "ja";     // Target: Japanese
            translate_options.beam_size = 5;
            translate_options.temperature = 0.0f;
            translate_options.vad_filter = true;
            translate_options.vad_type = muninn::VADType::Auto;
            translate_options.silero_model_path = silero_path;

            auto translate_start = std::chrono::high_resolution_clock::now();
            auto translated_result = transcriber.transcribe(audio_path, translate_options);
            auto translate_end = std::chrono::high_resolution_clock::now();
            auto translate_duration = std::chrono::duration_cast<std::chrono::milliseconds>(translate_end - translate_start);

            std::cout << "[Muninn] Translation complete! (" << (translate_duration.count() / 1000.0) << "s)\n";

            // Export translated SRT
            try {
                muninn::SubtitleExportOptions srt_opts;
                srt_opts.output_path = audio_path + ".ja.srt";  // Custom output with language code
                std::string ja_srt_path = exporter.export_srt(translated_result.segments, audio_path, srt_opts);
                std::cout << "[Muninn] ✓ Created Japanese SRT: " << ja_srt_path << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[Muninn] Failed to export Japanese SRT: " << e.what() << "\n";
            }

            // Show first few translated segments
            std::cout << "\n[Muninn] Sample translation (Japanese):\n";
            for (size_t i = 0; i < std::min(size_t(5), translated_result.segments.size()); ++i) {
                std::cout << "  " << translated_result.segments[i].text << "\n";
            }
        }

        std::cout << "\n[Muninn] All exports complete!\n";

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
