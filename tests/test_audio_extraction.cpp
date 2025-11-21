#include "muninn/audio_extractor.h"
#include <iostream>
#include <iomanip>

int main(int argc, char* argv[]) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Audio Extraction Test\n";
    std::cout << "Testing Internal Audio Decoder (no Heimdall DLL)\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Parse command-line arguments
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <video_or_audio_file>\n";
        std::cout << "\nExample:\n";
        std::cout << "  " << argv[0] << " test.mp4\n";
        std::cout << "  " << argv[0] << " audio.mp3\n";
        return 1;
    }

    std::string file_path = argv[1];
    std::cout << "[Test] File: " << file_path << "\n\n";

    try {
        // Create audio extractor
        muninn::AudioExtractor extractor;

        // Open file
        std::cout << "[1] Opening file...\n";
        if (!extractor.open(file_path)) {
            std::cerr << "[ERROR] Failed to open file: " << extractor.get_last_error() << "\n";
            return 1;
        }
        std::cout << "    ✓ File opened successfully\n\n";

        // Get file info
        int track_count = extractor.get_track_count();
        float duration = extractor.get_duration();

        std::cout << "[2] File Information:\n";
        std::cout << "    Audio Tracks: " << track_count << "\n";
        std::cout << "    Duration:     " << std::fixed << std::setprecision(2) << duration << "s\n\n";

        // Extract first track
        std::cout << "[3] Extracting Track 0 (16kHz mono)...\n";
        std::vector<float> samples;
        if (!extractor.extract_track(0, samples)) {
            std::cerr << "[ERROR] Failed to extract track: " << extractor.get_last_error() << "\n";
            return 1;
        }
        std::cout << "    ✓ Extracted " << samples.size() << " samples\n";
        std::cout << "    Duration:   " << (samples.size() / 16000.0f) << "s at 16kHz\n\n";

        // Show sample statistics
        if (!samples.empty()) {
            float min_sample = samples[0];
            float max_sample = samples[0];
            float sum = 0.0f;

            for (float s : samples) {
                if (s < min_sample) min_sample = s;
                if (s > max_sample) max_sample = s;
                sum += s * s;
            }

            float rms = std::sqrt(sum / samples.size());

            std::cout << "[4] Audio Statistics:\n";
            std::cout << "    Min:  " << std::fixed << std::setprecision(4) << min_sample << "\n";
            std::cout << "    Max:  " << max_sample << "\n";
            std::cout << "    RMS:  " << rms << "\n\n";
        }

        // Close file
        extractor.close();
        std::cout << "[5] Cleanup complete\n\n";

        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "✓ SUCCESS: Internal audio decoder working correctly!\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << "\n";
        return 1;
    }
}
