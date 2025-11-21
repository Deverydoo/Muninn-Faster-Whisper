#pragma once

#include <string>
#include <vector>

namespace muninn {

/**
 * @brief Word-level timestamp information
 */
struct Word {
    float start;             // Start time in seconds
    float end;               // End time in seconds
    std::string word;        // The word text
    float probability;       // Confidence score
};

/**
 * @brief Transcription segment with timing and metadata
 */
struct Segment {
    int id;                         // Segment index
    float start;                    // Start time in seconds
    float end;                      // End time in seconds
    std::string text;               // Transcribed text
    std::vector<Word> words;        // Word-level timestamps (if enabled)

    // Quality metrics
    float temperature;              // Sampling temperature used
    float avg_logprob;              // Average log probability
    float compression_ratio;        // Text compression ratio
    float no_speech_prob;           // Probability of no speech

    Segment() : id(0), start(0.0f), end(0.0f),
                temperature(0.0f), avg_logprob(0.0f),
                compression_ratio(0.0f), no_speech_prob(0.0f) {}
};

/**
 * @brief Complete transcription result
 */
struct TranscribeResult {
    std::vector<Segment> segments;   // All transcription segments
    std::string language;            // Detected/specified language
    float language_probability;      // Language detection confidence
    float duration;                  // Total audio duration in seconds

    TranscribeResult() : language_probability(0.0f), duration(0.0f) {}

    // Iterator support for range-based for loops
    auto begin() const { return segments.begin(); }
    auto end() const { return segments.end(); }
    auto begin() { return segments.begin(); }
    auto end() { return segments.end(); }
};

/**
 * @brief Transcription configuration options
 */
struct TranscribeOptions {
    // Language and task
    std::string language = "auto";         // "en", "es", "auto", etc.
    std::string task = "transcribe";       // "transcribe" or "translate"

    // Decoding parameters
    int beam_size = 5;                     // Beam search width
    float temperature = 0.0f;              // Sampling temperature (0 = greedy)
    float patience = 1.0f;                 // Beam search patience
    float length_penalty = 1.0f;           // Length penalty factor
    float repetition_penalty = 1.0f;       // Repetition penalty
    int no_repeat_ngram_size = 0;          // Prevent n-gram repetitions

    // Voice Activity Detection
    bool vad_filter = true;                // Enable VAD
    float vad_threshold = 0.5f;            // VAD energy threshold
    int vad_min_speech_duration_ms = 250;  // Minimum speech duration
    int vad_max_speech_duration_s = 30;    // Maximum speech duration
    int vad_min_silence_duration_ms = 2000; // Minimum silence for split

    // Hallucination filtering
    float compression_ratio_threshold = 2.4f;  // Max compression ratio
    float log_prob_threshold = -1.0f;          // Min average log probability
    float no_speech_threshold = 0.6f;          // Max no-speech probability

    // Timestamps
    bool word_timestamps = false;          // Extract word-level timing

    // Processing
    int batch_size = 16;                   // Batch size for parallel processing
    int max_length = 448;                  // Maximum tokens per segment
};

} // namespace muninn
