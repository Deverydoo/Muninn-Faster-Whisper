#pragma once

#include "export.h"
#include "types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace muninn {

/**
 * @brief Speaker embedding representation (512-dimensional vector)
 */
struct SpeakerEmbedding {
    std::vector<float> features;     // 512-dimensional embedding vector
    float start;                     // Start time in seconds
    float end;                       // End time in seconds

    SpeakerEmbedding() : start(0.0f), end(0.0f) {}
};

/**
 * @brief Speaker information
 */
struct Speaker {
    int speaker_id;                  // Unique speaker ID (0, 1, 2, ...)
    std::string label;               // Speaker label ("Speaker 0", "Speaker 1", or custom)
    std::vector<SpeakerEmbedding> embeddings;  // All embeddings for this speaker
    float total_duration;            // Total speaking time in seconds

    Speaker() : speaker_id(-1), total_duration(0.0f) {}
};

/**
 * @brief Diarization configuration options
 */
struct DiarizationOptions {
    // ═══════════════════════════════════════════════════════════
    // Model Configuration
    // ═══════════════════════════════════════════════════════════
    std::string embedding_model_path;      // Path to pyannote speaker embedding model (ONNX)
    std::string segmentation_model_path;   // Optional: Path to pyannote segmentation model

    // ═══════════════════════════════════════════════════════════
    // Clustering Parameters
    // ═══════════════════════════════════════════════════════════
    float clustering_threshold = 0.7f;     // Cosine similarity threshold (0.5-0.9)
    int min_speakers = 1;                  // Minimum number of speakers
    int max_speakers = 10;                 // Maximum number of speakers (0 = unlimited)

    // ═══════════════════════════════════════════════════════════
    // Embedding Extraction
    // ═══════════════════════════════════════════════════════════
    float embedding_window_s = 1.0f;       // Window size for embedding extraction (1s = 16000 samples at 16kHz)
    float embedding_step_s = 0.5f;         // Step size between embeddings (50% overlap)

    // ═══════════════════════════════════════════════════════════
    // Speaker Assignment
    // ═══════════════════════════════════════════════════════════
    float min_segment_duration = 0.3f;     // Minimum duration to assign speaker
    bool merge_adjacent_same_speaker = true;  // Merge consecutive segments from same speaker

    // ═══════════════════════════════════════════════════════════
    // Performance
    // ═══════════════════════════════════════════════════════════
    std::string device = "cuda";           // "cuda" or "cpu"
    int num_threads = 4;                   // CPU threads (if device = "cpu")
};

/**
 * @brief Diarization result for a time segment
 */
struct DiarizationSegment {
    float start;                     // Start time in seconds
    float end;                       // End time in seconds
    int speaker_id;                  // Speaker ID
    std::string speaker_label;       // Speaker label
    float confidence;                // Assignment confidence (0.0-1.0)

    DiarizationSegment() : start(0.0f), end(0.0f), speaker_id(-1), confidence(0.0f) {}
};

/**
 * @brief Complete diarization result
 */
struct DiarizationResult {
    std::vector<DiarizationSegment> segments;  // Time-aligned speaker segments
    std::vector<Speaker> speakers;             // Detected speakers with metadata
    int num_speakers;                          // Total number of speakers detected

    DiarizationResult() : num_speakers(0) {}
};

/**
 * @brief Speaker Diarization Engine
 *
 * Uses pyannote-audio ONNX models for speaker detection and separation.
 *
 * Features:
 * - Extract speaker embeddings from audio
 * - Cluster embeddings to identify unique speakers
 * - Assign speaker labels to transcription segments
 * - Multi-speaker conversation analysis
 *
 * Models:
 * - pyannote/embedding (512-dimensional speaker embeddings)
 * - pyannote/segmentation (optional, for better boundaries)
 *
 * Example usage:
 * @code
 * muninn::Diarizer diarizer("models/pyannote-embedding.onnx");
 *
 * // Load audio
 * std::vector<float> audio = load_audio("conversation.wav");
 *
 * // Run diarization
 * auto result = diarizer.diarize(audio.data(), audio.size(), 16000);
 *
 * // Assign speakers to transcription segments
 * for (auto& segment : transcription_segments) {
 *     int speaker = diarizer.get_speaker_at_time(result, segment.start);
 *     segment.speaker_id = speaker;
 *     segment.speaker_label = "Speaker " + std::to_string(speaker);
 * }
 * @endcode
 */
class MUNINN_API Diarizer {
public:
    /**
     * @brief Initialize diarizer with embedding model
     *
     * @param embedding_model_path Path to pyannote embedding ONNX model
     * @param options Diarization configuration
     */
    Diarizer(const std::string& embedding_model_path,
             const DiarizationOptions& options = DiarizationOptions());

    ~Diarizer();

    // ═══════════════════════════════════════════════════════════
    // Core Diarization
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Perform speaker diarization on audio
     *
     * @param audio_data Audio samples (mono, 16kHz)
     * @param num_samples Number of audio samples
     * @param sample_rate Sample rate (must be 16000)
     * @return DiarizationResult with speaker segments and metadata
     */
    DiarizationResult diarize(const float* audio_data,
                              size_t num_samples,
                              int sample_rate = 16000);

    /**
     * @brief Get speaker ID at specific time point
     *
     * @param result Diarization result
     * @param time_s Time point in seconds
     * @return Speaker ID at that time (-1 if no speaker)
     */
    static int get_speaker_at_time(const DiarizationResult& result, float time_s);

    /**
     * @brief Assign speakers to transcription segments
     *
     * Modifies segments in-place to add speaker_id and speaker_label.
     *
     * @param segments Transcription segments to annotate
     * @param diarization Diarization result
     */
    static void assign_speakers_to_segments(std::vector<Segment>& segments,
                                           const DiarizationResult& diarization);

    // ═══════════════════════════════════════════════════════════
    // Embedding Extraction
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Extract speaker embedding for audio segment
     *
     * @param audio_data Audio samples (mono, 16kHz)
     * @param num_samples Number of samples
     * @return 512-dimensional speaker embedding
     */
    SpeakerEmbedding extract_embedding(const float* audio_data, size_t num_samples);

    /**
     * @brief Extract embeddings for entire audio with sliding window
     *
     * @param audio_data Audio samples (mono, 16kHz)
     * @param num_samples Number of samples
     * @param sample_rate Sample rate
     * @return Vector of embeddings with time annotations
     */
    std::vector<SpeakerEmbedding> extract_embeddings(const float* audio_data,
                                                     size_t num_samples,
                                                     int sample_rate = 16000);

    // ═══════════════════════════════════════════════════════════
    // Speaker Clustering
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Cluster embeddings into speakers
     *
     * Uses hierarchical clustering with cosine similarity.
     *
     * @param embeddings Speaker embeddings to cluster
     * @return Vector of Speaker objects with clustered embeddings
     */
    std::vector<Speaker> cluster_speakers(const std::vector<SpeakerEmbedding>& embeddings);

    /**
     * @brief Calculate cosine similarity between two embeddings
     *
     * @param emb1 First embedding
     * @param emb2 Second embedding
     * @return Cosine similarity (0.0-1.0, higher = more similar)
     */
    static float cosine_similarity(const SpeakerEmbedding& emb1,
                                   const SpeakerEmbedding& emb2);

    // ═══════════════════════════════════════════════════════════
    // Speaker Management
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Set custom speaker labels
     *
     * @param result Diarization result to modify
     * @param labels Map of speaker_id -> custom label (e.g., {0: "Alice", 1: "Bob"})
     */
    static void set_speaker_labels(DiarizationResult& result,
                                   const std::map<int, std::string>& labels);

    /**
     * @brief Get speaker statistics
     *
     * @param result Diarization result
     * @param speaker_id Speaker ID
     * @return Speaker object with statistics
     */
    static Speaker get_speaker_stats(const DiarizationResult& result, int speaker_id);

    // ═══════════════════════════════════════════════════════════
    // Utilities
    // ═══════════════════════════════════════════════════════════

    /**
     * @brief Check if model is loaded and ready
     */
    bool is_ready() const;

    /**
     * @brief Get current configuration
     */
    const DiarizationOptions& get_options() const { return options_; }

private:
    DiarizationOptions options_;

    // ONNX Runtime
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> embedding_session_;
    std::unique_ptr<Ort::SessionOptions> session_options_;

    // Model metadata
    std::vector<int64_t> input_shape_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;

    // Internal methods
    void initialize_onnx_session();
    std::vector<float> run_embedding_model(const float* audio_data, size_t num_samples);
    std::vector<DiarizationSegment> embeddings_to_segments(
        const std::vector<SpeakerEmbedding>& embeddings,
        const std::vector<Speaker>& speakers);
};

/**
 * @brief Helper functions for speaker-aware transcription display
 */
namespace SpeakerFormatting {
    /**
     * @brief Format segment with speaker label
     *
     * @param segment Segment with speaker information
     * @param format Format string ("{label}: {text}" or similar)
     * @return Formatted string
     */
    MUNINN_API std::string format_speaker_text(const Segment& segment,
                                                const std::string& format = "[{label}] {text}");

    /**
     * @brief Build HTML with speaker-specific colors
     *
     * @param segment Segment with speaker information
     * @param speaker_colors Map of speaker_id -> color hex code
     * @return HTML formatted text
     */
    MUNINN_API std::string build_speaker_html(const Segment& segment,
                                               const std::map<int, std::string>& speaker_colors);

    /**
     * @brief Generate distinct colors for N speakers
     *
     * @param num_speakers Number of speakers
     * @return Map of speaker_id -> hex color
     */
    MUNINN_API std::map<int, std::string> generate_speaker_colors(int num_speakers);
}

} // namespace muninn
