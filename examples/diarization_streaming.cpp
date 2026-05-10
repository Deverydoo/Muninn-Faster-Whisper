/**
 * @file diarization_streaming.cpp
 * @brief Example: Multi-speaker diarization with streaming transcription
 *
 * This example demonstrates Muninn's speaker diarization capabilities:
 * - Automatic speaker detection and separation
 * - Speaker-labeled transcription
 * - Multi-speaker conversation tracking
 * - Custom speaker naming
 *
 * Use cases:
 * - Podcast transcription (host + guests)
 * - Meeting transcription (multiple participants)
 * - Interview transcription
 * - Multi-person gaming streams
 * - Conference call transcription
 */

#include <muninn/streaming_transcriber.h>
#include <muninn/diarization.h>
#include <muninn/word_styling.h>
#include <iostream>
#include <iomanip>
#include <map>

/**
 * @brief Display segment with speaker label and color
 */
void render_speaker_segment(const muninn::Segment& seg,
                           const std::map<int, std::string>& speaker_colors) {
    // Get speaker color
    std::string ansi_color = "\033[1;97m";  // Default white

    auto it = speaker_colors.find(seg.speaker_id);
    if (it != speaker_colors.end()) {
        // Map hex colors to ANSI codes
        static const std::map<std::string, std::string> color_map = {
            {"#00D9FF", "\033[1;96m"},  // Cyan
            {"#FF6B9D", "\033[1;95m"},  // Magenta
            {"#C9F04D", "\033[1;92m"},  // Green
            {"#FFB84D", "\033[1;93m"},  // Yellow
            {"#A78BFA", "\033[1;94m"}   // Blue
        };

        auto color_it = color_map.find(it->second);
        if (color_it != color_map.end()) {
            ansi_color = color_it->second;
        }
    }

    // Speaker label (bold, colored)
    std::cout << ansi_color << "[" << seg.speaker_label << "]"
              << "\033[0m ";

    // Text (normal, colored)
    std::cout << ansi_color << seg.text << "\033[0m";

    // Timing info (dim)
    std::cout << " \033[2m(" << std::fixed << std::setprecision(1)
              << seg.start << "s - " << seg.end << "s";

    // Confidence (if available)
    if (seg.speaker_confidence > 0.0f) {
        std::cout << ", confidence: " << std::setprecision(0)
                  << (seg.speaker_confidence * 100.0f) << "%";
    }

    std::cout << ")\033[0m" << std::endl << std::endl;
}

/**
 * @brief Build HTML for OBS with speaker colors
 */
std::string build_obs_speaker_html(const muninn::Segment& seg,
                                   const std::map<int, std::string>& speaker_colors,
                                   float current_time) {
    std::ostringstream html;

    // Get speaker color
    std::string color = "#FFFFFF";
    auto it = speaker_colors.find(seg.speaker_id);
    if (it != speaker_colors.end()) {
        color = it->second;
    }

    // Speaker label (small, bold, colored)
    html << "<font size='-1' color='" << color << "'>";
    html << "<b>[" << seg.speaker_label << "]</b>";
    html << "</font><br>";

    // Text with karaoke highlighting (if word timestamps available)
    if (!seg.words.empty() && current_time >= seg.start && current_time < seg.end) {
        for (const auto& word : seg.words) {
            bool is_current = (current_time >= word.start && current_time < word.end);

            if (is_current) {
                // Highlighted word (bold, larger, speaker color)
                html << "<font size='+1' color='" << color << "'>";
                html << "<b>" << word.word << "</b>";
                html << "</font> ";
            } else if (current_time > word.end) {
                // Past word (dim)
                html << "<font color='#888888'>" << word.word << "</font> ";
            } else {
                // Future word (normal, speaker color)
                html << "<font color='" << color << "'>" << word.word << "</font> ";
            }
        }
    } else {
        // No word timestamps - just show full text
        html << "<font size='+1' color='" << color << "'>";
        html << seg.text;
        html << "</font>";
    }

    return html.str();
}

/**
 * @brief Print speaker statistics
 */
void print_speaker_stats(const muninn::DiarizationResult& diarization) {
    std::cout << "\n═══════════════════════════════════════════════════════════\n";
    std::cout << "Speaker Statistics\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    for (const auto& speaker : diarization.speakers) {
        std::cout << speaker.label << " (ID: " << speaker.speaker_id << "):\n";
        std::cout << "  Total speaking time: " << std::fixed << std::setprecision(1)
                  << speaker.total_duration << "s\n";
        std::cout << "  Number of utterances: " << speaker.embeddings.size() << "\n";
        std::cout << "\n";
    }
}

int main(int argc, char** argv) {
    std::cout << "═══════════════════════════════════════════════════════════\n";
    std::cout << "Muninn Multi-Speaker Diarization Example\n";
    std::cout << "═══════════════════════════════════════════════════════════\n\n";

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <whisper_model> <pyannote_model> [audio_file]\n";
        std::cerr << "Example:\n";
        std::cerr << "  " << argv[0] << " models/faster-whisper-large-v3-turbo \\\n";
        std::cerr << "                   models/pyannote-embedding.onnx \\\n";
        std::cerr << "                   podcast_interview.wav\n";
        return 1;
    }

    std::string whisper_model = argv[1];
    std::string pyannote_model = argv[2];
    std::string audio_file = (argc > 3) ? argv[3] : "";

    try {
        // ═══════════════════════════════════════════════════════════
        // Step 1: Initialize Whisper transcriber
        // ═══════════════════════════════════════════════════════════
        std::cout << "Loading Whisper model: " << whisper_model << "...\n";
        muninn::StreamingTranscriber transcriber(whisper_model, "cuda", "float16");

        // Configure for streaming transcription
        muninn::StreamingOptions options;
        options.language = "auto";
        options.task = "transcribe";
        options.chunk_length_s = 2.0f;      // Longer chunks for better diarization
        options.overlap_s = 0.5f;
        options.enable_vad = true;
        options.word_timestamps = true;     // For karaoke + better speaker alignment

        // ═══════════════════════════════════════════════════════════
        // Step 2: Initialize speaker diarizer
        // ═══════════════════════════════════════════════════════════
        std::cout << "Loading pyannote model: " << pyannote_model << "...\n";

        muninn::DiarizationOptions diar_options;
        diar_options.clustering_threshold = 0.7f;   // Adjust for more/fewer speakers
        diar_options.embedding_window_s = 1.5f;
        diar_options.embedding_step_s = 0.75f;
        diar_options.device = "cuda";

        muninn::Diarizer diarizer(pyannote_model, diar_options);

        std::cout << "\n";
        std::cout << "Configuration:\n";
        std::cout << "  Transcription: " << options.chunk_length_s << "s chunks\n";
        std::cout << "  Diarization: " << diar_options.embedding_window_s << "s embeddings\n";
        std::cout << "  Clustering threshold: " << diar_options.clustering_threshold << "\n";
        std::cout << "\n";

        // ═══════════════════════════════════════════════════════════
        // Step 3: Load audio and run diarization
        // ═══════════════════════════════════════════════════════════
        std::vector<float> audio_samples;
        int sample_rate = 16000;

        if (!audio_file.empty()) {
            std::cout << "Loading audio: " << audio_file << "...\n";
            // TODO: Load audio file (using Heimdall or similar)
            // For now, use simulated data
            std::cout << "Note: Audio loading not implemented in this example.\n";
            std::cout << "Use push_audio() with real audio stream instead.\n\n";
        }

        // Run diarization on full audio
        std::cout << "Running speaker diarization...\n";
        auto diarization = diarizer.diarize(audio_samples.data(),
                                           audio_samples.size(),
                                           sample_rate);

        std::cout << "Detected " << diarization.num_speakers << " speakers!\n\n";

        // ═══════════════════════════════════════════════════════════
        // Step 4: Optional - Set custom speaker labels
        // ═══════════════════════════════════════════════════════════
        if (diarization.num_speakers == 2) {
            // Example: Podcast with host + guest
            std::map<int, std::string> custom_labels = {
                {0, "Host"},
                {1, "Guest"}
            };
            muninn::Diarizer::set_speaker_labels(diarization, custom_labels);
        } else if (diarization.num_speakers == 3) {
            // Example: Panel discussion
            std::map<int, std::string> custom_labels = {
                {0, "Alice"},
                {1, "Bob"},
                {2, "Charlie"}
            };
            muninn::Diarizer::set_speaker_labels(diarization, custom_labels);
        }

        // Generate speaker colors
        auto speaker_colors = muninn::SpeakerFormatting::generate_speaker_colors(
            diarization.num_speakers);

        // ═══════════════════════════════════════════════════════════
        // Step 5: Start streaming transcription
        // ═══════════════════════════════════════════════════════════
        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "Starting multi-speaker transcription...\n";
        std::cout << "═══════════════════════════════════════════════════════════\n\n";

        std::vector<muninn::Segment> all_segments;

        transcriber.start(options, [&](const muninn::Segment& seg) {
            all_segments.push_back(seg);
            return true;
        });

        // Push audio to transcriber
        // In real use: connect to microphone or audio stream
        // transcriber.push_audio(audio_samples.data(), audio_samples.size(), 16000, 1);

        // Stop and get final segments
        auto final_segments = transcriber.stop();

        // ═══════════════════════════════════════════════════════════
        // Step 6: Assign speakers to transcription segments
        // ═══════════════════════════════════════════════════════════
        std::cout << "Assigning speakers to transcription...\n\n";
        muninn::Diarizer::assign_speakers_to_segments(final_segments, diarization);

        // ═══════════════════════════════════════════════════════════
        // Step 7: Display results
        // ═══════════════════════════════════════════════════════════
        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "Transcription with Speaker Labels\n";
        std::cout << "═══════════════════════════════════════════════════════════\n\n";

        for (const auto& seg : final_segments) {
            render_speaker_segment(seg, speaker_colors);
        }

        // Print statistics
        print_speaker_stats(diarization);

        // ═══════════════════════════════════════════════════════════
        // Example: OBS Integration
        // ═══════════════════════════════════════════════════════════
        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "OBS Integration Example\n";
        std::cout << "═══════════════════════════════════════════════════════════\n\n";

        if (!final_segments.empty()) {
            float current_time = final_segments[0].start + 0.5f;  // Simulate playback
            std::string html = build_obs_speaker_html(final_segments[0],
                                                     speaker_colors,
                                                     current_time);

            std::cout << "HTML for OBS text source:\n";
            std::cout << html << "\n\n";
        }

        std::cout << "Example OBS worker thread:\n";
        std::cout << R"(
void update_obs_with_speakers(muninn_data* data) {
    muninn::Segment seg;
    if (data->stream->poll_segment(seg)) {
        // Assign speaker
        int speaker = muninn::Diarizer::get_speaker_at_time(
            data->diarization, seg.start);
        seg.speaker_id = speaker;
        seg.speaker_label = data->diarization.speakers[speaker].label;

        // Build HTML
        float time = get_stream_time();
        std::string html = build_obs_speaker_html(
            seg, data->speaker_colors, time);

        // Update OBS
        obs_data_t* settings = obs_data_create();
        obs_data_set_string(settings, "text", html.c_str());
        obs_source_update(data->text_source, settings);
        obs_data_release(settings);
    }
}
)" << "\n";

        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "Diarization complete!\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
