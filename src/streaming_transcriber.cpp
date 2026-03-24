/**
 * @file streaming_transcriber.cpp
 * @brief Real-time streaming transcription using CTranslate2 Whisper
 *
 * Provides low-latency transcription by processing audio in small chunks
 * with overlapping windows for context continuity.
 */

#include "muninn/streaming_transcriber.h"
#include "muninn/mel_spectrogram.h"
#include "muninn/vad.h"
#include <ctranslate2/models/whisper.h>
#include <ctranslate2/storage_view.h>

#include <thread>
#include <mutex>
#include <deque>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace muninn {

namespace {

// GPT-2 BPE space marker (U+0120)
const std::string GPT2_SPACE = "\xC4\xA0";

// Clean token by replacing BPE markers with spaces
std::string clean_token(const std::string& token) {
    std::string processed = token;
    size_t pos = 0;
    while ((pos = processed.find(GPT2_SPACE, pos)) != std::string::npos) {
        processed.replace(pos, 2, " ");
        pos += 1;
    }
    return processed;
}

// Check if token is a special token (<|...|>)
bool is_special_token(const std::string& token) {
    return token.size() >= 4 &&
           token.substr(0, 2) == "<|" &&
           token.substr(token.size() - 2) == "|>";
}

// Extract text from tokens, filtering special tokens
std::string extract_text(const std::vector<std::string>& tokens) {
    std::string text;
    for (const auto& token : tokens) {
        if (!is_special_token(token)) {
            text += clean_token(token);
        }
    }
    // Trim whitespace
    size_t start = text.find_first_not_of(" \t\n\r");
    size_t end = text.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return text.substr(start, end - start + 1);
}

} // anonymous namespace

// =======================
// StreamingTranscriber::Impl
// =======================

class StreamingTranscriber::Impl {
public:
    Impl(const ModelOptions& options)
        : model_opts_(options)
        , mel_converter_(16000, 400, 128, 160)
        , active_(false)
        , stop_requested_(false)
    {
        // Convert device type
        ctranslate2::Device ct_device = ctranslate2::Device::CPU;
        if (options.device == DeviceType::CUDA || options.device == DeviceType::Auto) {
            ct_device = ctranslate2::Device::CUDA;
        }

        // Load Whisper model
        model_ = std::make_unique<ctranslate2::models::Whisper>(
            options.model_path,
            ct_device
        );

        std::cout << "[StreamingTranscriber] Model loaded: " << options.model_path << "\n";
        std::cout << "[StreamingTranscriber] Multilingual: " << (model_->is_multilingual() ? "yes" : "no") << "\n";
    }

    ~Impl() {
        stop();
    }

    void start(const StreamingOptions& options, StreamingCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (active_) {
            throw std::runtime_error("Streaming session already active");
        }

        options_ = options;
        callback_ = callback;
        active_ = true;
        stop_requested_ = false;

        // Initialize VAD if enabled
        if (options_.enable_vad) {
            VADOptions vad_opts;
            vad_opts.threshold = 0.02f;
            vad_opts.min_speech_duration_ms = 200;
            vad_opts.min_silence_duration_ms = 300;
            vad_opts.speech_pad_ms = 50;
            vad_ = std::make_unique<VAD>(vad_opts);
        }

        // Clear state
        audio_buffer_.clear();
        completed_segments_.clear();
        pending_segments_.clear();
        previous_tokens_.clear();
        stats_ = {};

        // Start processing thread
        worker_ = std::thread(&Impl::process_loop, this);

        std::cout << "[StreamingTranscriber] Started with chunk_length=" << options_.chunk_length_s
                  << "s, overlap=" << options_.overlap_s << "s\n";
    }

    std::vector<Segment> stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_) {
                return {};
            }
            stop_requested_ = true;
        }

        if (worker_.joinable()) {
            worker_.join();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;

        // Process any remaining audio
        if (!audio_buffer_.empty()) {
            process_buffered_audio(true);
        }

        return completed_segments_;
    }

    bool is_active() const {
        return active_;
    }

    void push_audio(const float* samples, size_t sample_count, int sample_rate, int channels) {
        if (!active_ || sample_count == 0) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // Convert to mono if stereo
        std::vector<float> mono_samples;
        if (channels == 2) {
            mono_samples.resize(sample_count / 2);
            for (size_t i = 0; i < mono_samples.size(); ++i) {
                mono_samples[i] = (samples[i * 2] + samples[i * 2 + 1]) * 0.5f;
            }
        } else {
            mono_samples.assign(samples, samples + sample_count);
        }

        // Resample to 16kHz if needed
        if (sample_rate != 16000) {
            std::vector<float> resampled;
            float ratio = 16000.0f / sample_rate;
            resampled.reserve(static_cast<size_t>(mono_samples.size() * ratio + 1));

            for (float i = 0; i < mono_samples.size(); i += 1.0f / ratio) {
                size_t idx = static_cast<size_t>(i);
                if (idx < mono_samples.size()) {
                    resampled.push_back(mono_samples[idx]);
                }
            }
            mono_samples = std::move(resampled);
        }

        // Add to buffer
        audio_buffer_.insert(audio_buffer_.end(), mono_samples.begin(), mono_samples.end());
        stats_.total_samples_processed += mono_samples.size();
        stats_.total_audio_duration_s = stats_.total_samples_processed / 16000.0f;
    }

    bool poll_segment(Segment& out_segment) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_segments_.empty()) {
            return false;
        }
        out_segment = pending_segments_.front();
        pending_segments_.pop_front();
        return true;
    }

    std::vector<Segment> get_all_segments() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_segments_;
    }

    std::string get_current_text(bool include_partial) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string text;
        for (const auto& seg : completed_segments_) {
            if (!text.empty()) text += " ";
            text += seg.text;
        }
        return text;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_buffer_.clear();
        completed_segments_.clear();
        pending_segments_.clear();
        previous_tokens_.clear();
        stats_ = {};
    }

    StreamingTranscriber::Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        StreamingTranscriber::Stats s;
        s.total_samples_processed = stats_.total_samples_processed;
        s.segments_completed = stats_.segments_completed;
        s.total_audio_duration_s = stats_.total_audio_duration_s;

        // Calculate average latency
        if (!latencies_ms_.empty()) {
            float sum = 0;
            for (float lat : latencies_ms_) sum += lat;
            s.average_latency_ms = sum / latencies_ms_.size();
        } else {
            s.average_latency_ms = 0;
        }

        // Buffer fill ratio
        size_t chunk_samples = static_cast<size_t>(options_.chunk_length_s * 16000);
        s.buffer_fill_ratio = chunk_samples > 0 ?
            static_cast<float>(audio_buffer_.size()) / chunk_samples : 0.0f;

        return s;
    }

private:
    void process_loop() {
        while (!stop_requested_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                process_buffered_audio(false);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void process_buffered_audio(bool flush) {
        constexpr int SAMPLE_RATE = 16000;
        size_t chunk_samples = static_cast<size_t>(options_.chunk_length_s * SAMPLE_RATE);
        size_t overlap_samples = static_cast<size_t>(options_.overlap_s * SAMPLE_RATE);

        // Prevent underflow: overlap cannot exceed chunk size
        if (overlap_samples > chunk_samples) {
            overlap_samples = chunk_samples / 2;  // Default to 50% overlap if misconfigured
        }

        size_t min_samples = flush ? (SAMPLE_RATE / 4) : chunk_samples;  // 250ms minimum when flushing

        while (audio_buffer_.size() >= min_samples) {
            auto start_time = std::chrono::high_resolution_clock::now();

            // Extract chunk
            size_t process_count = std::min(audio_buffer_.size(), chunk_samples);
            std::vector<float> chunk(audio_buffer_.begin(), audio_buffer_.begin() + process_count);

            // Apply VAD if enabled
            std::vector<float> processed_audio;
            if (options_.enable_vad && vad_) {
                std::vector<SpeechSegment> speech_segs;
                processed_audio = vad_->filter_silence(chunk, SAMPLE_RATE, speech_segs);
                if (processed_audio.empty()) {
                    // No speech detected - remove audio and continue
                    size_t remove_count = process_count - overlap_samples;
                    if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                        audio_buffer_.erase(audio_buffer_.begin(),
                                          audio_buffer_.begin() + remove_count);
                    }
                    continue;
                }
            } else {
                processed_audio = chunk;
            }

            // Compute mel spectrogram
            std::vector<std::vector<float>> mel_features;
            int n_frames = mel_converter_.compute(processed_audio, mel_features);

            if (n_frames < 10) {  // Too short
                size_t remove_count = process_count - overlap_samples;
                if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                    audio_buffer_.erase(audio_buffer_.begin(),
                                      audio_buffer_.begin() + remove_count);
                }
                continue;
            }

            // Prepare features for CTranslate2 (transpose: n_frames x n_mels -> n_mels x n_frames)
            int n_mels = mel_features[0].size();
            std::vector<float> flat_features;
            flat_features.reserve(n_frames * n_mels);

            for (int mel = 0; mel < n_mels; mel++) {
                for (int frame = 0; frame < n_frames; frame++) {
                    flat_features.push_back(mel_features[frame][mel]);
                }
            }

            ctranslate2::StorageView features(
                ctranslate2::Shape{1, static_cast<ctranslate2::dim_t>(n_mels),
                                   static_cast<ctranslate2::dim_t>(n_frames)},
                flat_features
            );

            // Build prompt
            std::vector<std::vector<std::string>> prompts;
            if (model_->is_multilingual()) {
                // Multilingual model: <|startoftranscript|><|lang|><|transcribe|>
                std::string lang_token = "<|" + (options_.language == "auto" ? "en" : options_.language) + "|>";
                std::string task_token = options_.task == "translate" ? "<|translate|>" : "<|transcribe|>";
                prompts = {{"<|startoftranscript|>", lang_token, task_token}};
            } else {
                // English-only model
                prompts = {{"<|startoftranscript|>", "<|en|>", "<|transcribe|>"}};
            }

            // Set up Whisper options
            ctranslate2::models::WhisperOptions whisper_options;
            whisper_options.beam_size = options_.beam_size;
            whisper_options.patience = 1.0f;
            whisper_options.length_penalty = 1.0f;
            whisper_options.repetition_penalty = 1.0f;
            whisper_options.no_repeat_ngram_size = 0;
            whisper_options.max_length = 448;
            whisper_options.sampling_topk = (options_.temperature > 0) ? 0 : 1;
            whisper_options.sampling_temperature = options_.temperature;
            whisper_options.num_hypotheses = 1;
            whisper_options.return_scores = true;
            whisper_options.return_no_speech_prob = true;
            whisper_options.max_initial_timestamp_index = 50;
            whisper_options.suppress_blank = true;
            whisper_options.suppress_tokens = {-1};

            // Run inference
            auto future_results = model_->generate(features, prompts, whisper_options);

            if (future_results.empty()) {
                size_t remove_count = process_count - overlap_samples;
                if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                    audio_buffer_.erase(audio_buffer_.begin(),
                                      audio_buffer_.begin() + remove_count);
                }
                continue;
            }

            // Get result
            auto result = future_results[0].get();

            // Check for no speech
            if (result.no_speech_prob > options_.no_speech_threshold) {
                size_t remove_count = process_count - overlap_samples;
                if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                    audio_buffer_.erase(audio_buffer_.begin(),
                                      audio_buffer_.begin() + remove_count);
                }
                continue;
            }

            // Extract text
            if (result.sequences.empty() || result.sequences[0].empty()) {
                size_t remove_count = process_count - overlap_samples;
                if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                    audio_buffer_.erase(audio_buffer_.begin(),
                                      audio_buffer_.begin() + remove_count);
                }
                continue;
            }

            std::string text = extract_text(result.sequences[0]);

            // Skip empty or too short
            if (text.length() < 2) {
                size_t remove_count = process_count - overlap_samples;
                if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                    audio_buffer_.erase(audio_buffer_.begin(),
                                      audio_buffer_.begin() + remove_count);
                }
                continue;
            }

            // Calculate metrics
            float avg_logprob = 0.0f;
            if (result.has_scores() && !result.scores.empty() && !result.sequences_ids.empty()) {
                size_t seq_len = result.sequences_ids[0].size();
                avg_logprob = result.scores[0] / static_cast<float>(seq_len + 1);
            }

            size_t num_tokens = result.sequences_ids.empty() ? 0 : result.sequences_ids[0].size();
            float compression_ratio = static_cast<float>(num_tokens) /
                                     std::max(1.0f, static_cast<float>(text.length()));

            // Check for hallucination
            if (options_.filter_hallucinations) {
                if (compression_ratio > options_.compression_ratio_threshold || avg_logprob < -1.0f) {
                    size_t remove_count = process_count - overlap_samples;
                    if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                        audio_buffer_.erase(audio_buffer_.begin(),
                                          audio_buffer_.begin() + remove_count);
                    }
                    continue;
                }
            }

            // Calculate timing
            float chunk_duration = chunk.size() / 16000.0f;
            float seg_start = stats_.total_audio_duration_s - chunk_duration;
            float seg_end = stats_.total_audio_duration_s;

            // Create segment
            Segment seg;
            seg.id = static_cast<int>(completed_segments_.size());
            seg.track_id = 0;
            seg.start = std::max(0.0f, seg_start);
            seg.end = seg_end;
            seg.text = text;
            seg.no_speech_prob = result.no_speech_prob;
            seg.avg_logprob = avg_logprob;
            seg.compression_ratio = compression_ratio;
            seg.temperature = options_.temperature;
            seg.language = options_.language == "auto" ? "en" : options_.language;
            seg.language_probability = 1.0f;

            // Store segment
            completed_segments_.push_back(seg);
            pending_segments_.push_back(seg);
            stats_.segments_completed++;

            // Track latency
            auto end_time = std::chrono::high_resolution_clock::now();
            float latency_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
            latencies_ms_.push_back(latency_ms);
            if (latencies_ms_.size() > 100) {
                latencies_ms_.erase(latencies_ms_.begin());
            }

            // Call callback
            if (callback_) {
                bool should_continue = callback_(seg);
                if (!should_continue) {
                    stop_requested_ = true;
                    break;
                }
            }

            // Remove processed audio (keep overlap for context)
            size_t remove_count = process_count - overlap_samples;
            if (remove_count > 0 && remove_count <= audio_buffer_.size()) {
                audio_buffer_.erase(audio_buffer_.begin(),
                                  audio_buffer_.begin() + remove_count);
            }

            // Save tokens for context
            if (!result.sequences_ids.empty()) {
                previous_tokens_ = result.sequences_ids[0];
            }
        }
    }

    // Configuration
    ModelOptions model_opts_;
    StreamingOptions options_;
    StreamingCallback callback_;

    // Model
    std::unique_ptr<ctranslate2::models::Whisper> model_;
    MelSpectrogram mel_converter_;
    std::unique_ptr<VAD> vad_;

    // Threading
    mutable std::mutex mutex_;
    std::atomic<bool> active_;
    std::atomic<bool> stop_requested_;
    std::thread worker_;

    // State
    std::vector<float> audio_buffer_;
    std::vector<Segment> completed_segments_;
    std::deque<Segment> pending_segments_;
    std::vector<size_t> previous_tokens_;

    // Statistics
    struct {
        size_t total_samples_processed = 0;
        size_t segments_completed = 0;
        float total_audio_duration_s = 0.0f;
    } stats_;

    std::vector<float> latencies_ms_;
};

// =======================
// StreamingTranscriber Public API
// =======================

StreamingTranscriber::StreamingTranscriber(const std::string& model_path,
                                           const std::string& device,
                                           const std::string& compute_type) {
    ModelOptions opts;
    opts.model_path = model_path;
    opts.device = (device == "cuda" || device == "auto") ? DeviceType::CUDA : DeviceType::CPU;

    // Convert compute_type string to enum
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
    pimpl_->start(options, callback);
}

std::vector<Segment> StreamingTranscriber::stop() {
    return pimpl_->stop();
}

bool StreamingTranscriber::is_active() const {
    return pimpl_->is_active();
}

void StreamingTranscriber::push_audio(const float* samples, size_t sample_count,
                                      int sample_rate, int channels) {
    pimpl_->push_audio(samples, sample_count, sample_rate, channels);
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

StreamingTranscriber::Stats StreamingTranscriber::get_stats() const {
    return pimpl_->get_stats();
}

} // namespace muninn
