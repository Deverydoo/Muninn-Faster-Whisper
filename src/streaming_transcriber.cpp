/**
 * @file streaming_transcriber.cpp
 * @brief Real-time streaming transcription using CTranslate2 Whisper
 *
 * Audio pushed from any thread goes into a shared buffer (streaming_audio.h).
 * A worker thread takes overlapping windows from it, decodes each one with no
 * lock held, and hands the results to the callback and the poll queue.
 *
 * How windows are joined: window k covers [F, F + chunk) and the next starts
 * at F + chunk - overlap. Every word gets a time from CTranslate2's alignment,
 * and a word is emitted by the window in which its midpoint falls between
 * F + overlap/2 and F + chunk - overlap/2, i.e. the window where it is furthest
 * from an edge. Those ranges tile the timeline, so each word comes out once.
 * Words past the upper cut are held back ("withheld") until the next window
 * has been decoded; if that window has no speech, or the session ends, the
 * held-back words are emitted as they are.
 */

#include "muninn/streaming_transcriber.h"
#include "muninn/logging.h"
#include "muninn/mel_spectrogram.h"
#include "muninn/vad.h"
#include "muninn/silero_vad.h"
#include "streaming_audio.h"
#include "text_decode.h"
#include <ctranslate2/devices.h>
#include <ctranslate2/models/whisper.h>
#include <ctranslate2/storage_view.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace muninn {

namespace {

constexpr int kSampleRate = 16000;
constexpr int kWhisperFrames = 3000;        // 30 s of mel frames, what Whisper was trained on
constexpr float kTokensPerSecond = 50.0f;   // alignment time steps (20 ms each)
constexpr size_t kMaxContextTokens = 223;   // half of Whisper's 448-token text context
constexpr size_t kMinFlushSamples = kSampleRate / 4;  // under 250 ms of new audio isn't worth a decode

std::string trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

// Maps a time in VAD-filtered audio (speech pieces joined end to end) back to
// the window's own timeline. Pieces are cut exactly as filter_silence() cuts
// them, so the sample counts match.
class SpeechMap {
public:
    SpeechMap() = default;
    SpeechMap(const std::vector<SpeechSegment>& segments, size_t total_samples) {
        size_t filtered = 0;
        for (const auto& seg : segments) {
            const int start = std::max(0, static_cast<int>(seg.start * kSampleRate));
            const int end = std::min(static_cast<int>(total_samples),
                                     static_cast<int>(seg.end * kSampleRate));
            if (end <= start) continue;
            pieces_.push_back({filtered, static_cast<size_t>(start), static_cast<size_t>(end - start)});
            filtered += static_cast<size_t>(end - start);
        }
    }

    // Seconds in filtered audio -> seconds in the window.
    float to_window(float t) const {
        if (pieces_.empty()) return t;
        const double s = std::max(0.0, static_cast<double>(t) * kSampleRate);
        for (const auto& p : pieces_) {
            if (s < static_cast<double>(p.filtered_start + p.length)) {
                return static_cast<float>((p.window_start + (s - p.filtered_start)) / kSampleRate);
            }
        }
        const auto& last = pieces_.back();
        return static_cast<float>((last.window_start + last.length +
                                   (s - (last.filtered_start + last.length))) / kSampleRate);
    }

private:
    struct Piece {
        size_t filtered_start;
        size_t window_start;
        size_t length;
    };
    std::vector<Piece> pieces_;
};

// A word with absolute times and the text tokens it came from.
struct TimedWord {
    std::string raw;               // decoded text, with its leading space if any
    float start = 0.0f;
    float end = 0.0f;
    float probability = 0.0f;
    std::vector<size_t> token_ids;
    float midpoint() const { return 0.5f * (start + end); }
};

ctranslate2::ComputeType to_ct2_compute_type(ComputeType type, ctranslate2::Device device) {
    const bool cpu = (device == ctranslate2::Device::CPU);
    switch (type) {
        case ComputeType::Float32: return ctranslate2::ComputeType::FLOAT32;
        case ComputeType::Float16:
            return cpu ? ctranslate2::ComputeType::FLOAT32 : ctranslate2::ComputeType::FLOAT16;
        case ComputeType::Int8: return ctranslate2::ComputeType::INT8;
        case ComputeType::Int8Float16:
            return cpu ? ctranslate2::ComputeType::INT8 : ctranslate2::ComputeType::INT8_FLOAT16;
        default: return ctranslate2::ComputeType::DEFAULT;
    }
}

}  // namespace

// =======================
// StreamingTranscriber::Impl
// =======================

class StreamingTranscriber::Impl {
public:
    explicit Impl(const ModelOptions& options) : model_opts_(options) {
        load_model();
    }

    ~Impl() {
        try {
            stop_worker();
        } catch (...) {
        }
    }

    // ---------------------------------------------------------------
    // Session control
    // ---------------------------------------------------------------

    void start(const StreamingOptions& options, StreamingCallback callback) {
        refuse_on_worker_thread("start");
        std::lock_guard<std::mutex> session(session_mutex_);

        if (active_ && !session_ended_) {
            throw std::runtime_error("Streaming session already active");
        }
        // A session that ended through its callback still has a finished
        // worker to collect.
        stop_worker();

        StreamingOptions opts = options;
        validate_and_clamp(opts);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            options_ = opts;
        }
        callback_ = std::move(callback);

        setup_vad();  // may throw when vad_fallback_to_energy is false

        // Fresh session state.
        audio_.reset(static_cast<size_t>(std::max(options_.max_buffered_s, options_.chunk_length_s * 2.0f) *
                                         kSampleRate));
        {
            std::lock_guard<std::mutex> lock(push_mutex_);
            resampler_.reset();
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            completed_segments_.clear();
            pending_segments_.clear();
            pending_segments_.set_cap(options_.max_pending_segments);
            latencies_ms_.clear();
            stats_ = {};
            tail_text_.clear();
            last_error_.clear();
        }
        reset_worker_state();
        stop_requested_ = false;
        session_ended_ = false;
        worker_id_ = std::thread::id();

        active_ = true;
        worker_ = std::thread(&Impl::run, this);

        Logger::info("Streaming started: chunk " + std::to_string(options_.chunk_length_s) +
                     " s, overlap " + std::to_string(options_.overlap_s) + " s, language " +
                     options_.language);
    }

    std::vector<Segment> stop() {
        refuse_on_worker_thread("stop");
        std::lock_guard<std::mutex> session(session_mutex_);
        if (!active_ && !worker_.joinable()) {
            return {};
        }
        stop_worker();
        std::lock_guard<std::mutex> lock(state_mutex_);
        return completed_segments_;
    }

    bool is_active() const {
        return active_ && !session_ended_;
    }

    void reset() {
        // No session lock: reset() is allowed from the callback, and stop()
        // holds the session lock while it waits for the worker. Clearing the
        // buffer starts a new generation; the worker sees that on its next
        // window, drops anything it was decoding, and clears its own state.
        audio_.clear();
        {
            std::lock_guard<std::mutex> lock(push_mutex_);
            resampler_.reset();
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            completed_segments_.clear();
            pending_segments_.clear();
            latencies_ms_.clear();
            stats_ = {};
            tail_text_.clear();
        }
        wake_.notify_all();
    }

    // ---------------------------------------------------------------
    // Input
    // ---------------------------------------------------------------

    bool push_audio(const float* samples, size_t sample_count, int sample_rate, int channels) {
        if (sample_rate <= 0) {
            Logger::error("push_audio: sample_rate must be positive (got " + std::to_string(sample_rate) + ")");
            return false;
        }
        if (channels < 1) {
            Logger::error("push_audio: channels must be 1 or more (got " + std::to_string(channels) + ")");
            return false;
        }
        if (!active_ || session_ended_) return false;
        if (samples == nullptr || sample_count == 0) return true;

        size_t pushed = 0;
        {
            // Serialises producers (the resampler's phase is shared) and keeps
            // the conversion off the buffer lock. The worker never takes it.
            std::lock_guard<std::mutex> lock(push_mutex_);
            streaming_detail::downmix_to_mono(samples, sample_count, channels, mono_);
            converted_.clear();
            if (sample_rate == kSampleRate && resampler_.in_rate() == kSampleRate) {
                converted_.swap(mono_);
            } else {
                resampler_.set_rates(sample_rate, kSampleRate);
                resampler_.process(mono_.data(), mono_.size(), converted_);
            }
            if (converted_.empty()) return true;
            audio_.push(converted_.data(), converted_.size());
            pushed = converted_.size();
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            stats_.total_samples_processed += pushed;
            stats_.total_audio_duration_s =
                static_cast<float>(stats_.total_samples_processed) / kSampleRate;
        }
        wake_.notify_one();
        return true;
    }

    // ---------------------------------------------------------------
    // Output
    // ---------------------------------------------------------------

    bool poll_segment(Segment& out_segment) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return pending_segments_.pop(out_segment);
    }

    std::vector<Segment> get_all_segments() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return completed_segments_;
    }

    std::string get_current_text(bool include_partial) const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::string text;
        for (const auto& seg : completed_segments_) {
            if (seg.text.empty()) continue;
            if (!text.empty()) text += " ";
            text += seg.text;
        }
        if (include_partial && !tail_text_.empty()) {
            if (!text.empty()) text += " ";
            text += tail_text_;
        }
        return text;
    }

    void set_error_callback(StreamingErrorCallback callback) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        error_callback_ = std::move(callback);
    }

    std::string last_error() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_error_;
    }

    StreamingTranscriber::Stats get_stats() const {
        StreamingTranscriber::Stats s;
        const size_t buffered = audio_.size();
        const uint64_t dropped_audio = audio_.dropped();
        std::lock_guard<std::mutex> lock(state_mutex_);
        s.total_samples_processed = stats_.total_samples_processed;
        s.segments_completed = stats_.segments_completed;
        s.total_audio_duration_s = stats_.total_audio_duration_s;
        s.chunks_failed = stats_.chunks_failed;
        s.samples_dropped = dropped_audio;
        s.segments_dropped = pending_segments_.dropped();
        if (!latencies_ms_.empty()) {
            float sum = 0.0f;
            for (float lat : latencies_ms_) sum += lat;
            s.average_latency_ms = sum / static_cast<float>(latencies_ms_.size());
        }
        const size_t chunk_samples = static_cast<size_t>(options_.chunk_length_s * kSampleRate);
        s.buffer_fill_ratio = chunk_samples > 0 ? static_cast<float>(buffered) / chunk_samples : 0.0f;
        return s;
    }

private:
    // ---------------------------------------------------------------
    // Model
    // ---------------------------------------------------------------

    void load_model() {
        ctranslate2::ReplicaPoolConfig pool_config;
        pool_config.num_threads_per_replica =
            model_opts_.intra_threads > 0 ? static_cast<size_t>(model_opts_.intra_threads) : 0;

        auto build = [&](ctranslate2::Device device) {
            const std::vector<int> indices = {device == ctranslate2::Device::CUDA ? model_opts_.device_index : 0};
            model_ = std::make_unique<ctranslate2::models::Whisper>(
                model_opts_.model_path, device, to_ct2_compute_type(model_opts_.compute_type, device),
                indices, false, pool_config);
            on_cuda_ = (device == ctranslate2::Device::CUDA);
        };

        switch (model_opts_.device) {
            case DeviceType::CPU:
                build(ctranslate2::Device::CPU);
                break;
            case DeviceType::CUDA:
                build(ctranslate2::Device::CUDA);  // asked for CUDA: a failure is an error
                break;
            default: {
                int gpus = 0;
                try {
                    gpus = ctranslate2::get_device_count(ctranslate2::Device::CUDA);
                } catch (...) {
                    gpus = 0;
                }
                if (gpus > model_opts_.device_index) {
                    try {
                        build(ctranslate2::Device::CUDA);
                    } catch (const std::exception& e) {
                        Logger::warn(std::string("Streaming: CUDA failed (") + e.what() + "), using the CPU");
                        model_.reset();
                        build(ctranslate2::Device::CPU);
                    }
                } else {
                    Logger::info("Streaming: no CUDA device, using the CPU");
                    build(ctranslate2::Device::CPU);
                }
                break;
            }
        }

        multilingual_ = model_->is_multilingual();
        n_mels_ = static_cast<int>(model_->n_mels());
        mel_converter_ = std::make_unique<MelSpectrogram>(kSampleRate, 400, n_mels_, 160);

        // Token ids come from the model's own vocabulary: the fixed tables
        // differ between model families (large-v3 has an extra language).
        const auto base = model_->get_first_replica().model();
        auto whisper_model = std::dynamic_pointer_cast<const ctranslate2::models::WhisperModel>(base);
        if (!whisper_model) {
            throw std::runtime_error("Streaming: the model's vocabulary is not available");
        }
        vocab_ = &whisper_model->get_vocabulary();
        sot_id_ = require_token("<|startoftranscript|>");
        eot_id_ = require_token("<|endoftext|>");
        timestamp_begin_ = require_token("<|0.00|>");
        startofprev_id_ = token_id("<|startofprev|>");
        transcribe_id_ = token_id("<|transcribe|>");
        translate_id_ = token_id("<|translate|>");

        Logger::info("Streaming model loaded: " + model_opts_.model_path + " (" +
                     (on_cuda_ ? "CUDA" : "CPU") + ", " + std::to_string(n_mels_) + " mel bands, " +
                     (multilingual_ ? "multilingual" : "English-only") + ")");
    }

    // The id of a token, or SIZE_MAX when the vocabulary doesn't have it.
    size_t token_id(const std::string& token) const {
        const size_t id = vocab_->to_id(token);
        if (id >= vocab_->size() || vocab_->to_token(id) != token) {
            return std::numeric_limits<size_t>::max();
        }
        return id;
    }

    size_t require_token(const std::string& token) const {
        const size_t id = token_id(token);
        if (id == std::numeric_limits<size_t>::max()) {
            throw std::runtime_error("Streaming: the model's vocabulary has no " + token);
        }
        return id;
    }

    // ---------------------------------------------------------------
    // Options and VAD
    // ---------------------------------------------------------------

    void validate_and_clamp(StreamingOptions& opts) const {
        if (!(opts.chunk_length_s > 0.0f)) opts.chunk_length_s = 2.0f;
        opts.chunk_length_s = std::min(opts.chunk_length_s, 30.0f);
        opts.chunk_length_s = std::max(opts.chunk_length_s, 0.5f);
        if (!(opts.overlap_s >= 0.0f)) opts.overlap_s = 0.0f;
        opts.overlap_s = std::min(opts.overlap_s, opts.chunk_length_s / 2.0f);
        if (opts.beam_size < 1) opts.beam_size = 1;
        if (opts.max_pending_segments == 0) opts.max_pending_segments = 1;
        if (!(opts.max_buffered_s > 0.0f)) opts.max_buffered_s = 60.0f;

        if (opts.task != "transcribe" && opts.task != "translate") {
            throw std::invalid_argument("Streaming: task must be \"transcribe\" or \"translate\"");
        }
        if (!multilingual_) {
            if (opts.language != "auto" && opts.language != "en") {
                throw std::invalid_argument("Streaming: this model is English-only (language \"" +
                                            opts.language + "\")");
            }
            if (opts.task == "translate") {
                Logger::warn("Streaming: an English-only model can't translate; transcribing instead");
                opts.task = "transcribe";
            }
        } else if (opts.language != "auto" &&
                   token_id("<|" + opts.language + "|>") == std::numeric_limits<size_t>::max()) {
            throw std::invalid_argument("Streaming: the model doesn't know language \"" + opts.language + "\"");
        }
    }

    static VADOptions energy_vad_options() {
        VADOptions vad_opts;
        vad_opts.threshold = 0.02f;
        vad_opts.min_speech_duration_ms = 200;
        vad_opts.min_silence_duration_ms = 300;
        vad_opts.speech_pad_ms = 50;
        // No preamble: every window looks like a fresh file to the detector,
        // so the default 2 s preamble made every window "speech", silence was
        // decoded forever, and hallucinations followed.
        vad_opts.audio_preamble_ms = 0;
        return vad_opts;
    }

    void setup_vad() {
        // A Silero instance is kept across sessions when the model path has not
        // changed: callers that treat one utterance as one session would
        // otherwise pay an ONNX session creation on every turn. Its recurrent
        // state is cleared instead, which is what actually has to be per-session.
        // A session without a model path leaves the cached instance alone (it is
        // simply not used); only a different model path replaces it.
        vad_.reset();
        use_silero_ = false;
        if (silero_vad_ && !options_.vad_model_path.empty() &&
            silero_loaded_path_ != options_.vad_model_path) {
            silero_vad_.reset();
        }
        if (silero_vad_) {
            silero_vad_->reset_state();
        }

        if (!options_.enable_vad) return;

        // Resolve Auto here rather than at the filter site; streaming has no
        // track context, and picking once per session keeps the hot loop free
        // of decisions. Silero is the better endpointer on clean speech, so
        // Auto prefers it when a model is configured.
        VADType wanted = options_.vad_type;
        if (wanted == VADType::Auto) {
            wanted = options_.vad_model_path.empty() ? VADType::Energy : VADType::Silero;
        }

        if (wanted == VADType::Silero || wanted == VADType::TEN) {
            if (wanted == VADType::TEN) {
                // TEN has no streaming wiring; Silero covers the same need.
                Logger::warn("Streaming: TEN VAD is not wired into streaming, using Silero");
                wanted = VADType::Silero;
            }

            if (!is_silero_vad_available()) {
                Logger::warn("Streaming: Silero VAD not compiled in (MUNINN_USE_SILERO_VAD)");
                wanted = VADType::Energy;
            } else if (options_.vad_model_path.empty()) {
                Logger::warn("Streaming: Silero VAD requested but vad_model_path is empty");
                wanted = VADType::Energy;
            } else if (silero_vad_) {
                Logger::info("Streaming VAD: Silero (reused, " + options_.vad_model_path + ")");
            } else {
                try {
                    SileroVADOptions silero_opts;
                    silero_opts.model_path = options_.vad_model_path;
                    silero_opts.threshold = 0.25f;
                    silero_opts.min_speech_duration_ms = 200;
                    // Short, so the endpoint lands quickly after the speaker
                    // stops — this is a live command loop, not a subtitle pass.
                    silero_opts.min_silence_duration_ms = 100;
                    silero_opts.speech_pad_ms = 100;
                    // Streaming already chunks the audio, so the long-segment
                    // splitting the file path needs would only fight it.
                    silero_opts.max_speech_duration_s = 30;
                    // No preamble, for the same reason as the energy VAD.
                    silero_opts.audio_preamble_ms = 0;
                    // Kept on CPU: the model is ~1.8 MB and the GPU is busy
                    // with the Whisper decode this VAD is feeding.
                    silero_opts.use_gpu = false;

                    silero_vad_ = std::make_unique<SileroVAD>(silero_opts);
                    if (!silero_vad_->is_ready()) {
                        silero_vad_.reset();
                        throw std::runtime_error("model loaded but reported not ready");
                    }
                    // Only recorded once the instance is known good, so a
                    // failed load cannot be mistaken for a reusable one.
                    silero_loaded_path_ = options_.vad_model_path;
                    Logger::info("Streaming VAD: Silero (" + options_.vad_model_path + ")");
                } catch (const std::exception& e) {
                    silero_vad_.reset();
                    Logger::error(std::string("Streaming: Silero VAD init failed: ") + e.what());
                    if (!options_.vad_fallback_to_energy) {
                        throw;
                    }
                    wanted = VADType::Energy;
                }
            }
        }

        // The cached Silero instance may outlive the session that loaded it,
        // so its existence says nothing about this session: only a session
        // that resolved to Silero (and has the instance) uses it.
        use_silero_ = (wanted == VADType::Silero && silero_vad_ != nullptr);

        if (!use_silero_ && wanted != VADType::None) {
            vad_ = std::make_unique<VAD>(energy_vad_options());
            Logger::info("Streaming VAD: energy");
        }
    }

    // ---------------------------------------------------------------
    // Worker
    // ---------------------------------------------------------------

    void refuse_on_worker_thread(const char* what) const {
        if (worker_running_ && std::this_thread::get_id() == worker_id_.load()) {
            throw std::logic_error(std::string("StreamingTranscriber::") + what +
                                   "() was called from a streaming callback. Return false from "
                                   "the segment callback to end the session, then call stop() "
                                   "from your own thread.");
        }
    }

    // Ends the worker (it flushes the remaining audio first) and the session.
    void stop_worker() {
        stop_requested_ = true;
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        active_ = false;
    }

    void run() {
        worker_id_ = std::this_thread::get_id();
        worker_running_ = true;

        while (!stop_requested_ && !session_ended_) {
            bool worked = false;
            try {
                worked = step(false);
            } catch (const std::exception& e) {
                report_error(std::string("streaming worker: ") + e.what());
            } catch (...) {
                report_error("streaming worker: unknown error");
            }
            if (!worked && !stop_requested_ && !session_ended_) {
                std::unique_lock<std::mutex> lock(wake_mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(50));
            }
        }

        // Flush: decode what is left (but not the already-decoded overlap on
        // its own), then release the held-back words. Skipped when the
        // callback asked to stop: it wants no more segments. A window that
        // fails is skipped like any other; a run of failures ends the flush.
        if (!session_ended_) {
            int failures = 0;
            while (!session_ended_ && failures < 3) {
                bool more = false;
                try {
                    more = step(true);
                    failures = 0;
                } catch (const std::exception& e) {
                    report_error(std::string("streaming flush: ") + e.what());
                    more = true;
                    ++failures;
                } catch (...) {
                    report_error("streaming flush: unknown error");
                    more = true;
                    ++failures;
                }
                if (!more) break;
            }
            if (!session_ended_) {
                try {
                    std::vector<Segment> out;
                    release_withheld(out);
                    publish(out);
                } catch (const std::exception& e) {
                    report_error(std::string("streaming flush: ") + e.what());
                } catch (...) {
                    report_error("streaming flush: unknown error");
                }
            }
        }

        worker_running_ = false;
    }

    // Per-session state that only the worker touches.
    struct WorkerState {
        uint64_t generation = 0;      // the audio buffer generation this state belongs to
        uint64_t decoded_until = 0;   // absolute end of the last decoded window
        uint64_t emit_from = 0;       // words with a midpoint before this were emitted already
        std::vector<TimedWord> withheld;
        std::vector<size_t> context;  // emitted text tokens, for the prompt
        std::string language;         // "" until known
        float language_probability = 0.0f;
        int next_segment_id = 0;
        float last_emitted_end = 0.0f;  // end of the last word emitted (absolute seconds)
        std::deque<std::string> recent_words;  // the last few words emitted, normalised
    };

    void reset_worker_state() {
        ws_ = WorkerState{};
        if (!multilingual_) {
            ws_.language = "en";
            ws_.language_probability = 1.0f;
        } else if (options_.language != "auto") {
            ws_.language = options_.language;
            ws_.language_probability = 1.0f;
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        tail_text_.clear();
    }

    // Takes one window from the buffer and decodes it. Returns false when there
    // was nothing to do. `flush` is the final pass: shorter windows are taken.
    bool step(bool flush) {
        const auto snap = audio_.peek();
        if (snap.generation != ws_.generation) {
            // start() or reset() cleared the buffer: a new timeline.
            reset_worker_state();
            ws_.generation = snap.generation;
        }
        const uint64_t end = snap.front + snap.available;
        const size_t chunk_samples = static_cast<size_t>(options_.chunk_length_s * kSampleRate);
        const size_t overlap_samples = static_cast<size_t>(options_.overlap_s * kSampleRate);

        if (!flush) {
            if (snap.available < chunk_samples) return false;
        } else {
            const uint64_t new_from = std::max(snap.front, ws_.decoded_until);
            if (end <= new_from || end - new_from < kMinFlushSamples) {
                // Only the overlap (already decoded) or a sliver is left.
                audio_.consume_to(end, snap.generation);
                return false;
            }
        }

        std::vector<float> chunk;
        const auto taken = audio_.copy_front(chunk_samples, chunk);
        if (chunk.empty()) return false;
        const uint64_t front = taken.front;
        const size_t n = chunk.size();
        const bool is_last = flush && (front + n >= taken.front + taken.available);

        const uint64_t upper_cut = is_last ? std::numeric_limits<uint64_t>::max()
                                           : front + n - overlap_samples / 2;
        const uint64_t next_front = is_last ? front + n
                                            : front + (n > overlap_samples ? n - overlap_samples : n);

        std::vector<Segment> out;

        // Audio was dropped (buffer full) past the held-back words: no later
        // window will cover them, so they go out now.
        if (front > ws_.emit_from) {
            release_withheld(out);
            ws_.emit_from = front;
        }

        const auto started = std::chrono::steady_clock::now();
        bool decoded_words = false;
        try {
            decoded_words = decode_window(chunk, front, upper_cut, out);
        } catch (...) {
            // Skip this window so the loop can't spin on it, then report.
            ws_.decoded_until = front + n;
            audio_.consume_to(next_front, taken.generation);
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                stats_.chunks_failed++;
            }
            publish(out);
            throw;
        }

        if (!decoded_words) {
            // No speech, or nothing usable: the previous window's held-back
            // words won't be decoded again, and the next window may emit from
            // its very start.
            release_withheld(out);
            ws_.emit_from = std::max(ws_.emit_from, next_front);
        } else if (!is_last) {
            ws_.emit_from = upper_cut;
        }
        ws_.decoded_until = front + n;

        const float latency_ms = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - started).count();

        // A reset() while decoding makes this window stale: drop its output.
        if (audio_.peek().generation != taken.generation) {
            return true;
        }
        audio_.consume_to(next_front, taken.generation);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            latencies_ms_.push_back(latency_ms);
            if (latencies_ms_.size() > 100) latencies_ms_.pop_front();
            tail_text_ = withheld_text();
        }
        publish(out);
        return true;
    }

    // Decodes one window. Emits the words that belong to it into `out` and
    // holds back the rest. Returns false when the window had no usable speech.
    bool decode_window(const std::vector<float>& chunk, uint64_t front, uint64_t upper_cut,
                       std::vector<Segment>& out) {
        // VAD: skip silence, and remember where the speech pieces came from.
        std::vector<float> speech;
        SpeechMap speech_map;
        const bool silero_active = use_silero_ && silero_vad_;
        if (options_.enable_vad && (silero_active || vad_)) {
            std::vector<SpeechSegment> speech_segs;
            if (silero_active) {
                try {
                    speech = silero_vad_->filter_silence(chunk, kSampleRate, speech_segs);
                } catch (const std::exception& e) {
                    // A neural VAD that starts throwing mid-stream must not
                    // take the session down; drop to energy for the rest of it.
                    report_error(std::string("Silero VAD failed mid-stream (") + e.what() +
                                 "), switching to energy VAD");
                    use_silero_ = false;
                    silero_vad_.reset();
                    vad_ = std::make_unique<VAD>(energy_vad_options());
                    speech = vad_->filter_silence(chunk, kSampleRate, speech_segs);
                }
            } else {
                speech = vad_->filter_silence(chunk, kSampleRate, speech_segs);
            }
            // "No segments" is silence as well as "no samples": the energy VAD
            // returns the input unchanged when it found nothing.
            if (speech.empty() || speech_segs.empty()) return false;
            speech_map = SpeechMap(speech_segs, chunk.size());
        } else {
            speech = chunk;
        }

        // Mel, padded with zeros to 30 s as the offline path (and
        // faster-whisper's pad_or_trim) does: Whisper only ever saw 30 s
        // windows, and a short unpadded one makes it stop early or invent text.
        std::vector<std::vector<float>> mel;
        const int n_frames = mel_converter_->compute(speech, mel);
        if (n_frames < 10 || mel.empty()) return false;
        const int n_mels = static_cast<int>(mel[0].size());
        const int padded_frames = std::max(n_frames, kWhisperFrames);
        std::vector<float> flat;
        flat.reserve(static_cast<size_t>(padded_frames) * n_mels);
        for (int m = 0; m < n_mels; ++m) {
            for (int f = 0; f < n_frames; ++f) flat.push_back(mel[f][m]);
            flat.insert(flat.end(), static_cast<size_t>(padded_frames - n_frames), 0.0f);
        }
        ctranslate2::StorageView features(
            ctranslate2::Shape{1, static_cast<ctranslate2::dim_t>(n_mels),
                               static_cast<ctranslate2::dim_t>(padded_frames)},
            flat);

        if (ws_.language.empty()) detect_language(features);

        // Prompt: <|startofprev|> earlier text <|startoftranscript|> <|lang|> <|task|>
        std::vector<size_t> sot_sequence = {sot_id_};
        if (multilingual_) {
            sot_sequence.push_back(require_token("<|" + ws_.language + "|>"));
            const size_t task_id = (options_.task == "translate") ? translate_id_ : transcribe_id_;
            if (task_id != std::numeric_limits<size_t>::max()) sot_sequence.push_back(task_id);
        }
        std::vector<size_t> prompt;
        if (options_.condition_on_previous_text && !ws_.context.empty() &&
            startofprev_id_ != std::numeric_limits<size_t>::max()) {
            prompt.push_back(startofprev_id_);
            const size_t keep = std::min(ws_.context.size(), kMaxContextTokens);
            prompt.insert(prompt.end(), ws_.context.end() - static_cast<std::ptrdiff_t>(keep),
                          ws_.context.end());
        }
        prompt.insert(prompt.end(), sot_sequence.begin(), sot_sequence.end());

        ctranslate2::models::WhisperOptions whisper_options;
        whisper_options.beam_size = static_cast<size_t>(options_.beam_size);
        whisper_options.patience = 1.0f;
        whisper_options.length_penalty = 1.0f;
        whisper_options.repetition_penalty = 1.0f;
        whisper_options.no_repeat_ngram_size = 0;
        whisper_options.max_length = 448;
        whisper_options.sampling_topk = (options_.temperature > 0) ? 0 : 1;
        whisper_options.sampling_temperature = options_.temperature > 0 ? options_.temperature : 1.0f;
        whisper_options.num_hypotheses = 1;
        whisper_options.return_scores = true;
        whisper_options.return_no_speech_prob = true;
        whisper_options.max_initial_timestamp_index = 50;
        whisper_options.suppress_blank = true;
        whisper_options.suppress_tokens = {-1};

        auto futures = model_->generate(features, std::vector<std::vector<size_t>>{prompt}, whisper_options);
        if (futures.empty()) return false;
        auto result = futures[0].get();
        if (result.sequences.empty() || result.sequences_ids.empty()) return false;

        const auto& tokens = result.sequences[0];
        const auto& ids = result.sequences_ids[0];
        const std::string full_text = text::extract_text(tokens);

        // faster-whisper's measures, as offline: the score CTranslate2 returns
        // is normalised by length^length_penalty; undo that, then average over
        // the tokens plus the end token.
        float avg_logprob = 0.0f;
        if (result.has_scores() && !result.scores.empty()) {
            const size_t seq_len = ids.size();
            const float cum_logprob = result.scores[0] *
                std::pow(static_cast<float>(seq_len), whisper_options.length_penalty);
            avg_logprob = cum_logprob / static_cast<float>(seq_len + 1);
        }
        const float compression_ratio = text::compression_ratio_of(full_text);

        if (result.no_speech_prob > options_.no_speech_threshold &&
            avg_logprob < options_.log_prob_threshold) {
            return false;  // silence
        }
        if (options_.filter_hallucinations && compression_ratio > options_.compression_ratio_threshold) {
            Logger::warn("Streaming: dropped a window that looks like a decoding loop (compression " +
                         std::to_string(compression_ratio) + ")");
            ws_.context.clear();  // don't feed the loop back in
            return false;
        }

        // Text tokens only (no timestamps, no specials).
        std::vector<std::string> text_tokens;
        std::vector<size_t> text_ids;
        for (size_t i = 0; i < ids.size() && i < tokens.size(); ++i) {
            if (ids[i] >= eot_id_) continue;
            text_tokens.push_back(tokens[i]);
            text_ids.push_back(ids[i]);
        }
        if (text_ids.empty() || trim(full_text).empty()) return false;

        // Per-token start times in the filtered audio, from the alignment.
        const float speech_seconds = static_cast<float>(speech.size()) / kSampleRate;
        std::vector<float> token_start(text_ids.size() + 1, 0.0f);
        std::vector<float> token_prob(text_ids.size(), 1.0f);
        if (!align_tokens(features, sot_sequence, text_ids, n_frames, speech_seconds, token_start, token_prob)) {
            // Alignment failed: spread the tokens over the speech by length.
            size_t total = 0;
            for (const auto& t : text_tokens) total += std::max<size_t>(1, text::clean_token(t).size());
            size_t acc = 0;
            for (size_t i = 0; i < text_tokens.size(); ++i) {
                token_start[i] = speech_seconds * static_cast<float>(acc) / static_cast<float>(total);
                acc += std::max<size_t>(1, text::clean_token(text_tokens[i]).size());
            }
            token_start[text_tokens.size()] = speech_seconds;
        }

        // Words, timed on the absolute timeline.
        auto word_tokens = text::split_to_word_tokens(text_tokens, text_ids, eot_id_, ws_.language);
        text::merge_punctuations(word_tokens);
        const double window_start_s = static_cast<double>(front) / kSampleRate;
        std::vector<TimedWord> words;
        float prev_end = 0.0f;
        for (const auto& wt : word_tokens) {
            if (wt.token_indices.empty()) continue;
            const size_t first = wt.token_indices.front();
            const size_t last = wt.token_indices.back();
            TimedWord w;
            w.raw = wt.text;
            const float s_filtered = token_start[first];
            const float e_filtered = std::max(token_start[last + 1], s_filtered);
            w.start = static_cast<float>(window_start_s + speech_map.to_window(s_filtered));
            w.end = static_cast<float>(window_start_s + speech_map.to_window(e_filtered));
            w.start = std::max(w.start, prev_end);
            w.end = std::max(w.end, w.start);
            float p = 0.0f;
            for (size_t idx : wt.token_indices) {
                p += token_prob[idx];
                w.token_ids.push_back(text_ids[idx]);
            }
            w.probability = p / static_cast<float>(wt.token_indices.size());
            prev_end = w.end;
            words.push_back(std::move(w));
        }
        if (words.empty()) return false;
        limit_edge_word_durations(words);

        // This window's view replaces the previous window's held-back words.
        ws_.withheld.clear();

        const double emit_from_s = static_cast<double>(ws_.emit_from) / kSampleRate;
        const double upper_cut_s = (upper_cut == std::numeric_limits<uint64_t>::max())
                                       ? std::numeric_limits<double>::infinity()
                                       : static_cast<double>(upper_cut) / kSampleRate;
        // Words the previous window already emitted.
        words.erase(std::remove_if(words.begin(), words.end(),
                                   [&](const TimedWord& w) { return w.midpoint() < emit_from_s; }),
                    words.end());
        drop_repeated_join_words(words, emit_from_s);

        std::vector<TimedWord> emitted;
        for (auto& w : words) {
            const double mid = w.midpoint();
            if (mid >= upper_cut_s) {
                ws_.withheld.push_back(std::move(w));   // the next window decides
            } else {
                emitted.push_back(std::move(w));
            }
        }

        last_metrics_ = {avg_logprob, compression_ratio, result.no_speech_prob};
        if (!emitted.empty()) out.push_back(make_segment(emitted));
        return true;
    }

    // Lowercase ASCII with punctuation removed, for comparing words.
    static std::string normalize_word(const std::string& raw) {
        std::string out;
        for (unsigned char c : raw) {
            if (c >= 0x80) out.push_back(static_cast<char>(c));
            else if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
        }
        return out;
    }

    // The midpoint split relies on both windows timing a word alike. Near
    // the join they can disagree by a few hundred milliseconds, and the
    // word comes out twice ("You can't see this." / "this, but..."). So the
    // leading words of a window, while still within overlap_s of the join, are
    // dropped when they repeat the last words emitted (up to three).
    void drop_repeated_join_words(std::vector<TimedWord>& words, double join_s) const {
        if (words.empty() || ws_.recent_words.empty()) return;
        // Only right after a window that emitted up to the join; after a
        // silence the last words are too old to be repeats.
        const double gap = std::max(1.0, 2.0 * options_.overlap_s);
        if (ws_.last_emitted_end < join_s - gap) return;
        const double near = join_s + std::max(0.3, static_cast<double>(options_.overlap_s));
        const size_t max_m = std::min<size_t>({3, ws_.recent_words.size(), words.size()});
        for (size_t m = max_m; m >= 1; --m) {
            if (words[m - 1].midpoint() >= near) continue;
            bool same = true;
            for (size_t i = 0; i < m && same; ++i) {
                const std::string& previous = ws_.recent_words[ws_.recent_words.size() - m + i];
                same = !previous.empty() && previous == normalize_word(words[i].raw);
            }
            if (same) {
                words.erase(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(m));
                return;
            }
        }
    }

    // The alignment tends to stretch the first word back to the start of the
    // window (or across a silence the VAD removed) and the last word forward
    // to the end. faster-whisper caps such words at twice the median word
    // duration (the median itself capped at 0.7 s); the same here.
    static void limit_edge_word_durations(std::vector<TimedWord>& words) {
        std::vector<float> durations;
        for (const auto& w : words) {
            if (w.end > w.start) durations.push_back(w.end - w.start);
        }
        float median = 0.7f;
        if (!durations.empty()) {
            std::nth_element(durations.begin(), durations.begin() + durations.size() / 2, durations.end());
            median = std::min(0.7f, durations[durations.size() / 2]);
        }
        const float max_duration = std::max(2.0f * median, 0.2f);
        TimedWord& first = words.front();
        if (first.end - first.start > max_duration) first.start = first.end - max_duration;
        TimedWord& last = words.back();
        if (last.end - last.start > max_duration) last.end = last.start + max_duration;
    }

    // Fills token_start (seconds, one per text token plus the end) and
    // token_prob from CTranslate2's alignment, as faster-whisper's
    // find_alignment does: a token starts where the alignment path first
    // reaches it, and ends where the next token starts.
    bool align_tokens(const ctranslate2::StorageView& features, const std::vector<size_t>& sot_sequence,
                      const std::vector<size_t>& text_ids, int n_frames, float speech_seconds,
                      std::vector<float>& token_start, std::vector<float>& token_prob) {
        try {
            auto futures = model_->align(features, sot_sequence, {text_ids},
                                         {static_cast<size_t>(n_frames)}, 7);
            if (futures.empty()) return false;
            const auto aligned = futures[0].get();
            if (aligned.alignments.empty()) return false;

            const size_t count = text_ids.size();
            std::vector<float> first_time(count + 1, -1.0f);
            float last_time = 0.0f;
            for (const auto& [text_index, time_index] : aligned.alignments) {
                const float t = static_cast<float>(time_index) / kTokensPerSecond;
                last_time = std::max(last_time, t);
                if (text_index >= 0 && static_cast<size_t>(text_index) <= count &&
                    first_time[static_cast<size_t>(text_index)] < 0.0f) {
                    first_time[static_cast<size_t>(text_index)] = t;
                }
            }
            if (first_time[count] < 0.0f) first_time[count] = std::max(last_time, 0.0f);
            // Fill gaps forward and keep the times in order.
            float prev = 0.0f;
            for (size_t i = 0; i <= count; ++i) {
                if (first_time[i] < 0.0f) first_time[i] = prev;
                first_time[i] = std::min(std::max(first_time[i], prev), speech_seconds);
                prev = first_time[i];
                token_start[i] = first_time[i];
            }
            for (size_t i = 0; i < count && i < aligned.text_token_probs.size(); ++i) {
                token_prob[i] = aligned.text_token_probs[i];
            }
            return true;
        } catch (const std::exception& e) {
            Logger::warn(std::string("Streaming: word alignment failed, spreading words evenly: ") + e.what());
            return false;
        }
    }

    void detect_language(const ctranslate2::StorageView& features) {
        try {
            auto futures = model_->detect_language(features);
            if (!futures.empty()) {
                auto probs = futures[0].get();
                if (!probs.empty()) {
                    auto best = std::max_element(probs.begin(), probs.end(),
                        [](const auto& a, const auto& b) { return a.second < b.second; });
                    std::string code = best->first;
                    if (text::is_bracket_token(code)) code = code.substr(2, code.size() - 4);
                    if (token_id("<|" + code + "|>") != std::numeric_limits<size_t>::max()) {
                        ws_.language = code;
                        ws_.language_probability = best->second;
                        Logger::info("Streaming: detected language " + code + " (probability " +
                                     std::to_string(best->second) + ")");
                        return;
                    }
                }
            }
            report_error("language detection returned nothing usable; using English");
        } catch (const std::exception& e) {
            report_error(std::string("language detection failed (") + e.what() + "); using English");
        }
        ws_.language = "en";
        ws_.language_probability = 0.0f;
    }

    Segment make_segment(std::vector<TimedWord> words) {
        // Never start before what was already emitted (a word at a window's
        // leading edge can be aligned slightly early).
        for (auto& w : words) {
            w.start = std::max(w.start, ws_.last_emitted_end);
            w.end = std::max(w.end, w.start);
            ws_.last_emitted_end = std::max(ws_.last_emitted_end, w.end);
            ws_.recent_words.push_back(normalize_word(w.raw));
            if (ws_.recent_words.size() > 3) ws_.recent_words.pop_front();
        }

        Segment seg;
        seg.id = ws_.next_segment_id++;
        seg.track_id = 0;
        std::string raw;
        for (const auto& w : words) raw += w.raw;
        seg.text = trim(raw);
        seg.start = words.front().start;
        seg.end = std::max(words.back().end, seg.start);
        if (options_.word_timestamps) {
            for (const auto& w : words) {
                Word word;
                word.word = trim(w.raw);
                word.start = w.start;
                word.end = w.end;
                word.probability = w.probability;
                seg.words.push_back(word);
            }
        }
        seg.language = ws_.language.empty() ? "en" : ws_.language;
        seg.language_probability = ws_.language_probability;
        seg.temperature = options_.temperature;
        seg.avg_logprob = last_metrics_.avg_logprob;
        seg.compression_ratio = last_metrics_.compression_ratio;
        seg.no_speech_prob = last_metrics_.no_speech_prob;

        for (const auto& w : words) {
            ws_.context.insert(ws_.context.end(), w.token_ids.begin(), w.token_ids.end());
        }
        if (ws_.context.size() > kMaxContextTokens) {
            ws_.context.erase(ws_.context.begin(),
                              ws_.context.end() - static_cast<std::ptrdiff_t>(kMaxContextTokens));
        }
        return seg;
    }

    void release_withheld(std::vector<Segment>& out) {
        if (ws_.withheld.empty()) return;
        auto words = std::move(ws_.withheld);
        ws_.withheld.clear();
        const double emit_from_s = static_cast<double>(ws_.emit_from) / kSampleRate;
        words.erase(std::remove_if(words.begin(), words.end(),
                                   [&](const TimedWord& w) { return w.midpoint() < emit_from_s; }),
                    words.end());
        if (!words.empty()) {
            out.push_back(make_segment(words));
            // Their time is covered now.
            const uint64_t past = static_cast<uint64_t>(std::ceil(words.back().end * kSampleRate));
            ws_.emit_from = std::max(ws_.emit_from, past);
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        tail_text_.clear();
    }

    std::string withheld_text() const {
        std::string raw;
        for (const auto& w : ws_.withheld) raw += w.raw;
        return trim(raw);
    }

    // Stores the segments, then calls the callback with no lock held.
    void publish(const std::vector<Segment>& segments) {
        if (segments.empty()) return;
        StreamingCallback callback;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            for (const auto& seg : segments) {
                completed_segments_.push_back(seg);
                pending_segments_.push(seg);
                stats_.segments_completed++;
            }
            callback = callback_;
        }
        if (!callback) return;
        for (const auto& seg : segments) {
            if (session_ended_) break;
            try {
                if (!callback(seg)) {
                    session_ended_ = true;
                }
            } catch (const std::exception& e) {
                report_error(std::string("segment callback threw: ") + e.what());
            } catch (...) {
                report_error("segment callback threw an unknown exception");
            }
        }
    }

    void report_error(const std::string& message) {
        Logger::error("Streaming: " + message);
        StreamingErrorCallback callback;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            last_error_ = message;
            callback = error_callback_;
        }
        if (callback) {
            try {
                callback(message);
            } catch (...) {
                // An error handler that throws has nowhere left to report to.
            }
        }
    }

    // Configuration
    ModelOptions model_opts_;
    StreamingOptions options_;
    StreamingCallback callback_;
    StreamingErrorCallback error_callback_;

    // Model
    std::unique_ptr<ctranslate2::models::Whisper> model_;
    const ctranslate2::Vocabulary* vocab_ = nullptr;
    std::unique_ptr<MelSpectrogram> mel_converter_;
    bool multilingual_ = false;
    bool on_cuda_ = false;
    int n_mels_ = 80;
    size_t sot_id_ = 0;
    size_t eot_id_ = 0;
    size_t timestamp_begin_ = 0;
    size_t startofprev_id_ = 0;
    size_t transcribe_id_ = 0;
    size_t translate_id_ = 0;

    // One detector per session. VAD and SileroVAD expose the same
    // filter_silence/detect_speech surface but share no base class, so the choice
    // is held as two members and resolved at the call site. silero_vad_ is a
    // cache that can outlive the session that loaded it; use_silero_ says
    // whether the current session uses it. The worker owns them during a session.
    std::unique_ptr<VAD> vad_;
    std::unique_ptr<SileroVAD> silero_vad_;
    std::string silero_loaded_path_;   // guards reuse across sessions
    bool use_silero_ = false;

    // Threading. Lock order: session_mutex_, then push_mutex_ or state_mutex_
    // (never both), each of which may take the audio buffer's own lock.
    std::mutex session_mutex_;          // start / stop
    std::mutex push_mutex_;             // producers: downmix, resampler
    mutable std::mutex state_mutex_;    // segments, queue, stats, errors
    std::mutex wake_mutex_;
    std::condition_variable wake_;
    std::atomic<bool> active_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> session_ended_{false};   // the callback returned false
    std::atomic<bool> worker_running_{false};
    std::atomic<std::thread::id> worker_id_{};
    std::thread worker_;

    // Input
    streaming_detail::AudioBuffer audio_;
    streaming_detail::LinearResampler resampler_;
    std::vector<float> mono_;
    std::vector<float> converted_;

    // Worker-only
    WorkerState ws_;
    struct {
        float avg_logprob = 0.0f;
        float compression_ratio = 0.0f;
        float no_speech_prob = 0.0f;
    } last_metrics_;

    // Output (state_mutex_)
    std::vector<Segment> completed_segments_;
    streaming_detail::CappedQueue<Segment> pending_segments_;
    std::string tail_text_;
    std::string last_error_;
    struct {
        size_t total_samples_processed = 0;
        size_t segments_completed = 0;
        float total_audio_duration_s = 0.0f;
        size_t chunks_failed = 0;
    } stats_;
    std::deque<float> latencies_ms_;
};

// =======================
// StreamingTranscriber Public API
// =======================

StreamingTranscriber::StreamingTranscriber(const std::string& model_path,
                                           const std::string& device,
                                           const std::string& compute_type) {
    ModelOptions opts;
    opts.model_path = model_path;
    if (device == "cuda" || device == "CUDA") {
        opts.device = DeviceType::CUDA;
    } else if (device == "cpu" || device == "CPU") {
        opts.device = DeviceType::CPU;
    } else {
        opts.device = DeviceType::Auto;
    }

    if (compute_type == "float32" || compute_type == "Float32") {
        opts.compute_type = ComputeType::Float32;
    } else if (compute_type == "float16" || compute_type == "Float16") {
        opts.compute_type = ComputeType::Float16;
    } else if (compute_type == "int8" || compute_type == "Int8") {
        opts.compute_type = ComputeType::Int8;
    } else if (compute_type == "int8_float16" || compute_type == "Int8Float16") {
        opts.compute_type = ComputeType::Int8Float16;
    } else {
        opts.compute_type = ComputeType::Auto;
    }

    pimpl_ = std::make_unique<Impl>(opts);
}

StreamingTranscriber::StreamingTranscriber(const ModelOptions& options)
    : pimpl_(std::make_unique<Impl>(options)) {
}

StreamingTranscriber::~StreamingTranscriber() = default;

StreamingTranscriber::StreamingTranscriber(StreamingTranscriber&&) noexcept = default;
StreamingTranscriber& StreamingTranscriber::operator=(StreamingTranscriber&&) noexcept = default;

void StreamingTranscriber::start(const StreamingOptions& options, StreamingCallback callback) {
    pimpl_->start(options, std::move(callback));
}

std::vector<Segment> StreamingTranscriber::stop() {
    return pimpl_->stop();
}

bool StreamingTranscriber::is_active() const {
    return pimpl_->is_active();
}

bool StreamingTranscriber::push_audio(const float* samples, size_t sample_count,
                                      int sample_rate, int channels) {
    return pimpl_->push_audio(samples, sample_count, sample_rate, channels);
}

bool StreamingTranscriber::poll_segment(Segment& out_segment) {
    return pimpl_->poll_segment(out_segment);
}

std::vector<Segment> StreamingTranscriber::get_all_segments() const {
    return pimpl_->get_all_segments();
}

std::string StreamingTranscriber::get_current_text(bool include_partial) const {
    return pimpl_->get_current_text(include_partial);
}

void StreamingTranscriber::reset() {
    pimpl_->reset();
}

void StreamingTranscriber::set_error_callback(StreamingErrorCallback callback) {
    pimpl_->set_error_callback(std::move(callback));
}

std::string StreamingTranscriber::last_error() const {
    return pimpl_->last_error();
}

StreamingTranscriber::Stats StreamingTranscriber::get_stats() const {
    return pimpl_->get_stats();
}

} // namespace muninn
