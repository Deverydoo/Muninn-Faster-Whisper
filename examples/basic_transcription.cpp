/**
 * @file basic_transcription.cpp
 * @brief Example: transcribe an audio or video file and print the segments
 *
 * Usage: basic_transcription <model_dir> <audio_file> [cuda|cpu|auto] [language]
 *
 * model_dir is a CTranslate2 Whisper model (for example
 * faster-whisper-large-v3-turbo). Every audio track of the file is
 * transcribed; each segment says which track it came from.
 */

#include <muninn/transcriber.h>
#include <muninn/version.h>

#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <model_dir> <audio_file> [cuda|cpu|auto] [language]\n", argv[0]);
        return 1;
    }
    const std::string model_path = argv[1];
    const std::string audio_path = argv[2];
    const std::string device = argc > 3 ? argv[3] : "auto";

    try {
        // Throws if muninn.dll isn't from the same release as these headers.
        muninn::check_version();

        // float16 on the GPU; Muninn uses float32 instead on the CPU.
        muninn::Transcriber transcriber(model_path, device, "float16");
        const auto info = transcriber.get_device_info();
        std::printf("Muninn %s on %s (%s)\n", muninn::library_version(), info.device.c_str(),
                    info.compute_type.c_str());

        muninn::TranscribeOptions options;
        options.language = argc > 4 ? argv[4] : "auto";
        options.word_timestamps = true;

        const auto result = transcriber.transcribe(
            audio_path, options,
            [](int track, int total_tracks, float progress, const std::string& message) {
                std::fprintf(stderr, "\rTrack %d/%d %3.0f%% %-40s", track + 1, total_tracks,
                             progress * 100.0f, message.c_str());
                return true;  // false cancels
            });
        std::fprintf(stderr, "\n");

        std::printf("Language: %s (%.0f%%), duration %.1f s, %zu segments\n",
                    result.language.c_str(), result.language_probability * 100.0f, result.duration,
                    result.segments.size());
        for (const auto& seg : result.segments) {
            std::printf("[track %d] [%7.2f -> %7.2f] %s\n", seg.track_id, seg.start, seg.end,
                        seg.text.c_str());
        }

        // A window or track that failed doesn't stop the run; it is counted here.
        if (result.failed_windows > 0 || result.failed_tracks > 0) {
            std::fprintf(stderr, "%d window(s) and %d track(s) failed: %s\n", result.failed_windows,
                         result.failed_tracks, result.error.c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}
