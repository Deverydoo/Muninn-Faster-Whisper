#pragma once

#include "export.h"
#include "types.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace muninn {

/**
 * @brief Audio file information
 *
 * get_audio_info() is not available in this build: it always throws (it sits
 * behind a WITH_HEIMDALL switch that nothing sets). AudioExtractor::open()
 * with get_track_count() and get_duration() gives the same information.
 */
struct MUNINN_API AudioInfo {
    float duration;              // Duration in seconds
    int sample_rate;             // Native sample rate
    int num_tracks;              // Number of audio tracks/streams
    std::vector<int> channels;   // Channels per track
};

/**
 * @brief Progress callback for GUI integration
 *
 * @param track_index Current track being processed (0-based)
 * @param total_tracks Total number of tracks
 * @param progress Progress within current track (0.0-1.0)
 * @param message Status message for display
 * @return false to cancel transcription, true to continue
 */
using ProgressCallback = std::function<bool(int track_index, int total_tracks, float progress, const std::string& message)>;

/**
 * @brief High-level Whisper transcription API
 *
 * A C++ port of faster-whisper's transcribe():
 *
 * - every audio track of a file transcribed separately (Segment::track_id)
 * - voice activity detection to skip silence (Energy or Silero)
 * - 30 s windows with faster-whisper's seek, context and temperature fallback
 * - word timestamps from CTranslate2's alignment
 * - language detection
 *
 * A window or track that fails is counted in TranscribeResult::failed_windows
 * and failed_tracks; transcribe() throws when nothing succeeded.
 *
 * Example:
 * @code
 *   muninn::Transcriber transcriber("models/whisper-large-v3-turbo");
 *   auto result = transcriber.transcribe("audio.mp3");
 *   for (const auto& segment : result.segments) {
 *       std::cout << segment.text << std::endl;
 *   }
 * @endcode
 */
class MUNINN_API Transcriber {
public:
    /**
     * @brief Initialize Whisper transcriber with ModelOptions
     *
     * Recommended constructor for full control over model initialization.
     *
     * @param options Model configuration including device, compute type, threading
     * @throws std::runtime_error if model cannot be loaded
     */
    explicit Transcriber(const ModelOptions& options);

    /**
     * @brief Initialize Whisper transcriber (convenience overload)
     *
     * @param model_path Path to CTranslate2-converted Whisper model
     * @param device Device to use: "cuda", "cpu", "auto"
     * @param compute_type Precision: "float16", "int8", "float32"
     *
     * @throws std::runtime_error if model cannot be loaded
     */
    Transcriber(const std::string& model_path,
                const std::string& device = "cuda",
                const std::string& compute_type = "float16");

    /**
     * @brief Destructor
     */
    ~Transcriber();

    // Move-only (no copying)
    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;
    Transcriber(Transcriber&&) noexcept;
    Transcriber& operator=(Transcriber&&) noexcept;

    /**
     * @brief Unload the model and release GPU resources
     *
     * Waits for a running transcribe() to finish, then frees the model's GPU
     * memory. The Transcriber can't transcribe again afterwards (transcribe()
     * throws); create a new one. A second call does nothing.
     *
     * @param timeout_ms Not used at present (the wait has no limit)
     */
    void unload_model(int timeout_ms = 3000);

    /**
     * @brief Get audio file information without transcribing
     *
     * Not available at present: always throws std::runtime_error (see
     * AudioInfo). Use AudioExtractor instead.
     *
     * @param audio_path Path to audio/video file
     * @return Audio metadata including duration and track count
     */
    static AudioInfo get_audio_info(const std::string& audio_path);

    /**
     * @brief Transcribe audio from file
     *
     * Anything FFmpeg can read (MP3, WAV, M4A, FLAC, MP4, MKV, MOV, ...).
     * Each audio track is decoded to 16 kHz mono and transcribed separately;
     * Segment::track_id says which track a segment came from.
     *
     * @param audio_path Path to audio/video file
     * @param options Transcription configuration
     * @param progress_callback Optional callback for progress updates (GUI integration)
     * @return Transcription result with segments and metadata
     *
     * @throws std::runtime_error if file cannot be read or transcription fails
     */
    TranscribeResult transcribe(
        const std::string& audio_path,
        const TranscribeOptions& options = {},
        ProgressCallback progress_callback = nullptr
    );

    /**
     * @brief Transcribe audio from memory (single track)
     *
     * @param audio_samples Audio samples (mono, float32, normalized to [-1, 1])
     * @param sample_rate Must be 16000: other rates throw (there is no resampling here;
     *                    AudioExtractor and the file overload resample for you)
     * @param options Transcription configuration
     * @param track_id Track identifier for multi-track results (default 0)
     * @param total_tracks Total number of tracks being processed
     * @param progress_callback Optional callback for progress updates (GUI integration)
     * @return Transcription result with segments and metadata
     *
     * @throws std::runtime_error if transcription fails
     */
    TranscribeResult transcribe(
        const std::vector<float>& audio_samples,
        int sample_rate = 16000,
        const TranscribeOptions& options = {},
        int track_id = 0,
        int total_tracks = 1,
        ProgressCallback progress_callback = nullptr
    );

    /**
     * @brief Get model information
     *
     * @return Model metadata (language support, mel bins, etc.)
     */
    struct ModelInfo {
        bool is_multilingual;
        int n_mels;
        int num_languages;
        std::string model_type;  // "tiny", "base", "small", etc.
    };
    ModelInfo get_model_info() const;

    /**
     * @brief Get device information after initialization
     *
     * @return Device info including actual device used, compute type, and GPU details
     */
    struct DeviceInfo {
        std::string device;        // "cuda" or "cpu"
        std::string compute_type;  // "float16", "int8", "float32"
        bool is_cuda;              // True if using CUDA
        int device_index;          // GPU index (0 for CPU)
        std::string gpu_name;      // GPU name (empty for CPU)
        size_t gpu_memory_mb;      // GPU total memory in MB (0 for CPU)
    };
    DeviceInfo get_device_info() const;

    /**
     * @brief Request cancellation of ongoing transcription
     *
     * Thread-safe method that can be called from any thread (e.g., UI thread)
     * to request cancellation of an ongoing transcribe() call.
     *
     * The cancellation is cooperative: the run stops at its next checkpoint
     * (before each track, between VAD and mel, before each window or batch).
     * The segments finished so far are returned on the original timeline,
     * with TranscribeResult::was_cancelled set.
     *
     * The flag is cleared when the running transcribe() returns, so a cancel()
     * sent just before transcribe() starts still cancels that run.
     */
    void cancel();

    /**
     * @brief Reset cancellation flag
     *
     * Only needed when cancel() was called while no transcription was running
     * and the next run should not be cancelled. transcribe() clears the flag
     * itself when it returns.
     */
    void reset_cancel();

    /**
     * @brief Check if cancellation was requested
     * @return true if cancel() was called and not yet reset
     */
    bool is_cancelled() const;

    /**
     * @brief Clear internal caches to free GPU memory
     *
     * Frees CTranslate2's cached allocator blocks; the model stays loaded.
     * Useful between files in a long batch job.
     */
    void clear_cache();

    /**
     * @brief Text to the model's token ids, as Whisper's tokenizer makes them
     * (GPT-2 byte-level BPE from the model's tokenizer.json). Used for the
     * initial prompt; empty if the model has no vocabulary.
     */
    std::vector<size_t> encode(const std::string& text) const;

private:
    // Both public constructors land here.
    Transcriber(const std::string& model_path, const std::string& device,
                const std::string& compute_type, int intra_threads, int device_index);

    class Impl;  // Forward declaration for pimpl idiom
    std::unique_ptr<Impl> pimpl_;
};

} // namespace muninn
