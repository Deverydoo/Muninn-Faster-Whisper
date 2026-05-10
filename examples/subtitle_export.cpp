/**
 * @file subtitle_export.cpp
 * @brief Example: Subtitle export (SRT/VTT) with speaker labels
 *
 * This example demonstrates Muninn's subtitle export capabilities:
 * - Automatic SRT generation (universal compatibility)
 * - VTT export with speaker colors and styling
 * - Metadata JSON preservation (Loki Studio format)
 * - Custom speaker label formatting
 * - Multi-format export workflows
 *
 * Use cases:
 * - Video editing (Premiere, DaVinci Resolve)
 * - YouTube/streaming platform uploads
 * - Podcast episode transcripts
 * - Gaming commentary subtitles
 * - Multi-speaker content (interviews, panels)
 */

#include <muninn/transcriber.h>
#include <muninn/diarization.h>
#include <muninn/subtitle_export.h>
#include <iostream>
#include <iomanip>

/**
 * @brief Example 1: Basic SRT export (simple transcription)
 */
void example_basic_srt(const std::string& video_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 1: Basic SRT Export\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Transcribe video
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

    muninn::TranscribeOptions options;
    options.word_timestamps = false;  // Not needed for basic SRT

    auto result = transcriber.transcribe(video_path, options);

    std::cout << "Transcribed " << result.segments.size() << " segments\n";

    // Export to SRT (automatic path: video.mp4 -> video.srt)
    muninn::SubtitleExporter exporter;
    std::string srt_path = exporter.export_srt(result.segments, video_path);

    std::cout << "✓ Created SRT: " << srt_path << "\n\n";
}

/**
 * @brief Example 2: SRT with speaker labels (podcast/interview)
 */
void example_speaker_srt(const std::string& video_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 2: SRT with Speaker Labels\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Transcribe
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");
    auto result = transcriber.transcribe(video_path);

    // Run diarization
    muninn::Diarizer diarizer("models/pyannote-embedding.onnx");

    // Load audio (simplified - in real code, use audio extraction)
    std::vector<float> audio_samples;
    // auto audio_samples = load_audio(video_path);

    auto diarization = diarizer.diarize(audio_samples.data(),
                                       audio_samples.size(), 16000);

    // Assign speakers
    muninn::Diarizer::assign_speakers_to_segments(result.segments, diarization);

    // Custom speaker labels
    std::map<int, std::string> speaker_names = {
        {0, "Host"},
        {1, "Guest"}
    };
    muninn::Diarizer::set_speaker_labels(diarization, speaker_names);

    // Export with speaker labels
    muninn::SubtitleExportOptions options;
    options.include_speakers = true;
    options.speaker_format = "[{label}] {text}";  // Format: [Host] text

    muninn::SubtitleExporter exporter;
    std::string srt_path = exporter.export_srt(result.segments, video_path, options);

    std::cout << "✓ Created SRT with speakers: " << srt_path << "\n";
    std::cout << "Format: [Speaker] text\n\n";
}

/**
 * @brief Example 3: VTT with speaker colors (streaming/web)
 */
void example_vtt_with_colors(const std::string& video_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 3: VTT with Speaker Colors\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Transcribe + diarize (same as example 2)
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");
    auto result = transcriber.transcribe(video_path);

    // Configure VTT export with speaker colors
    muninn::SubtitleExportOptions options;
    options.format = muninn::SubtitleFormat::VTT;
    options.include_speakers = true;
    options.vtt_include_speaker_colors = true;

    // Define speaker colors (cyan, pink, lime, orange)
    options.vtt_speaker_colors = {
        {0, "#00D9FF"},  // Cyan
        {1, "#FF6B9D"},  // Pink
        {2, "#C9F04D"},  // Lime
        {3, "#FFB84D"}   // Orange
    };

    muninn::SubtitleExporter exporter;
    std::string vtt_path = exporter.export_vtt(result.segments, video_path, options);

    std::cout << "✓ Created VTT with speaker colors: " << vtt_path << "\n";
    std::cout << "  Speaker 0: Cyan (#00D9FF)\n";
    std::cout << "  Speaker 1: Pink (#FF6B9D)\n";
    std::cout << "  Speaker 2: Lime (#C9F04D)\n";
    std::cout << "  Speaker 3: Orange (#FFB84D)\n\n";
}

/**
 * @brief Example 4: Gaming commentary (player + game audio)
 */
void example_gaming_commentary(const std::string& video_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 4: Gaming Commentary Subtitles\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Transcribe with multi-track support (track 0 = game, track 1 = mic)
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

    muninn::TranscribeOptions trans_options;
    // Process all tracks separately

    auto result = transcriber.transcribe(video_path, trans_options);

    // Map tracks to speakers
    for (auto& seg : result.segments) {
        if (seg.track_id == 0) {
            seg.speaker_id = 0;
            seg.speaker_label = "Game Audio";
        } else if (seg.track_id == 1) {
            seg.speaker_id = 1;
            seg.speaker_label = "Player";
        }
    }

    // Export with custom gaming format
    muninn::SubtitleExportOptions options;
    options.include_speakers = true;
    options.speaker_format = "[{label}] {text}";
    options.max_chars_per_line = 50;  // Wider for gaming

    muninn::SubtitleExporter exporter;
    std::string srt_path = exporter.export_srt(result.segments, video_path, options);

    std::cout << "✓ Created gaming SRT: " << srt_path << "\n";
    std::cout << "Format:\n";
    std::cout << "  [Game Audio] NPC dialogue\n";
    std::cout << "  [Player] Commentary\n\n";
}

/**
 * @brief Example 5: Loki Studio metadata JSON
 */
void example_metadata_json(const std::string& video_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 5: Loki Studio Metadata JSON\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Transcribe
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

    muninn::TranscribeOptions options;
    options.word_timestamps = true;  // Include word-level data

    auto result = transcriber.transcribe(video_path, options);

    // Generate metadata JSON (Loki Studio format)
    std::string metadata_path = muninn::SubtitleMetadata::generate_metadata_json(
        result.segments,
        video_path,
        "large-v3-turbo",
        result.language,
        result.duration
    );

    std::cout << "✓ Created metadata JSON: " << metadata_path << "\n";
    std::cout << "  Segments: " << result.segments.size() << "\n";
    std::cout << "  Language: " << result.language << "\n";
    std::cout << "  Duration: " << std::fixed << std::setprecision(1)
              << result.duration << "s\n\n";

    // Also export SRT
    muninn::SubtitleExporter exporter;
    std::string srt_path = exporter.export_srt(result.segments, video_path);

    std::cout << "✓ Created SRT: " << srt_path << "\n\n";
}

/**
 * @brief Example 6: Multi-format export (SRT + VTT + JSON)
 */
void example_multi_format(const std::string& video_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 6: Multi-Format Export\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Transcribe once
    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");

    muninn::TranscribeOptions trans_options;
    trans_options.word_timestamps = true;

    auto result = transcriber.transcribe(video_path, trans_options);

    muninn::SubtitleExporter exporter;

    // Export SRT (universal compatibility - video editors)
    muninn::SubtitleExportOptions srt_options;
    srt_options.format = muninn::SubtitleFormat::SRT;
    std::string srt_path = exporter.export_srt(result.segments, video_path, srt_options);

    // Export VTT (web/streaming platforms)
    muninn::SubtitleExportOptions vtt_options;
    vtt_options.format = muninn::SubtitleFormat::VTT;
    vtt_options.vtt_include_word_timestamps = true;
    std::string vtt_path = exporter.export_vtt(result.segments, video_path, vtt_options);

    // Export metadata JSON (archival/editing)
    std::string json_path = muninn::SubtitleMetadata::generate_metadata_json(
        result.segments,
        video_path,
        "large-v3-turbo",
        result.language,
        result.duration
    );

    std::cout << "✓ Created SRT: " << srt_path << "\n";
    std::cout << "✓ Created VTT: " << vtt_path << "\n";
    std::cout << "✓ Created JSON: " << json_path << "\n\n";
}

/**
 * @brief Example 7: Custom text formatting (line splitting)
 */
void example_custom_formatting(const std::string& video_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 7: Custom Text Formatting\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    muninn::Transcriber transcriber("models/faster-whisper-large-v3-turbo");
    auto result = transcriber.transcribe(video_path);

    // Configure text formatting
    muninn::SubtitleExportOptions options;
    options.max_chars_per_line = 35;       // Short lines (mobile-friendly)
    options.max_lines = 2;                 // Max 2 lines per subtitle
    options.auto_split_long_text = true;   // Auto-split at word boundaries
    options.gap_threshold = 0.5f;          // Merge segments < 0.5s apart

    muninn::SubtitleExporter exporter;
    std::string srt_path = exporter.export_srt(result.segments, video_path, options);

    std::cout << "✓ Created formatted SRT: " << srt_path << "\n";
    std::cout << "  Max line length: 35 chars\n";
    std::cout << "  Max lines: 2\n";
    std::cout << "  Merged close segments\n\n";
}

/**
 * @brief Example 8: Load from metadata JSON (re-export)
 */
void example_load_metadata(const std::string& metadata_json_path) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Example 8: Load from Metadata JSON\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    // Load previously saved metadata
    auto segments = muninn::SubtitleMetadata::load_from_metadata_json(metadata_json_path);

    std::cout << "Loaded " << segments.size() << " segments from JSON\n";

    // Re-export with different options
    muninn::SubtitleExportOptions options;
    options.include_speakers = true;
    options.speaker_format = "{label}: {text}";

    // Extract video path from metadata path
    std::filesystem::path json_path(metadata_json_path);
    std::string stem = json_path.stem().string();
    stem = stem.substr(0, stem.find("_metadata"));  // Remove "_metadata"
    std::string video_path = (json_path.parent_path() / (stem + ".mp4")).string();

    muninn::SubtitleExporter exporter;
    std::string srt_path = exporter.export_srt(segments, video_path, options);

    std::cout << "✓ Re-exported to SRT: " << srt_path << "\n\n";
}

int main(int argc, char** argv) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Subtitle Export Examples\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file> [example_number]\n";
        std::cerr << "\nExamples:\n";
        std::cerr << "  1 - Basic SRT export\n";
        std::cerr << "  2 - SRT with speaker labels\n";
        std::cerr << "  3 - VTT with speaker colors\n";
        std::cerr << "  4 - Gaming commentary subtitles\n";
        std::cerr << "  5 - Loki Studio metadata JSON\n";
        std::cerr << "  6 - Multi-format export (SRT + VTT + JSON)\n";
        std::cerr << "  7 - Custom text formatting\n";
        std::cerr << "  8 - Load from metadata JSON (requires _metadata.json path)\n";
        std::cerr << "\n";
        std::cerr << "Default: Runs example 6 (multi-format export)\n";
        return 1;
    }

    std::string video_path = argv[1];
    int example_num = (argc > 2) ? std::stoi(argv[2]) : 6;

    try {
        switch (example_num) {
            case 1:
                example_basic_srt(video_path);
                break;
            case 2:
                example_speaker_srt(video_path);
                break;
            case 3:
                example_vtt_with_colors(video_path);
                break;
            case 4:
                example_gaming_commentary(video_path);
                break;
            case 5:
                example_metadata_json(video_path);
                break;
            case 6:
                example_multi_format(video_path);
                break;
            case 7:
                example_custom_formatting(video_path);
                break;
            case 8:
                example_load_metadata(video_path);  // video_path = metadata.json
                break;
            default:
                std::cerr << "Invalid example number. Use 1-8.\n";
                return 1;
        }

        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "Done!\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
