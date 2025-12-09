/**
 * @file test_translation.cpp
 * @brief Test transcription and translation pipeline
 */

#include <muninn/transcriber.h>
#include <muninn/translator.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Global output stream - either stdout or file
std::ostream* g_out = &std::cout;
std::ofstream g_file_out;

// Enable UTF-8 console output on Windows
void enable_utf8_console() {
#ifdef _WIN32
    // Set console output to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    // Also set input code page
    SetConsoleCP(CP_UTF8);
#endif
}

// Write UTF-8 BOM to file stream
void write_utf8_bom(std::ofstream& file) {
    // UTF-8 BOM: EF BB BF - helps editors recognize UTF-8 encoding
    unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write(reinterpret_cast<char*>(bom), 3);
}

std::string format_time(float seconds) {
    int mins = static_cast<int>(seconds) / 60;
    float secs = seconds - mins * 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%05.2f", mins, secs);
    return buf;
}

int main(int argc, char* argv[]) {
    // Enable UTF-8 console output for Cyrillic, CJK, etc.
    enable_utf8_console();

    std::string audio_file;
    std::string whisper_model = "models/faster-whisper-large-v3-turbo";
    std::string nllb_model = "models/nllb-200-distilled-600M";
    std::string output_file;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_file> [whisper_model] [nllb_model] [output_file]\n";
        std::cerr << "\nIf output_file is specified, results are written to that file with proper UTF-8 encoding.\n";
        return 1;
    }

    audio_file = argv[1];
    if (argc > 2) whisper_model = argv[2];
    if (argc > 3) nllb_model = argv[3];
    if (argc > 4) output_file = argv[4];

    // Set up output stream - file or stdout
    if (!output_file.empty()) {
        g_file_out.open(output_file, std::ios::out | std::ios::binary);
        if (!g_file_out.is_open()) {
            std::cerr << "Error: Could not open output file: " << output_file << "\n";
            return 1;
        }
        write_utf8_bom(g_file_out);
        g_out = &g_file_out;
        std::cout << "Writing output to: " << output_file << " (UTF-8 encoded)\n";
    }

    // Target languages for translation
    std::vector<std::pair<std::string, std::string>> target_langs = {
        {"es", "Spanish"},
        {"fr", "French"},
        {"de", "German"},
        {"ja", "Japanese"},
        {"zh", "Chinese"},
        {"ru", "Russian"},
        {"ko", "Korean"},
        {"pt", "Portuguese"},
    };

    try {
        *g_out << "\n";
        *g_out << "============================================================\n";
        *g_out << "  Muninn Transcription + Translation Test\n";
        *g_out << "============================================================\n\n";

        // Check files exist
        if (!fs::exists(audio_file)) {
            std::cerr << "Error: Audio file not found: " << audio_file << "\n";
            return 1;
        }
        *g_out << "Audio file: " << audio_file << "\n";
        *g_out << "Whisper model: " << whisper_model << "\n";
        *g_out << "NLLB model: " << nllb_model << "\n\n";

        // ============================================================
        // Load Whisper model
        // ============================================================
        *g_out << "Loading Whisper model...\n";
        auto start = std::chrono::high_resolution_clock::now();

        muninn::ModelOptions model_opts;
        model_opts.model_path = whisper_model;
        model_opts.device = muninn::DeviceType::CUDA;
        model_opts.compute_type = muninn::ComputeType::Float16;

        muninn::Transcriber transcriber(model_opts);

        auto load_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();
        *g_out << "Whisper loaded in " << std::fixed << std::setprecision(2)
               << load_time << "s\n\n";

        // ============================================================
        // Transcribe
        // ============================================================
        *g_out << "============================================================\n";
        *g_out << "  TRANSCRIPTION\n";
        *g_out << "============================================================\n\n";

        muninn::TranscribeOptions opts;
        opts.language = "en";  // We know it's English
        opts.word_timestamps = false;
        opts.beam_size = 5;

        *g_out << "Transcribing...\n";
        start = std::chrono::high_resolution_clock::now();

        auto result = transcriber.transcribe(audio_file, opts);

        auto transcribe_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();

        *g_out << "Transcription complete in " << std::fixed << std::setprecision(2)
               << transcribe_time << "s\n";
        *g_out << "Language: " << result.language << " (confidence: "
               << std::setprecision(1) << (result.language_probability * 100) << "%)\n";
        *g_out << "Duration: " << format_time(result.duration) << "\n";
        *g_out << "Segments: " << result.segments.size() << "\n\n";

        // Print transcription
        *g_out << "--- Original Transcription [" << result.language << "] ---\n\n";
        for (const auto& seg : result.segments) {
            *g_out << "[" << format_time(seg.start) << " -> " << format_time(seg.end) << "]\n";
            *g_out << seg.text << "\n\n";
        }

        // ============================================================
        // Translation (if NLLB model exists)
        // ============================================================
        if (fs::exists(nllb_model)) {
            *g_out << "============================================================\n";
            *g_out << "  TRANSLATIONS\n";
            *g_out << "============================================================\n\n";

            *g_out << "Loading NLLB model...\n";
            start = std::chrono::high_resolution_clock::now();

            muninn::Translator translator(nllb_model, "cuda", "float16");

            auto nllb_load_time = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - start).count();
            *g_out << "NLLB loaded in " << std::fixed << std::setprecision(2)
                   << nllb_load_time << "s\n\n";

            // Collect all segment texts for batch translation
            std::vector<std::string> segment_texts;
            segment_texts.reserve(result.segments.size());
            for (const auto& seg : result.segments) {
                segment_texts.push_back(seg.text);
            }

            *g_out << "Using batch translation for " << segment_texts.size()
                   << " segments x " << target_langs.size() << " languages\n\n";

            muninn::TranslationOptions trans_opts;
            trans_opts.beam_size = 4;
            trans_opts.max_length = 256;

            for (const auto& [lang_code, lang_name] : target_langs) {
                *g_out << "--- Translation [" << lang_code << " - " << lang_name << "] ---\n\n";

                start = std::chrono::high_resolution_clock::now();

                // Batch translate all segments at once (MUCH faster!)
                auto translations = translator.translate_batch(
                    segment_texts,
                    result.language,
                    lang_code,
                    trans_opts
                );

                // Output results
                for (size_t i = 0; i < result.segments.size() && i < translations.size(); ++i) {
                    *g_out << "[" << format_time(result.segments[i].start) << " -> "
                           << format_time(result.segments[i].end) << "]\n";
                    *g_out << translations[i] << "\n\n";
                }

                auto trans_time = std::chrono::duration<double>(
                    std::chrono::high_resolution_clock::now() - start).count();
                *g_out << "(batch translated " << translations.size() << " segments in "
                       << std::setprecision(2) << trans_time << "s)\n\n";
            }
        } else {
            *g_out << "============================================================\n";
            *g_out << "  NLLB Model Not Found - Skipping Translation\n";
            *g_out << "============================================================\n\n";
            *g_out << "To enable translation, download and convert NLLB:\n\n";
            *g_out << "  pip install ctranslate2 transformers sentencepiece\n";
            *g_out << "  ct2-transformers-converter \\\n";
            *g_out << "      --model facebook/nllb-200-distilled-600M \\\n";
            *g_out << "      --output_dir models/nllb-200-distilled-600M \\\n";
            *g_out << "      --quantization float16\n\n";
        }

        // ============================================================
        // Summary
        // ============================================================
        *g_out << "============================================================\n";
        *g_out << "  SUMMARY\n";
        *g_out << "============================================================\n\n";

        *g_out << "Audio duration:    " << format_time(result.duration) << "\n";
        *g_out << "Whisper load time: " << std::setprecision(2) << load_time << "s\n";
        *g_out << "Transcribe time:   " << transcribe_time << "s\n";

        if (result.duration > 0) {
            double rtf = transcribe_time / result.duration;
            *g_out << "Real-time factor:  " << std::setprecision(3) << rtf << "x\n";
        }

        // Close file if writing to one
        if (g_file_out.is_open()) {
            g_file_out.close();
            std::cout << "Output written successfully.\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 1;
    }
}
