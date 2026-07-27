#include "muninn/silero_vad.h"
#include <stdexcept>

#ifdef MUNINN_USE_SILERO_VAD

#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <cmath>

#ifdef _WIN32
// windows.h defines min/max macros that break std::min/std::max below.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace muninn {

namespace {

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}
#endif

} // namespace

class SileroVAD::Impl {
public:
    Impl(const SileroVADOptions& options)
        : options_(options)
        , env_(ORT_LOGGING_LEVEL_WARNING, "SileroVAD")
    {
        // Configure session options
        Ort::SessionOptions session_options;
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        bool using_gpu = false;

        // Try GPU first if requested
        if (options.use_gpu) {
            try {
                OrtCUDAProviderOptions cuda_options;
                cuda_options.device_id = options.gpu_device_id;
                cuda_options.arena_extend_strategy = 0;  // kNextPowerOfTwo
                cuda_options.gpu_mem_limit = 0;          // No limit
                cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
                cuda_options.do_copy_in_default_stream = 1;

                session_options.AppendExecutionProvider_CUDA(cuda_options);
                using_gpu = true;
            } catch (const Ort::Exception& e) {
                std::cout << "[SileroVAD] CUDA not available: " << e.what() << "\n";
                std::cout << "[SileroVAD] Falling back to CPU\n";
                using_gpu = false;
            }
        }

        // CPU settings (used as fallback or if GPU disabled)
        if (!using_gpu) {
            session_options.SetIntraOpNumThreads(1);
            session_options.SetInterOpNumThreads(1);
        }

        // Load model
        #ifdef _WIN32
        // The old char-by-char widening mangled any non-ASCII byte, so the
        // model failed to load for anyone whose path contained an accented
        // character. Paths reach us as UTF-8.
        std::wstring model_path_w = utf8_to_wide(options.model_path);
        try {
            session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), session_options);
        } catch (const std::exception& e) {
            if (!using_gpu) throw;
            // The CUDA provider appended cleanly but the session could not be
            // built with it (missing onnxruntime CUDA DLLs, driver too old).
            // A VAD that refuses to load takes the whole transcription down, so
            // retry on CPU rather than propagate.
            std::cout << "[SileroVAD] CUDA session failed (" << e.what() << ") - retrying on CPU\n";
            Ort::SessionOptions cpu_options;
            cpu_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            cpu_options.SetIntraOpNumThreads(1);
            cpu_options.SetInterOpNumThreads(1);
            session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), cpu_options);
            using_gpu = false;
        }
        #else
        session_ = std::make_unique<Ort::Session>(env_, options.model_path.c_str(), session_options);
        #endif

        // Initialize state tensors
        reset_state();

        ready_ = true;
        using_gpu_ = using_gpu;
        std::cout << "[SileroVAD] Model loaded (" << (using_gpu ? "CUDA" : "CPU") << "): "
                  << options.model_path << std::endl;
    }

    bool is_ready() const { return ready_; }

    void reset_state() {
        // Initialize state tensor (Silero VAD uses shape [2, 1, 128] = 256 floats)
        state_.assign(2 * 1 * 128, 0.0f);
        // Initialize context buffer (64 samples of previous audio)
        context_.assign(context_size_, 0.0f);
    }

    // Runs once per 32 ms of audio — about 75,000 times on a 40-minute track —
    // so every buffer, shape and MemoryInfo it needs is allocated once and
    // reused rather than rebuilt per call.
    float predict(const float* chunk, size_t chunk_size) {
        // Augmented input: context (64 samples) + current chunk (512) = 576
        if (input_data_.size() != context_size_ + chunk_size) {
            input_data_.assign(context_size_ + chunk_size, 0.0f);
            input_shape_ = {1, static_cast<int64_t>(input_data_.size())};
        }
        std::copy(context_.begin(), context_.end(), input_data_.begin());
        std::copy(chunk, chunk + chunk_size, input_data_.begin() + context_size_);

        // Carry the last 64 samples into the next window
        std::copy(input_data_.end() - context_size_, input_data_.end(), context_.begin());

        // Tensors are views over our buffers, so they must be rebuilt each call,
        // but the buffers and shapes behind them are stable.
        std::array<Ort::Value, 3> input_tensors = {
            Ort::Value::CreateTensor<float>(
                memory_info_, input_data_.data(), input_data_.size(),
                input_shape_.data(), input_shape_.size()),
            Ort::Value::CreateTensor<float>(
                memory_info_, state_.data(), state_.size(),
                state_shape_.data(), state_shape_.size()),
            Ort::Value::CreateTensor<int64_t>(
                memory_info_, sr_data_.data(), sr_data_.size(),
                sr_shape_.data(), sr_shape_.size()),
        };

        // Input/output names (order: input, state, sr -> output, stateN)
        static const char* input_names[] = {"input", "state", "sr"};
        static const char* output_names[] = {"output", "stateN"};

        auto output_tensors = session_->Run(
            Ort::RunOptions{nullptr},
            input_names, input_tensors.data(), input_tensors.size(),
            output_names, 2);

        float speech_prob = output_tensors[0].GetTensorMutableData<float>()[0];

        // Update state for next iteration
        const float* stateN_data = output_tensors[1].GetTensorMutableData<float>();
        std::copy(stateN_data, stateN_data + state_.size(), state_.begin());

        return speech_prob;
    }

    std::vector<SpeechSegment> detect_speech(
        const std::vector<float>& samples,
        int sample_rate
    ) {
        std::vector<SpeechSegment> segments;

        if (samples.empty()) return segments;

        // Validate sample rate
        if (sample_rate != 8000 && sample_rate != 16000) {
            std::cerr << "[SileroVAD] Warning: Sample rate " << sample_rate
                      << " not supported, use 8000 or 16000\n";
            return segments;
        }

        reset_state();

        int window_size = options_.window_size_samples;
        float min_speech_sec = options_.min_speech_duration_ms / 1000.0f;
        float min_silence_sec = options_.min_silence_duration_ms / 1000.0f;
        float max_speech_sec = static_cast<float>(options_.max_speech_duration_s);

        // State for segment detection
        int speech_start = -1;
        int neg_threshold_count = 0;
        int min_silence_samples = static_cast<int>(min_silence_sec * sample_rate);
        int min_speech_samples = static_cast<int>(min_speech_sec * sample_rate);
        int max_speech_samples = static_cast<int>(max_speech_sec * sample_rate);

        // Hysteresis thresholds (per official Silero VAD implementation)
        // Use higher threshold for speech onset, lower for offset to prevent premature cutoffs
        float onset_threshold = options_.threshold;
        float offset_threshold = std::max(0.15f, options_.threshold - 0.15f);

        // Process audio in chunks
        for (size_t i = 0; i + window_size <= samples.size(); i += window_size) {
            float speech_prob = predict(samples.data() + i, static_cast<size_t>(window_size));

            int current_pos = static_cast<int>(i);

            if (speech_start < 0) {
                // Not currently in speech - check for onset
                if (speech_prob >= onset_threshold) {
                    speech_start = current_pos;
                    neg_threshold_count = 0;
                }
            } else {
                // Currently in speech segment
                if (speech_prob >= offset_threshold) {
                    // Still in speech (using lower offset threshold for hysteresis)
                    neg_threshold_count = 0;

                    // Force split if segment too long
                    if ((current_pos - speech_start) > max_speech_samples) {
                        segments.emplace_back(
                            static_cast<float>(speech_start) / sample_rate,
                            static_cast<float>(current_pos) / sample_rate
                        );
                        speech_start = current_pos;
                    }
                } else {
                    // Below offset threshold - count silence
                    neg_threshold_count += window_size;

                    if (neg_threshold_count >= min_silence_samples) {
                        // End of speech segment
                        int speech_end = current_pos - neg_threshold_count + window_size;
                        int duration = speech_end - speech_start;

                        if (duration >= min_speech_samples) {
                            segments.emplace_back(
                                static_cast<float>(speech_start) / sample_rate,
                                static_cast<float>(speech_end) / sample_rate
                            );
                        }
                        speech_start = -1;
                        neg_threshold_count = 0;
                    }
                }
            }
        }

        // Handle final segment
        if (speech_start >= 0) {
            int speech_end = static_cast<int>(samples.size());
            int duration = speech_end - speech_start;

            if (duration >= min_speech_samples) {
                segments.emplace_back(
                    static_cast<float>(speech_start) / sample_rate,
                    static_cast<float>(speech_end) / sample_rate
                );
            }
        }

        // Add padding to segments
        float pad_sec = options_.speech_pad_ms / 1000.0f;
        float audio_duration = static_cast<float>(samples.size()) / sample_rate;

        for (auto& seg : segments) {
            seg.start = std::max(0.0f, seg.start - pad_sec);
            seg.end = std::min(audio_duration, seg.end + pad_sec);
        }

        // The defaults pad 300 ms each side but only require a 100 ms gap to
        // split, and the max_speech_duration force-split leaves no gap at all —
        // so neighbours routinely end up overlapping. filter_silence() would
        // then copy the shared audio twice and Whisper would transcribe those
        // words twice, which reads as stuttering or looping in the transcript.
        segments = merge_overlapping_segments(std::move(segments));

        // Apply audio preamble: always include first N ms of audio
        // This handles VAD warm-up issues and speech that starts immediately
        float preamble_sec = options_.audio_preamble_ms / 1000.0f;
        if (preamble_sec > 0.0f && audio_duration > 0.0f) {
            float preamble_end = std::min(preamble_sec, audio_duration);

            if (segments.empty()) {
                // No speech detected, but include preamble as a segment
                segments.emplace_back(0.0f, preamble_end);
                std::cout << "[SileroVAD] No speech detected, adding preamble segment 0.0-" << preamble_end << "s\n";
            } else if (segments[0].start > 0.0f) {
                // First segment doesn't start at 0, extend it or insert preamble
                if (segments[0].start <= preamble_end) {
                    // Extend first segment to include preamble
                    segments[0].start = 0.0f;
                    std::cout << "[SileroVAD] Extended first segment to include preamble (now 0.0-" << segments[0].end << "s)\n";
                } else {
                    // Insert preamble as separate segment at the beginning
                    segments.insert(segments.begin(), SpeechSegment(0.0f, preamble_end));
                    std::cout << "[SileroVAD] Inserted preamble segment 0.0-" << preamble_end << "s\n";
                }
            }
        }

        std::cout << "[SileroVAD] Detected " << segments.size() << " speech segments\n";
        return segments;
    }

private:
    SileroVADOptions options_;
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    bool ready_ = false;
    bool using_gpu_ = false;

    // Model state tensor [2, 1, 128]
    std::vector<float> state_;

    // Context buffer for previous audio samples
    static constexpr size_t context_size_ = 64;
    std::vector<float> context_;

    // Reused across every predict() call
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
    std::vector<float> input_data_;
    std::vector<int64_t> input_shape_;
    std::vector<int64_t> state_shape_{2, 1, 128};
    std::vector<int64_t> sr_data_{options_.sample_rate};
    std::vector<int64_t> sr_shape_{1};
};

// SileroVAD public interface implementation

SileroVAD::SileroVAD(const SileroVADOptions& options)
    : pimpl_(std::make_unique<Impl>(options))
{
}

SileroVAD::~SileroVAD() = default;

SileroVAD::SileroVAD(SileroVAD&&) noexcept = default;
SileroVAD& SileroVAD::operator=(SileroVAD&&) noexcept = default;

bool SileroVAD::is_ready() const {
    return pimpl_ && pimpl_->is_ready();
}

void SileroVAD::reset_state() {
    if (pimpl_) {
        pimpl_->reset_state();
    }
}

std::vector<SpeechSegment> SileroVAD::detect_speech(
    const std::vector<float>& samples,
    int sample_rate
) {
    if (!pimpl_) return {};
    return pimpl_->detect_speech(samples, sample_rate);
}

std::vector<float> SileroVAD::filter_silence(
    const std::vector<float>& samples,
    int sample_rate,
    std::vector<SpeechSegment>& segments
) {
    segments = detect_speech(samples, sample_rate);

    if (segments.empty()) {
        // Check if truly silent
        float max_sample = 0.0f;
        for (size_t i = 0; i < samples.size(); i += 100) {
            max_sample = std::max(max_sample, std::abs(samples[i]));
        }

        if (max_sample < 0.001f) {
            std::cout << "[SileroVAD] Track is silent - skipping\n";
            silence_removed_ = static_cast<float>(samples.size()) / sample_rate;
            return {};
        }

        std::cout << "[SileroVAD] No speech detected - returning original audio\n";
        silence_removed_ = 0.0f;
        return samples;
    }

    // Extract speech portions
    std::vector<float> filtered;
    float total_duration = static_cast<float>(samples.size()) / sample_rate;

    for (const auto& seg : segments) {
        int start_sample = std::max(0, static_cast<int>(seg.start * sample_rate));
        int end_sample = std::min(static_cast<int>(samples.size()),
                                  static_cast<int>(seg.end * sample_rate));
        if (end_sample <= start_sample) continue;

        filtered.insert(filtered.end(),
                       samples.begin() + start_sample,
                       samples.begin() + end_sample);
    }

    // Measure what was actually kept. Summing the segment durations instead
    // double-counted overlaps and ignored clamping, which could report a
    // negative amount of silence removed.
    silence_removed_ = total_duration - static_cast<float>(filtered.size()) / sample_rate;

    std::cout << "[SileroVAD] Removed " << silence_removed_ << "s silence ("
              << static_cast<int>(silence_removed_ / std::max(total_duration, 1e-6f) * 100) << "%)\n";

    return filtered;
}

bool is_silero_vad_available() {
    return true;
}

} // namespace muninn

#else  // MUNINN_USE_SILERO_VAD not defined

namespace muninn {

// Stub implementation when ONNX Runtime not available

class SileroVAD::Impl {};

SileroVAD::SileroVAD(const SileroVADOptions&) {
    throw std::runtime_error("SileroVAD not available - compile with MUNINN_USE_SILERO_VAD");
}

SileroVAD::~SileroVAD() = default;
SileroVAD::SileroVAD(SileroVAD&&) noexcept = default;
SileroVAD& SileroVAD::operator=(SileroVAD&&) noexcept = default;

bool SileroVAD::is_ready() const { return false; }
void SileroVAD::reset_state() {}

std::vector<SpeechSegment> SileroVAD::detect_speech(const std::vector<float>&, int) {
    return {};
}

std::vector<float> SileroVAD::filter_silence(
    const std::vector<float>& samples, int, std::vector<SpeechSegment>&
) {
    return samples;
}

bool is_silero_vad_available() {
    return false;
}

} // namespace muninn

#endif  // MUNINN_USE_SILERO_VAD
