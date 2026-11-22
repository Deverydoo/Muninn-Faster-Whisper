#pragma once

#include "export.h"
#include "types.h"
#include <cstddef>
#include <cstdint>
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
    //
    // "auto" detects the language once, on the first chunk that has speech,
    // and keeps it for the rest of the session; Segment::language reports it.
    // Any other value must be a language the model knows ("en", "es", "ja",
    // ...), or start() throws. English-only (.en) models always use English.
    std::string language = "auto";
    std::string task = "transcribe";       // "transcribe" or "translate" (to English)
    int beam_size = 5;
    float temperature = 0.0f;

    // Streaming-specific
    //
    // Audio is decoded in windows of chunk_length_s seconds (at most 30). Each
    // window starts overlap_s seconds before the previous one ended (at most
    // half a chunk), so a word cut by one window's edge is heard whole by the
    // next. Each word is emitted once: by the window in which it sits furthest
    // from an edge. That holds the last overlap_s / 2 seconds of a window back
    // until the next window has been decoded (get_current_text(true) shows
    // that held-back text).
    float chunk_length_s = 2.0f;
    float overlap_s = 0.5f;
    bool enable_vad = true;                // Use VAD to skip windows without speech
    VADType vad_type = VADType::Energy;    // VAD algorithm

    // Path to silero_vad.onnx. Required when vad_type is Silero (or when Auto
    // resolves to it); ignored otherwise. Without it the neural VAD cannot be
    // constructed and the stream falls back to energy VAD.
    std::string vad_model_path;

    // Energy VAD is the safe default because it has no model dependency. When a
    // neural VAD is requested but cannot be built — missing model, ONNX Runtime
    // too old, load failure — falling back keeps transcription working instead
    // of failing the session outright. Set false to surface the error instead.
    bool vad_fallback_to_energy = true;

    // Quality, with faster-whisper's meanings:
    //  - a window is silence when no_speech_prob > no_speech_threshold AND
    //    avg_logprob < log_prob_threshold (always checked);
    //  - a window is a decoding loop when its zlib compression ratio is above
    //    compression_ratio_threshold (checked when filter_hallucinations).
    float compression_ratio_threshold = 2.4f;
    float no_speech_threshold = 0.4f;
    float log_prob_threshold = -1.0f;

    // Word timestamps (Segment::words), from CTranslate2's alignment as in the
    // offline path. Words are always aligned internally (that is how windows
    // are joined); this only says whether they are returned.
    bool word_timestamps = true;

    // Filtering
    bool filter_hallucinations = true;     // Drop windows that look like a decoding loop

    // Prompt each window with the text already emitted (the last 223 tokens),
    // as the offline path does. Reset after a window is dropped as a loop.
    bool condition_on_previous_text = true;

    // Limits, so a stalled consumer can't grow memory without bound.
    //  - Audio waiting to be decoded beyond max_buffered_s is dropped, oldest
    //    first (Stats::samples_dropped). Timestamps stay correct.
    //  - Segments waiting for poll_segment() beyond max_pending_segments are
    //    dropped, oldest first (Stats::segments_dropped). get_all_segments()
    //    and stop() still return them.
    float max_buffered_s = 60.0f;
    size_t max_pending_segments = 1000;
};

/**
 * @brief Callback for receiving completed segments in real-time
 *
 * Called on the transcriber's worker thread, with no lock held, so it may call
 * push_audio(), poll_segment(), get_all_segments(), get_current_text(),
 * get_stats(), last_error() and reset(). It must not call start() or stop():
 * those throw std::logic_error when called from a callback. To end the session
 * from the callback, return false, then call stop() from your own thread.
 *
 * An exception thrown by the callback is caught and reported through the error
 * callback; streaming carries on.
 *
 * @param segment The completed transcription segment with timing
 * @return false to stop transcription, true to continue
 */
using StreamingCallback = std::function<bool(const Segment& segment)>;

/**
 * @brief Callback for errors on the worker thread
 *
 * A window that fails to decode (out of GPU memory, a CTranslate2 error, a
 * VAD failure) is skipped and reported here; streaming carries on with the
 * next window. Called on the worker thread with no lock held, under the same
 * rules as StreamingCallback.
 */
using StreamingErrorCallback = std::function<void(const std::string& message)>;

/**
 * @brief Streaming Whisper transcriber for real-time captions
 *
 * StreamingTranscriber processes audio incrementally, providing low-latency
 * transcription results suitable for live captions, subtitles, or voice commands.
 *
 * Features:
 * - Push audio from any thread, at any sample rate and channel count
 * - Get completed segments via polling or a callback
 * - VAD to skip silence
 * - Word-level timestamps for karaoke-style highlighting
 * - Language detection on the first speech ("auto")
 * - Each window prompted with the text before it
 *
 * Timestamps are seconds since start() (or the last reset()), counted in
 * audio pushed, not wall-clock time.
 *
 * Example:
 * @code
 *   muninn::StreamingTranscriber stream("models/whisper-turbo");
 *
 *   stream.start({}, [](const muninn::Segment& seg) {
 *       std::cout << seg.text << std::endl;
 *       return true;  // Continue
 *   });
 *
 *   // Push audio chunks from microphone/OBS
 *   stream.push_audio(mic_samples, 1024, 48000);
 *
 *   // Or poll for results
 *   muninn::Segment seg;
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
     * @param device Device: "cuda", "cpu", "auto" (CUDA when it works, else CPU)
     * @param compute_type Precision: "float16", "int8", "int8_float16",
     *        "float32", or anything else for the model's own type. On the CPU,
     *        float16 becomes float32 and int8_float16 becomes int8.
     */
    StreamingTranscriber(const std::string& model_path,
                         const std::string& device = "cuda",
                         const std::string& compute_type = "float16");

    /**
     * @brief Initialize with ModelOptions
     *
     * Uses device, compute_type, device_index and intra_threads (CPU threads,
     * 0 = CTranslate2's default). inter_threads is not used: streaming decodes
     * one window at a time, so extra replicas would only take memory.
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
     * Clears the segments, audio and statistics of any earlier session.
     * Throws std::runtime_error when a session is already active,
     * std::invalid_argument for a language the model doesn't know, and
     * std::logic_error when called from a callback.
     *
     * @param options Streaming configuration
     * @param callback Optional callback for completed segments
     */
    void start(const StreamingOptions& options = {},
               StreamingCallback callback = nullptr);

    /**
     * @brief Stop streaming, decode the audio still buffered, and end the session
     *
     * Waits for the worker to finish the remaining audio (the final segments
     * are also passed to the callback, unless it returned false earlier).
     * Throws std::logic_error when called from a callback (it would wait for
     * itself).
     *
     * @return Every segment of the session, in order
     */
    std::vector<Segment> stop();

    /**
     * @brief True from start() until stop(), or until the callback returned false
     */
    bool is_active() const;

    /**
     * @brief Push audio samples for transcription
     *
     * Audio is downmixed to mono (all channels averaged) and resampled to
     * 16 kHz; the resampler keeps its phase between calls. Never waits for a
     * decode: it takes the buffer's lock only to append. Can be called from
     * audio callback threads; calls from several threads at once are
     * serialised.
     *
     * @param samples Audio samples (interleaved when channels > 1)
     * @param sample_count Number of floats in samples (frames x channels)
     * @param sample_rate Sample rate in Hz (e.g. 48000, 16000); must be > 0
     * @param channels Number of channels; must be >= 1
     * @return false when nothing was taken: no active session, or a sample
     *         rate or channel count that isn't valid (logged as an error)
     */
    bool push_audio(const float* samples,
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
     * @brief The text so far
     *
     * @param include_partial Also include the words already decoded but held
     *        back for the next window (see StreamingOptions::overlap_s). They
     *        may still change.
     * @return Completed segments' text, joined with spaces
     */
    std::string get_current_text(bool include_partial = true) const;

    /**
     * @brief Clear all accumulated segments and reset state
     *
     * Drops the buffered audio, the segments, the context and the detected
     * language, and starts the timeline again at 0. The session stays active.
     */
    void reset();

    /**
     * @brief Set the callback for worker errors (see StreamingErrorCallback)
     *
     * May be called at any time, before or during a session.
     */
    void set_error_callback(StreamingErrorCallback callback);

    /**
     * @brief The most recent worker error, or "" if none since start()
     */
    std::string last_error() const;

    /**
     * @brief Get streaming statistics
     */
    struct Stats {
        size_t total_samples_processed = 0;  // Samples pushed, after resampling to 16 kHz
        size_t segments_completed = 0;       // Number of completed segments
        float total_audio_duration_s = 0;    // Audio pushed, in seconds
        float average_latency_ms = 0;        // Average decode time per window
        float buffer_fill_ratio = 0;         // Audio waiting, as a fraction of one chunk
        size_t chunks_failed = 0;            // Windows skipped because decoding threw
        uint64_t samples_dropped = 0;        // Audio dropped because the buffer was full
        uint64_t segments_dropped = 0;       // Segments dropped from the poll queue
    };
    Stats get_stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace muninn
