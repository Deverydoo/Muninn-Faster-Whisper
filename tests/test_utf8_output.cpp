/**
 * @file test_utf8_output.cpp
 * @brief Quick test to verify UTF-8 file output works correctly
 */

#include <muninn/translator.h>
#include <iostream>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Write UTF-8 BOM to file stream
void write_utf8_bom(std::ofstream& file) {
    unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write(reinterpret_cast<char*>(bom), 3);
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::string nllb_model = "models/nllb-200-distilled-600M";
    std::string output_file = "utf8_test_output.txt";

    if (argc > 1) nllb_model = argv[1];
    if (argc > 2) output_file = argv[2];

    std::cout << "Testing UTF-8 output to: " << output_file << "\n";

    if (!fs::exists(nllb_model)) {
        std::cerr << "NLLB model not found: " << nllb_model << "\n";
        return 1;
    }

    try {
        // Open output file with UTF-8 BOM
        std::ofstream out(output_file, std::ios::out | std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "Failed to open output file: " << output_file << "\n";
            return 1;
        }
        write_utf8_bom(out);

        out << "UTF-8 Translation Test\n";
        out << "=====================\n\n";

        // Load NLLB translator
        std::cout << "Loading NLLB model...\n";
        muninn::Translator translator(nllb_model, "cuda", "float16");

        // Test text to translate
        std::string test_text = "Hello, this is a test of the translation system. The quick brown fox jumps over the lazy dog.";

        out << "Original English:\n";
        out << test_text << "\n\n";

        muninn::TranslationOptions opts;
        opts.beam_size = 4;

        // Test translations to multiple languages
        std::vector<std::pair<std::string, std::string>> langs = {
            {"es", "Spanish"},
            {"de", "German"},
            {"fr", "French"},
            {"ja", "Japanese"},
            {"zh", "Chinese"},
            {"ru", "Russian"},
            {"ko", "Korean"},
            {"ar", "Arabic"},
        };

        for (const auto& [code, name] : langs) {
            std::cout << "Translating to " << name << "...\n";
            std::string translated = translator.translate(test_text, "en", code, opts);
            out << name << " (" << code << "):\n";
            out << translated << "\n\n";
        }

        out << "Test complete!\n";
        out.close();

        std::cout << "\nOutput written to: " << output_file << "\n";
        std::cout << "Please open this file in VS Code to verify UTF-8 encoding.\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
