#pragma once

#include "export.h"
#include "types.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace muninn {

/**
 * @brief Streaming options for real-time transcription
 */
struct StreamingOptions {
    // Core transcription options (subset of TranscribeOptions)
    std::string language = "auto";         // "auto", "en", "es", "ja", etc.
    std::string task = "transcribe";       // "transcribe" or "translate" (to English)
    int beam_size = 5;
    float temperature = 0.0f;

    // Streaming-specific
    float chunk_length_s = 2.0f;           // Process audio in N-second chunks
    float overlap_s = 0.5f;                // Overlap between chunks for context
    bool enable_vad = true;                // Use VAD to detect speech boundaries
    VADType vad_type = VADType::Energy;    // VAD algorithm

    // Quality
    float compression_ratio_threshold = 2.4f;
    float no_speech_threshold = 0.4f;

    // Word timestamps
    bool word_timestamps = true;           // Enable by default for streaming

    // Filtering
    bool filter_hallucinations = true;     // Skip low-quality segments
    int batch_size = 1;                    // Usually 1 for low-latency streaming
};

/**
 * @brief Callback for receiving completed segments in real-time
 *
 * @param segment The completed transcription segment with timing
 * @return false to stop transcription, true to continue
 */
using StreamingCallback = std::function<bool(const Segment& segment)>;

/**
 * @brief Streaming Whisper transcriber for real-time captions
 *
 * StreamingTranscriber processes audio incrementally, providing low-latency
 * transcription results suitable for live captions, subtitles, or voice commands.
 *
 * Features:
 * - Push audio chunks as they arrive (48kHz, 16kHz, any rate)
 * - Get partial/completed segments via polling or callbacks
 * - Automatic VAD-based segmentation
 * - Word-level timestamps for karaoke-style highlighting
 * - Context preservation across chunks
 *
 * Example:
 * @code
 *   muninn::StreamingTranscriber stream("models/whisper-turbo");
 *
 *   stream.start([](const Segment& seg) {
 *       std::cout << seg.text << std::endl;
 *       return true;  // Continue
 *   });
 *
 *   // Push audio chunks from microphone/OBS
 *   stream.push_audio(mic_samples, 1024, 48000);
 *
 *   // Or poll for results
 *   Segment seg;
 *   if (stream.poll_segment(seg)) {
 *       std::cout << seg.text << std::endl;
 *   }
 *
 *   stream.stop();
 * @endcode
 */
class MUNINN_API StreamingTranscriber {
public:
    /**
     * @brief Initialize streaming transcriber
     *
     * @param model_path Path to CTranslate2 Whisper model
     * @param device Device: "cuda", "cpu", "auto"
     * @param compute_type Precision: "float16", "int8", "float32"
     */
    StreamingTranscriber(const std::string& model_path,
                         const std::string& device = "cuda",
                         const std::string& compute_type = "float16");

    /**
     * @brief Initialize with ModelOptions
     */
    explicit StreamingTranscriber(const ModelOptions& options);

    ~StreamingTranscriber();

    // Move-only
    StreamingTranscriber(const StreamingTranscriber&) = delete;
    StreamingTranscriber& operator=(const StreamingTranscriber&) = delete;
    StreamingTranscriber(StreamingTranscriber&&) noexcept;
    StreamingTranscriber& operator=(StreamingTranscriber&&) noexcept;

    /**
     * @brief Start streaming session
     *
     * @param options Streaming configuration
     * @param callback Optional callback for completed segments
     */
    void start(const StreamingOptions& options = {},
               StreamingCallback callback = nullptr);

    /**
     * @brief Stop streaming and flush remaining audio
     *
     * @return Final segments from buffered audio
     */
    std::vector<Segment> stop();

    /**
     * @brief Check if streaming session is active
     */
    bool is_active() const;

    /**
     * @brief Push audio samples for transcription
     *
     * Audio is automatically resampled to 16kHz mono if needed.
     * Thread-safe - can be called from audio callback threads.
     *
     * @param samples Audio samples (interleaved if stereo)
     * @param sample_count Number of samples
     * @param sample_rate Sample rate (e.g., 48000, 16000)
     * @param channels Number of channels (1=mono, 2=stereo)
     */
    void push_audio(const float* samples,
                    size_t sample_count,
                    int sample_rate = 16000,
                    int channels = 1);

    /**
     * @brief Poll for completed segments (non-blocking)
     *
     * Call this regularly from your main loop to get transcription results.
     * Thread-safe.
     *
     * @param out_segment Output segment (only valid if returns true)
     * @return true if a new segment is available, false otherwise
     */
    bool poll_segment(Segment& out_segment);

    /**
     * @brief Get all completed segments so far
     *
     * Useful for exporting SRT subtitles or reviewing transcript.
     *
     * @return Vector of all completed segments since start()
     */
    std::vector<Segment> get_all_segments() const;

    /**
     * @brief Get current partial text (last N seconds)
     *
     * Returns the most recent transcription, including partial results.
     * Useful for showing "typing" effect in live captions.
     *
     * @param include_partial Include incomplete segments
     * @return Current caption text
     */
    std::string get_current_text(bool include_partial = true) const;

    /**
     * @brief Clear all accumulated segments and reset state
     *
     * Useful when switching topics or resetting the stream.
     */
    void reset();

    /**
     * @brief Get streaming statistics
     */
    struct Stats {
        size_t total_samples_processed;   // Total audio samples processed
        size_t segments_completed;        // Number of completed segments
        float total_audio_duration_s;     // Total audio duration processed
        float average_latency_ms;         // Average segment completion latency
        float buffer_fill_ratio;          // Current buffer fill (0.0-1.0)
    };
    Stats get_stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace muninn
