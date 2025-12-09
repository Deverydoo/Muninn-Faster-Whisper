/**
 * @file translation_example.cpp
 * @brief Example demonstrating transcription + translation pipeline
 *
 * This example shows how to:
 * 1. Transcribe audio in any language using Whisper
 * 2. Translate the transcription to multiple target languages using NLLB
 *
 * Usage:
 *   translation_example <whisper_model> <nllb_model> <audio_file> [target_langs...]
 *
 * Example:
 *   translation_example models/faster-whisper-large-v3-turbo \
 *                       models/nllb-200-distilled-600M \
 *                       japanese_video.mp4 \
 *                       en es fr de
 */

#include <muninn/transcriber.h>
#include <muninn/translator.h>
#include <iostream>
#include <iomanip>
#include <chrono>

void print_separator(const char* title = nullptr) {
    std::cout << "\n";
    if (title) {
        std::cout << "=== " << title << " ";
        for (int i = 0; i < 60 - static_cast<int>(strlen(title)); ++i) std::cout << "=";
    } else {
        for (int i = 0; i < 70; ++i) std::cout << "=";
    }
    std::cout << "\n\n";
}

std::string format_time(float seconds) {
    int mins = static_cast<int>(seconds) / 60;
    float secs = seconds - mins * 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%05.2f", mins, secs);
    return buf;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <whisper_model> <nllb_model> <audio_file> [target_langs...]\n\n";
        std::cerr << "Example:\n";
        std::cerr << "  " << argv[0] << " models/faster-whisper-large-v3-turbo \\\n";
        std::cerr << "                    models/nllb-200-distilled-600M \\\n";
        std::cerr << "                    video.mp4 en es fr de\n\n";
        std::cerr << "Supported languages:\n";
        for (const auto& lang : muninn::Translator::supported_languages()) {
            std::cerr << "  " << lang.code << " - " << lang.name << "\n";
        }
        return 1;
    }

    std::string whisper_model_path = argv[1];
    std::string nllb_model_path = argv[2];
    std::string audio_file = argv[3];

    // Collect target languages (default to English if none specified)
    std::vector<std::string> target_langs;
    if (argc > 4) {
        for (int i = 4; i < argc; ++i) {
            target_langs.push_back(argv[i]);
        }
    } else {
        target_langs = {"en"};  // Default to English translation
    }

    try {
        print_separator("Loading Models");

        // Load Whisper model
        std::cout << "Loading Whisper model: " << whisper_model_path << "\n";
        auto start = std::chrono::high_resolution_clock::now();

        muninn::ModelOptions model_opts;
        model_opts.model_path = whisper_model_path;
        model_opts.device = muninn::DeviceType::CUDA;
        model_opts.compute_type = muninn::ComputeType::Float16;

        muninn::Transcriber transcriber(model_opts);

        auto whisper_load_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();
        std::cout << "Whisper loaded in " << std::fixed << std::setprecision(2)
                  << whisper_load_time << "s\n\n";

        // Load NLLB translator
        std::cout << "Loading NLLB model: " << nllb_model_path << "\n";
        start = std::chrono::high_resolution_clock::now();

        muninn::Translator translator(nllb_model_path, "cuda", "float16");

        auto nllb_load_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();
        std::cout << "NLLB loaded in " << std::fixed << std::setprecision(2)
                  << nllb_load_time << "s\n";

        // Validate target languages
        std::cout << "\nTarget languages: ";
        for (const auto& lang : target_langs) {
            if (!translator.is_language_supported(lang)) {
                std::cerr << "\nError: Unsupported language '" << lang << "'\n";
                return 1;
            }
            std::cout << lang << " ";
        }
        std::cout << "\n";

        print_separator("Transcribing Audio");

        // Transcribe with auto language detection
        muninn::TranscribeOptions opts;
        opts.language = "auto";  // Auto-detect source language
        opts.word_timestamps = false;
        opts.beam_size = 5;

        std::cout << "Transcribing: " << audio_file << "\n";
        start = std::chrono::high_resolution_clock::now();

        auto result = transcriber.transcribe(audio_file, opts);

        auto transcribe_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();

        std::cout << "Transcription complete in " << std::fixed << std::setprecision(2)
                  << transcribe_time << "s\n";
        std::cout << "Detected language: " << result.language
                  << " (confidence: " << std::setprecision(1)
                  << (result.language_probability * 100) << "%)\n";
        std::cout << "Segments: " << result.segments.size() << "\n";
        std::cout << "Duration: " << format_time(result.duration) << "\n";

        print_separator("Translating to Target Languages");

        // Translation options
        muninn::TranslationOptions trans_opts;
        trans_opts.beam_size = 4;
        trans_opts.max_length = 256;

        start = std::chrono::high_resolution_clock::now();
        int total_translations = 0;

        // Process each segment
        for (auto& segment : result.segments) {
            std::cout << "\n[" << format_time(segment.start) << " -> "
                      << format_time(segment.end) << "]\n";
            std::cout << "  Original (" << result.language << "): " << segment.text << "\n";

            // Translate to each target language
            for (const auto& target : target_langs) {
                // Skip if source == target
                if (target == result.language) {
                    std::cout << "  " << target << ": (same as source)\n";
                    continue;
                }

                std::string translated = translator.translate(
                    segment.text,
                    result.language,
                    target,
                    trans_opts
                );

                std::cout << "  " << target << ": " << translated << "\n";
                total_translations++;

                // Store the last translation in the segment
                segment.translated_text = translated;
                segment.translation_target = target;
            }
        }

        auto translate_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();

        print_separator("Summary");

        std::cout << "Audio duration:     " << format_time(result.duration) << "\n";
        std::cout << "Source language:    " << result.language << "\n";
        std::cout << "Segments:           " << result.segments.size() << "\n";
        std::cout << "Translations:       " << total_translations << "\n";
        std::cout << "\n";
        std::cout << "Whisper load time:  " << std::fixed << std::setprecision(2)
                  << whisper_load_time << "s\n";
        std::cout << "NLLB load time:     " << nllb_load_time << "s\n";
        std::cout << "Transcribe time:    " << transcribe_time << "s\n";
        std::cout << "Translate time:     " << translate_time << "s\n";
        std::cout << "Total time:         "
                  << (whisper_load_time + nllb_load_time + transcribe_time + translate_time) << "s\n";

        if (result.duration > 0) {
            double rtf = (transcribe_time + translate_time) / result.duration;
            std::cout << "\nReal-time factor:   " << std::setprecision(3) << rtf << "x\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 1;
    }
}
