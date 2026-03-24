/**
 * @file vad_tuning_app.cpp
 * @brief VAD Parameter Tuning Application for Muninn
 *
 * This standalone application iterates through different VAD types and
 * transcription settings to find optimal configurations, particularly
 * for CJK (Korean/Japanese/Chinese) content.
 *
 * Usage:
 *   vad_tuning_app.exe <model_path> <audio_file> [language]
 *
 * Example:
 *   vad_tuning_app.exe models/faster-whisper-large-v3-turbo korean_drama.mp4 ko
 *   vad_tuning_app.exe models/faster-whisper-large-v3-turbo anime.mp4 ja
 *   vad_tuning_app.exe models/faster-whisper-large-v3-turbo video.mp4 auto
 */

#include "muninn/transcriber.h"
#include "muninn/subtitle_export.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════
// Configuration Presets to Test
// ═══════════════════════════════════════════════════════════════════════════

struct VADConfig {
    std::string name;
    muninn::VADType vad_type;
    float vad_threshold;
    int min_speech_ms;
    int max_speech_s;
    int min_silence_ms;
    int speech_pad_ms;
    std::string description;
};

// Test configurations - from conservative to aggressive
std::vector<VADConfig> get_test_configs() {
    return {
        // No VAD - baseline
        {
            "no_vad",
            muninn::VADType::None,
            0.0f, 0, 30, 0, 0,
            "No VAD - process all audio (baseline)"
        },

        // Energy VAD configurations
        {
            "energy_default",
            muninn::VADType::Energy,
            0.02f,      // threshold
            250,        // min_speech_ms
            30,         // max_speech_s
            500,        // min_silence_ms
            100,        // speech_pad_ms
            "Energy VAD - default settings"
        },
        {
            "energy_sensitive",
            muninn::VADType::Energy,
            0.01f,      // lower threshold = more sensitive
            150,        // shorter min speech
            30,
            300,        // shorter silence requirement
            150,        // more padding
            "Energy VAD - sensitive (catches quiet speech)"
        },
        {
            "energy_aggressive",
            muninn::VADType::Energy,
            0.05f,      // higher threshold = more aggressive
            300,        // longer min speech
            30,
            800,        // longer silence before split
            50,         // less padding
            "Energy VAD - aggressive (removes more silence)"
        },
        {
            "energy_cjk_optimized",
            muninn::VADType::Energy,
            0.015f,     // sensitive threshold for tonal languages
            200,        // slightly shorter for rapid speech
            25,         // shorter max to split more often
            400,        // moderate silence
            200,        // generous padding for context
            "Energy VAD - optimized for CJK dialogue"
        },

        // Silero VAD configurations
        {
            "silero_default",
            muninn::VADType::Silero,
            0.5f,       // probability threshold
            250,
            30,
            100,        // Silero can use shorter silence
            100,
            "Silero VAD - default settings"
        },
        {
            "silero_sensitive",
            muninn::VADType::Silero,
            0.3f,       // lower threshold = more sensitive
            150,
            30,
            80,
            150,
            "Silero VAD - sensitive"
        },
        {
            "silero_aggressive",
            muninn::VADType::Silero,
            0.7f,       // higher threshold = more confident speech only
            300,
            30,
            150,
            50,
            "Silero VAD - aggressive"
        },
        {
            "silero_cjk_optimized",
            muninn::VADType::Silero,
            0.4f,       // balanced threshold
            180,        // shorter for rapid Korean/Japanese
            20,         // more frequent splits
            100,
            200,        // generous padding
            "Silero VAD - optimized for CJK dialogue"
        },

        // TEN VAD configurations
        {
            "ten_default",
            muninn::VADType::TEN,
            0.5f,
            250,
            30,
            100,
            100,
            "TEN VAD - default settings"
        },
        {
            "ten_sensitive",
            muninn::VADType::TEN,
            0.3f,
            150,
            30,
            80,
            150,
            "TEN VAD - sensitive"
        },

        // Auto VAD (system decides)
        {
            "auto_default",
            muninn::VADType::Auto,
            0.02f,
            250,
            30,
            500,
            100,
            "Auto VAD - system selects best algorithm"
        },
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Transcription Settings Presets
// ═══════════════════════════════════════════════════════════════════════════

struct TranscribeConfig {
    std::string name;
    int beam_size;
    float temperature;
    float compression_ratio_threshold;
    float no_speech_threshold;
    bool word_timestamps;
    std::string description;
};

std::vector<TranscribeConfig> get_transcribe_configs() {
    return {
        {
            "default",
            5, 0.0f, 2.4f, 0.4f, true,
            "Default transcription settings"
        },
        {
            "quality",
            8, 0.0f, 2.0f, 0.3f, true,
            "Higher quality (larger beam, stricter filtering)"
        },
        {
            "fast",
            3, 0.0f, 2.8f, 0.5f, false,
            "Faster processing (smaller beam, looser filtering)"
        },
        {
            "cjk_optimized",
            5, 0.0f, 3.0f, 0.5f, true,
            "Optimized for CJK (looser compression for longer characters)"
        },
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Result Tracking
// ═══════════════════════════════════════════════════════════════════════════

struct TestResult {
    std::string vad_config;
    std::string transcribe_config;
    std::string language;

    int segment_count;
    float audio_duration;
    float processing_time_s;
    float rtf;  // Real-time factor

    int total_chars;
    int total_words;
    float avg_segment_duration;
    float avg_chars_per_segment;

    std::string output_file;
    bool success;
    std::string error_message;
};

// ═══════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════

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

void save_transcript(const std::string& output_path,
                    const muninn::TranscribeResult& result,
                    const VADConfig& vad_cfg,
                    const TranscribeConfig& trans_cfg) {
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open output file: " << output_path << "\n";
        return;
    }

    file << "=== VAD TUNING TEST RESULT ===\n";
    file << "VAD Config: " << vad_cfg.name << " - " << vad_cfg.description << "\n";
    file << "Transcribe Config: " << trans_cfg.name << " - " << trans_cfg.description << "\n";
    file << "Language: " << result.language << "\n";
    file << "Duration: " << std::fixed << std::setprecision(2) << result.duration << "s\n";
    file << "Segments: " << result.segments.size() << "\n";
    file << "=================================\n\n";

    for (const auto& segment : result.segments) {
        file << "[" << format_timestamp(segment.start) << "] " << segment.text << "\n";
    }

    file.close();
}

void save_summary_report(const std::string& output_dir,
                        const std::vector<TestResult>& results) {
    std::string report_path = output_dir + "/vad_tuning_report.txt";
    std::ofstream file(report_path);

    file << "═══════════════════════════════════════════════════════════════════════════\n";
    file << "VAD TUNING COMPREHENSIVE REPORT\n";
    file << "═══════════════════════════════════════════════════════════════════════════\n\n";

    // Summary table
    file << std::left << std::setw(25) << "VAD Config"
         << std::setw(15) << "Transcribe"
         << std::setw(10) << "Segments"
         << std::setw(12) << "Chars"
         << std::setw(12) << "Avg Seg(s)"
         << std::setw(10) << "RTF"
         << "Status\n";
    file << std::string(94, '-') << "\n";

    for (const auto& r : results) {
        file << std::left << std::setw(25) << r.vad_config
             << std::setw(15) << r.transcribe_config
             << std::setw(10) << r.segment_count
             << std::setw(12) << r.total_chars
             << std::setw(12) << std::fixed << std::setprecision(2) << r.avg_segment_duration
             << std::setw(10) << std::fixed << std::setprecision(2) << r.rtf
             << (r.success ? "OK" : "FAIL") << "\n";
    }

    file << "\n═══════════════════════════════════════════════════════════════════════════\n";
    file << "DETAILED RESULTS\n";
    file << "═══════════════════════════════════════════════════════════════════════════\n\n";

    for (const auto& r : results) {
        file << "--- " << r.vad_config << " + " << r.transcribe_config << " ---\n";
        file << "  Output: " << r.output_file << "\n";
        file << "  Language: " << r.language << "\n";
        file << "  Audio Duration: " << r.audio_duration << "s\n";
        file << "  Processing Time: " << r.processing_time_s << "s\n";
        file << "  RTF: " << r.rtf << "x\n";
        file << "  Segments: " << r.segment_count << "\n";
        file << "  Total Chars: " << r.total_chars << "\n";
        file << "  Avg Segment Duration: " << r.avg_segment_duration << "s\n";
        file << "  Avg Chars/Segment: " << r.avg_chars_per_segment << "\n";
        if (!r.success) {
            file << "  ERROR: " << r.error_message << "\n";
        }
        file << "\n";
    }

    // Recommendations
    file << "═══════════════════════════════════════════════════════════════════════════\n";
    file << "RECOMMENDATIONS\n";
    file << "═══════════════════════════════════════════════════════════════════════════\n\n";

    // Find best by segment count (more segments = finer granularity)
    auto max_segments = std::max_element(results.begin(), results.end(),
        [](const TestResult& a, const TestResult& b) { return a.segment_count < b.segment_count; });

    // Find best by total chars (more chars = less truncation)
    auto max_chars = std::max_element(results.begin(), results.end(),
        [](const TestResult& a, const TestResult& b) { return a.total_chars < b.total_chars; });

    // Find fastest
    auto fastest = std::min_element(results.begin(), results.end(),
        [](const TestResult& a, const TestResult& b) { return a.rtf < b.rtf; });

    if (max_segments != results.end() && max_segments->success) {
        file << "Most Segments: " << max_segments->vad_config << " + " << max_segments->transcribe_config
             << " (" << max_segments->segment_count << " segments)\n";
    }
    if (max_chars != results.end() && max_chars->success) {
        file << "Most Content: " << max_chars->vad_config << " + " << max_chars->transcribe_config
             << " (" << max_chars->total_chars << " chars)\n";
    }
    if (fastest != results.end() && fastest->success) {
        file << "Fastest: " << fastest->vad_config << " + " << fastest->transcribe_config
             << " (" << fastest->rtf << "x RTF)\n";
    }

    file.close();
    std::cout << "\n[Report] Summary saved to: " << report_path << "\n";
}

// ═══════════════════════════════════════════════════════════════════════════
// Main Test Runner
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    std::cout << "═══════════════════════════════════════════════════════════════════════════\n";
    std::cout << "Muninn VAD Parameter Tuning Application\n";
    std::cout << "Version: 1.0.0\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════\n\n";

    // Parse arguments
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <model_path> <audio_file> [language]\n";
        std::cout << "\n";
        std::cout << "Arguments:\n";
        std::cout << "  model_path   Path to Whisper model (e.g., models/faster-whisper-large-v3-turbo)\n";
        std::cout << "  audio_file   Audio/video file to transcribe\n";
        std::cout << "  language     Language code (ko, ja, zh, en, auto) [default: auto]\n";
        std::cout << "\n";
        std::cout << "Example:\n";
        std::cout << "  " << argv[0] << " models/faster-whisper-large-v3-turbo korean_drama.mp4 ko\n";
        return 1;
    }

    std::string model_path = argv[1];
    std::string audio_path = argv[2];
    std::string language = (argc > 3) ? argv[3] : "auto";

    // Create output directory
    fs::path audio_fs_path(audio_path);
    std::string output_dir = audio_fs_path.parent_path().string() + "/vad_tuning_" +
                            audio_fs_path.stem().string();
    fs::create_directories(output_dir);

    std::cout << "[Config] Model: " << model_path << "\n";
    std::cout << "[Config] Audio: " << audio_path << "\n";
    std::cout << "[Config] Language: " << language << "\n";
    std::cout << "[Config] Output: " << output_dir << "\n\n";

    // Get test configurations
    auto vad_configs = get_test_configs();
    auto transcribe_configs = get_transcribe_configs();

    // For faster testing, use only default transcribe config
    // Comment this out for full matrix test
    transcribe_configs = { transcribe_configs[0] };  // default only

    int total_tests = vad_configs.size() * transcribe_configs.size();
    std::cout << "[Info] Running " << total_tests << " test configurations\n";
    std::cout << "[Info] VAD configs: " << vad_configs.size() << "\n";
    std::cout << "[Info] Transcribe configs: " << transcribe_configs.size() << "\n\n";

    // Load model once
    std::cout << "[Muninn] Loading model...\n";
    std::unique_ptr<muninn::Transcriber> transcriber;

    try {
        transcriber = std::make_unique<muninn::Transcriber>(model_path, "cuda", "float16");
        auto model_info = transcriber->get_model_info();
        std::cout << "[Muninn] Model loaded: "
                  << (model_info.is_multilingual ? "Multilingual" : "English-only")
                  << ", " << model_info.num_languages << " languages\n\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to load model: " << e.what() << "\n";
        return 1;
    }

    // Run tests
    std::vector<TestResult> results;
    int test_num = 0;

    for (const auto& vad_cfg : vad_configs) {
        for (const auto& trans_cfg : transcribe_configs) {
            test_num++;
            std::cout << "═══════════════════════════════════════════════════════════════════════════\n";
            std::cout << "[Test " << test_num << "/" << total_tests << "] "
                      << vad_cfg.name << " + " << trans_cfg.name << "\n";
            std::cout << "  VAD: " << vad_cfg.description << "\n";
            std::cout << "  Transcribe: " << trans_cfg.description << "\n";
            std::cout << "═══════════════════════════════════════════════════════════════════════════\n";

            TestResult result;
            result.vad_config = vad_cfg.name;
            result.transcribe_config = trans_cfg.name;
            result.success = false;

            try {
                // Configure options
                muninn::TranscribeOptions options;
                options.language = language;

                // VAD settings
                options.vad_type = vad_cfg.vad_type;
                options.vad_filter = (vad_cfg.vad_type != muninn::VADType::None);
                options.vad_threshold = vad_cfg.vad_threshold;
                options.vad_min_speech_duration_ms = vad_cfg.min_speech_ms;
                options.vad_max_speech_duration_s = vad_cfg.max_speech_s;
                options.vad_min_silence_duration_ms = vad_cfg.min_silence_ms;
                options.vad_speech_pad_ms = vad_cfg.speech_pad_ms;

                // Silero model path (if needed)
                options.silero_model_path = model_path + "/../silero_vad.onnx";
                options.ten_vad_model_path = model_path + "/../ten-vad.onnx";

                // Transcription settings
                options.beam_size = trans_cfg.beam_size;
                options.temperature = trans_cfg.temperature;
                options.compression_ratio_threshold = trans_cfg.compression_ratio_threshold;
                options.no_speech_threshold = trans_cfg.no_speech_threshold;
                options.word_timestamps = trans_cfg.word_timestamps;

                // Diarization disabled for speed
                options.enable_diarization = false;

                // Run transcription
                auto start_time = std::chrono::high_resolution_clock::now();
                auto transcribe_result = transcriber->transcribe(audio_path, options);
                auto end_time = std::chrono::high_resolution_clock::now();

                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                float processing_time_s = duration_ms.count() / 1000.0f;

                // Calculate metrics
                result.segment_count = transcribe_result.segments.size();
                result.audio_duration = transcribe_result.duration;
                result.processing_time_s = processing_time_s;
                result.rtf = (transcribe_result.duration > 0) ?
                            (processing_time_s / transcribe_result.duration) : 0.0f;
                result.language = transcribe_result.language;

                int total_chars = 0;
                int total_words = 0;
                float total_segment_duration = 0.0f;

                for (const auto& seg : transcribe_result.segments) {
                    total_chars += seg.text.length();
                    total_segment_duration += (seg.end - seg.start);
                    // Simple word count (space-separated for Latin, char-based for CJK)
                    if (language == "ko" || language == "ja" || language == "zh") {
                        total_words += seg.text.length();  // CJK: chars ~ words
                    } else {
                        total_words += std::count(seg.text.begin(), seg.text.end(), ' ') + 1;
                    }
                }

                result.total_chars = total_chars;
                result.total_words = total_words;
                result.avg_segment_duration = (result.segment_count > 0) ?
                    (total_segment_duration / result.segment_count) : 0.0f;
                result.avg_chars_per_segment = (result.segment_count > 0) ?
                    (static_cast<float>(total_chars) / result.segment_count) : 0.0f;

                // Save transcript
                std::string output_file = output_dir + "/" + vad_cfg.name + "_" +
                                         trans_cfg.name + ".txt";
                save_transcript(output_file, transcribe_result, vad_cfg, trans_cfg);
                result.output_file = output_file;
                result.success = true;

                // Print summary
                std::cout << "\n[Result] Segments: " << result.segment_count
                          << ", Chars: " << total_chars
                          << ", RTF: " << std::fixed << std::setprecision(2) << result.rtf << "x\n";
                std::cout << "[Result] Avg segment: " << result.avg_segment_duration << "s\n";
                std::cout << "[Result] Saved to: " << output_file << "\n\n";

                // Clear GPU cache between tests
                transcriber->clear_cache();

            } catch (const std::exception& e) {
                result.error_message = e.what();
                std::cerr << "[ERROR] Test failed: " << e.what() << "\n\n";
            }

            results.push_back(result);
        }
    }

    // Generate summary report
    save_summary_report(output_dir, results);

    // Print final summary
    std::cout << "\n═══════════════════════════════════════════════════════════════════════════\n";
    std::cout << "VAD TUNING COMPLETE\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════\n";
    std::cout << "Tests run: " << results.size() << "\n";

    int success_count = std::count_if(results.begin(), results.end(),
        [](const TestResult& r) { return r.success; });
    std::cout << "Successful: " << success_count << "\n";
    std::cout << "Failed: " << (results.size() - success_count) << "\n";
    std::cout << "Output directory: " << output_dir << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════\n";

    return 0;
}
