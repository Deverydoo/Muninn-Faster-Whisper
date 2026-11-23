#pragma once

#include "export.h"
#include <string>
#include <vector>
#include <set>
#include <map>

namespace muninn {

/**
 * @brief Track type hint for VAD decisions
 *
 * Used to tell Muninn what kind of audio each track contains,
 * enabling smarter VAD selection per track.
 */
enum class TrackType {
    Unknown,        // Unknown track type (use heuristics)
    Microphone,     // Voice recording (mic, commentary) - use VAD
    SystemAudio,    // Desktop/system audio - disable VAD
    GameAudio,      // Game audio with music/effects - disable VAD
    MovieAudio,     // Movie/video audio - disable VAD
    Music,          // Music track - disable VAD
    VoiceChat       // Discord/voice chat - use VAD
};

/**
 * @brief VAD algorithm type (user-selectable in GUI)
 *
 * Implemented: Auto (default), None, Energy, Silero. TEN and WebRTC are not:
 * TEN runs Silero and WebRTC runs Energy, with a warning.
 */
enum class VADType {
    Auto,           // Pick per track (track hints, then the audio's noise floor and range)
    None,           // No VAD - process all audio
    Energy,         // Energy-based VAD (no model needed)
    Silero,         // Silero VAD ONNX (needs ONNX Runtime and silero_model_path; else Energy)
    TEN,            // Not implemented yet: runs Silero instead
    WebRTC          // Not implemented: runs Energy instead
};

/**
 * @brief Compute precision type
 */
enum class ComputeType {
    Float32,        // Full precision (most accurate, slowest)
    Float16,        // Half precision (fast on GPU)
    Int8,           // 8-bit quantized (fastest, good quality)
    Int8Float16,    // Mixed precision
    Auto            // Auto-detect best for device
};

/**
 * @brief Device type for inference
 */
enum class DeviceType {
    Auto,           // Auto-detect (prefer CUDA if available)
    CUDA,           // NVIDIA GPU
    CPU             // CPU only
};

/**
 * @brief Word emphasis level for styling
 *
 * Muninn does not measure loudness, so it never sets this. It stays for
 * callers who work out emphasis themselves and pass it to the styling
 * presets in word_styling.h.
 */
enum class EmphasisLevel {
    VeryLow,        // Whispered/very quiet (< 20% intensity)
    Low,            // Quiet speech (20-40% intensity)
    Normal,         // Normal speech (40-70% intensity)
    High,           // Emphasized/louder (70-90% intensity)
    VeryHigh        // Shouted/very loud (> 90% intensity)
};

/**
 * @brief Word-level timestamp information
 */
struct Word {
    float start;             // Start time in seconds
    float end;               // End time in seconds
    std::string word;        // The word text
    float probability;       // Confidence score (0.0-1.0)

    Word() : start(0.0f), end(0.0f), probability(1.0f) {}
};

/**
 * @brief Transcription segment with timing and metadata
 */
struct Segment {
    int id;                         // Segment index
    int track_id;                   // Audio track index (for multi-track files)
    float start;                    // Start time in seconds
    float end;                      // End time in seconds
    std::string text;               // Transcribed/translated text
    std::vector<Word> words;        // Word-level timestamps (if enabled)

    // Language detection (streaming mode)
    std::string language;           // Detected language code ("en", "es", "ja", etc.)
    float language_probability;     // Language detection confidence

    // Speaker diarization (multi-speaker mode)
    int speaker_id;                 // Speaker ID (-1 if not assigned, 0+ for identified speakers)
    std::string speaker_label;      // Speaker label ("Speaker 0", "Alice", etc.)
    float speaker_confidence;       // Speaker assignment confidence (0.0-1.0)

    // Quality metrics
    float temperature;              // Sampling temperature used
    float avg_logprob;              // Average log probability
    float compression_ratio;        // Text compression ratio
    float no_speech_prob;           // Probability of no speech

    // Translation (when post-transcription translation is enabled)
    std::string translated_text;    // Translated text (empty if no translation)
    std::string translation_target; // Target language code ("es", "fr", etc.)

    Segment() : id(0), track_id(0), start(0.0f), end(0.0f),
                language_probability(0.0f),
                speaker_id(-1), speaker_confidence(0.0f),
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
    bool was_cancelled;              // True if transcription was cancelled by user

    // Failures. A window or track that failed (CUDA out of memory, a decode
    // error) used to vanish: the run reported success with fewer segments.
    // When nothing at all succeeded, transcribe() throws instead.
    int failed_windows = 0;          // 30 s windows whose decode threw
    int failed_tracks = 0;           // audio tracks that couldn't be extracted, prepared or decoded
    std::string error;               // the last failure, empty when none

    TranscribeResult() : language_probability(0.0f), duration(0.0f), was_cancelled(false) {}

    // Iterator support for range-based for loops
    auto begin() const { return segments.begin(); }
    auto end() const { return segments.end(); }
    auto begin() { return segments.begin(); }
    auto end() { return segments.end(); }
};

/**
 * @brief Transcription configuration options
 *
 * All options configurable from the Qt GUI settings panel
 */
struct TranscribeOptions {
    // ═══════════════════════════════════════════════════════════
    // Language and Task
    // ═══════════════════════════════════════════════════════════
    std::string language = "auto";         // "en", "es", "auto", etc.
    std::string task = "transcribe";       // "transcribe" or "translate"

    // ═══════════════════════════════════════════════════════════
    // Decoding Parameters
    // ═══════════════════════════════════════════════════════════
    int beam_size = 5;                     // Beam search width (1-10)
    float temperature = 0.0f;              // Sampling temperature (0 = greedy)
    std::vector<float> temperature_fallback = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};  // Temperature fallback sequence
    int best_of = 5;                       // Samples per window above temperature 0 (the best is kept)
    float patience = 1.0f;                 // Beam search patience
    float length_penalty = 1.0f;           // Length penalty factor
    float repetition_penalty = 1.0f;       // Repetition penalty
    int no_repeat_ngram_size = 0;          // Prevent n-gram repetitions

    // ═══════════════════════════════════════════════════════════
    // Voice Activity Detection (VAD)
    // ═══════════════════════════════════════════════════════════
    VADType vad_type = VADType::Auto;      // VAD algorithm to use (Auto = smart selection)
    bool vad_filter = true;                // Enable VAD (shortcut for vad_type != None)
    float vad_threshold = 0.02f;           // Energy reads it as an RMS floor when <= 0.1; Silero reads it
                                           // as a speech probability when > 0.1, and uses 0.25 otherwise
    int vad_min_speech_duration_ms = 250;  // Minimum speech duration to keep
    int vad_max_speech_duration_s = 30;    // Maximum speech duration before split (Silero splits at a pause)
    int vad_min_silence_duration_ms = 500; // Minimum silence for split (both detectors)
    int vad_speech_pad_ms = 300;           // Padding around speech segments
    int vad_audio_preamble_ms = 2000;      // Not read at present: Energy and Silero use 2000 ms. Speech that
                                           // starts within it is pulled back to 0; it never adds a segment
                                           // of its own.

    // Silero VAD specific
    std::string silero_model_path;         // Path to silero_vad.onnx (required for VADType::Silero)

    // TEN VAD specific
    std::string ten_vad_model_path;        // Not used: TEN isn't implemented yet and VADType::TEN runs Silero

    // ═══════════════════════════════════════════════════════════
    // Hallucination Filtering
    // ═══════════════════════════════════════════════════════════
    float compression_ratio_threshold = 2.4f;  // Max compression ratio
    float log_prob_threshold = -1.0f;          // Min average log probability
    float no_speech_threshold = 0.4f;          // Max no-speech probability
    float hallucination_silence_threshold = 0.0f;  // Fraction (0-1) of a segment that must overlap
                                                   // VAD speech, or it is dropped. 0 = off. Not
                                                   // faster-whisper's option of the same name (seconds).

    // ═══════════════════════════════════════════════════════════
    // Timestamps
    // ═══════════════════════════════════════════════════════════
    bool word_timestamps = false;          // Extract word-level timing
    float clip_start = 0.0f;               // Start time for clip (0 = beginning)
    float clip_end = -1.0f;                // End time for clip (-1 = full audio)

    // ═══════════════════════════════════════════════════════════
    // Token Suppression
    // ═══════════════════════════════════════════════════════════
    bool suppress_blank = true;            // Suppress blank outputs at segment start
    std::vector<int> suppress_tokens = {-1};  // Token IDs to suppress (-1 = use model defaults)

    // ═══════════════════════════════════════════════════════════
    // Multi-Track Processing
    // ═══════════════════════════════════════════════════════════
    std::set<int> skip_tracks;             // Track indices to skip (empty = process all)
    bool skip_silent_tracks = true;        // Auto-skip tracks with no audio signal
    std::map<int, TrackType> track_hints;  // Track type hints for VAD decisions (track_id -> TrackType)

    // ═══════════════════════════════════════════════════════════
    // Speaker Diarization ("Who Said What")
    // ═══════════════════════════════════════════════════════════
    bool enable_diarization = false;       // Diarize after transcribing (file input only, on the CPU)
    std::string diarization_model_path;    // Path to pyannote embedding model (ONNX)
    float diarization_threshold = 0.7f;    // Speaker clustering threshold (0.5-0.9)
    int diarization_min_speakers = 1;      // Not used
    int diarization_max_speakers = 10;     // Maximum number of speakers (0 = unlimited)

    // ═══════════════════════════════════════════════════════════
    // Performance Tuning
    // ═══════════════════════════════════════════════════════════
    int batch_size = 4;                    // Windows decoded together when sequential = false (1-32)
    int max_length = 448;                  // Maximum tokens per segment

    // Phase C pipeline: overlap CPU prep (VAD + mel) of upcoming track with
    // GPU inference of current track. Off by default — the parallel-extract
    // path (Phase A) is always on; this flag adds the prep-ahead stage on top.
    // Toggle off to revert to sequential per-track prep+infer if a regression
    // is suspected.
    bool concurrent_pipeline = false;

    // ═══════════════════════════════════════════════════════════
    // Prompt / Context
    // ═══════════════════════════════════════════════════════════
    std::string initial_prompt;            // Initial prompt to condition model
    std::vector<std::string> hotwords;     // Not used yet
    bool condition_on_previous = true;     // Use previous text as context
    // Long audio decoded as faster-whisper does: 30 s windows from a seek position,
    // with fallback and context. Default since October 2026: on Loki Studio's test
    // clips 2-3% WER against faster-whisper where batching gave 11-15%, and faster.
    // false = the older batched path (batch_size windows at once, no context).
    bool sequential = true;
    float prompt_reset_on_temperature = 0.5f;  // Reset prompt context when T exceeds this
};

/**
 * @brief Model initialization options
 *
 * Passed to Transcriber constructor
 */
struct ModelOptions {
    std::string model_path;                // Path to CTranslate2 model directory

    // Device configuration
    DeviceType device = DeviceType::Auto;  // Inference device
    ComputeType compute_type = ComputeType::Float16;  // Precision

    // Threading (0 = auto-detect)
    int intra_threads = 0;                 // Threads per operation (CPU)
    int inter_threads = 1;                 // Parallel operations (workers)

    // GPU options
    int device_index = 0;                  // GPU index for multi-GPU systems

    // Helper to convert enums to strings for CTranslate2
    std::string device_string() const {
        switch (device) {
            case DeviceType::CUDA: return "cuda";
            case DeviceType::CPU: return "cpu";
            default: return "auto";
        }
    }

    std::string compute_type_string() const {
        switch (compute_type) {
            case ComputeType::Float32: return "float32";
            case ComputeType::Float16: return "float16";
            case ComputeType::Int8: return "int8";
            case ComputeType::Int8Float16: return "int8_float16";
            default: return "default";
        }
    }
};

} // namespace muninn
