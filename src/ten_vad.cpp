/**
 * @file ten_vad.cpp
 * @brief TEN VAD implementation using ONNX Runtime
 *
 * TEN VAD is a lightweight neural VAD with:
 * - Small model size (~308KB vs 2.3MB for Silero)
 * - No delay issues (simpler architecture)
 * - Fast inference
 *
 * Model source: https://github.com/TEN-framework/ten-vad
 */

#include "muninn/ten_vad.h"
#include <stdexcept>

#ifdef MUNINN_USE_SILERO_VAD  // Reuse the same ONNX Runtime flag

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

class TenVAD::Impl {
public:
    Impl(const TenVADOptions& options)
        : options_(options)
        , env_(ORT_LOGGING_LEVEL_WARNING, "TenVAD")
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
                std::cout << "[TenVAD] CUDA not available: " << e.what() << "\n";
                std::cout << "[TenVAD] Falling back to CPU\n";
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
        // Paths arrive as UTF-8; the old char-by-char widening corrupted any
        // non-ASCII byte and the model simply failed to load.
        std::wstring model_path_w = utf8_to_wide(options.model_path);
        try {
            session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), session_options);
        } catch (const std::exception& e) {
            if (!using_gpu) throw;
            // Provider appended but the session could not be built with it.
            // Retry on CPU rather than fail the whole transcription.
            std::cout << "[TenVAD] CUDA session failed (" << e.what() << ") - retrying on CPU\n";
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

        // Discover input/output names from model
        Ort::AllocatorWithDefaultOptions allocator;

        // Get input info
        size_t num_inputs = session_->GetInputCount();
        input_names_.reserve(num_inputs);
        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator);
            input_names_.push_back(std::string(name.get()));

            auto shape_info = session_->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
            auto shape = shape_info.GetShape();

            std::cout << "[TenVAD] Input " << i << ": " << input_names_.back();
            std::cout << " shape=[";
            for (size_t j = 0; j < shape.size(); ++j) {
                std::cout << shape[j];
                if (j < shape.size() - 1) std::cout << ",";
            }
            std::cout << "]\n";
        }

        // Get output info
        size_t num_outputs = session_->GetOutputCount();
        output_names_.reserve(num_outputs);
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            output_names_.push_back(std::string(name.get()));

            auto shape_info = session_->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
            auto shape = shape_info.GetShape();

            std::cout << "[TenVAD] Output " << i << ": " << output_names_.back();
            std::cout << " shape=[";
            for (size_t j = 0; j < shape.size(); ++j) {
                std::cout << shape[j];
                if (j < shape.size() - 1) std::cout << ",";
            }
            std::cout << "]\n";
        }

        // Build the char* views only after both name vectors have stopped
        // growing. Taking .c_str() as we pushed handed ONNX Runtime pointers
        // into short strings that moved on the next reallocation — small names
        // like "input" live inside the std::string object itself, so the
        // address changed and Run() read freed memory.
        input_names_ptrs_.reserve(input_names_.size());
        for (const auto& n : input_names_) input_names_ptrs_.push_back(n.c_str());
        output_names_ptrs_.reserve(output_names_.size());
        for (const auto& n : output_names_) output_names_ptrs_.push_back(n.c_str());

        ready_ = true;
        using_gpu_ = using_gpu;
        std::cout << "[TenVAD] Model loaded (" << (using_gpu ? "CUDA" : "CPU") << "): "
                  << options.model_path << std::endl;
    }

    bool is_ready() const { return ready_; }

    void reset_state() {
        // TEN VAD doesn't use persistent state (unlike Silero which has LSTM state)
        // Reset is a no-op, but we keep this for API compatibility
    }

    // Called once per VAD window across the whole track, so the buffers and
    // shapes it needs are allocated once and reused instead of per call.
    float predict(const float* chunk, size_t chunk_size) {
        if (!ready_ || input_names_.empty() || output_names_.empty()) {
            return 0.0f;
        }

        if (input_data_.size() != chunk_size) {
            input_data_.assign(chunk_size, 0.0f);
            input_shape_ = {1, static_cast<int64_t>(chunk_size)};
        }
        std::copy(chunk, chunk + chunk_size, input_data_.begin());

        // The tensor is a view over input_data_, so it is rebuilt each call
        // while the buffer behind it stays put.
        std::array<Ort::Value, 1> input_tensors = {
            Ort::Value::CreateTensor<float>(
                memory_info_, input_data_.data(), input_data_.size(),
                input_shape_.data(), input_shape_.size()),
        };

        // Run inference
        auto output_tensors = session_->Run(
            Ort::RunOptions{nullptr},
            input_names_ptrs_.data(), input_tensors.data(), input_tensors.size(),
            output_names_ptrs_.data(), output_names_ptrs_.size());

        // Get speech probability from first output
        if (!output_tensors.empty()) {
            float* output_data = output_tensors[0].GetTensorMutableData<float>();
            return output_data[0];
        }

        return 0.0f;
    }

    std::vector<SpeechSegment> detect_speech(
        const std::vector<float>& samples,
        int sample_rate
    ) {
        std::vector<SpeechSegment> segments;

        if (samples.empty()) return segments;

        // TEN VAD expects 16kHz
        if (sample_rate != 16000) {
            std::cerr << "[TenVAD] Warning: Sample rate " << sample_rate
                      << " not optimal, use 16000 for best results\n";
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

        // Process audio in chunks
        for (size_t i = 0; i + window_size <= samples.size(); i += window_size) {
            float speech_prob = predict(samples.data() + i, static_cast<size_t>(window_size));

            int current_pos = static_cast<int>(i);

            if (speech_prob >= options_.threshold) {
                // Speech detected
                if (speech_start < 0) {
                    speech_start = current_pos;
                }
                neg_threshold_count = 0;

                // Force split if segment too long
                if (speech_start >= 0 && (current_pos - speech_start) > max_speech_samples) {
                    segments.emplace_back(
                        static_cast<float>(speech_start) / sample_rate,
                        static_cast<float>(current_pos) / sample_rate
                    );
                    speech_start = current_pos;
                }
            } else {
                // No speech
                if (speech_start >= 0) {
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

        // Padding is applied outwards after the boundaries were chosen, so
        // neighbours can end up overlapping — and filter_silence() would then
        // hand Whisper the shared audio twice, producing repeated words.
        segments = merge_overlapping_segments(std::move(segments));

        // Apply audio preamble: always include first N ms of audio
        // This handles VAD warm-up issues and speech that starts immediately
        float preamble_sec = options_.audio_preamble_ms / 1000.0f;
        if (preamble_sec > 0.0f && audio_duration > 0.0f) {
            float preamble_end = std::min(preamble_sec, audio_duration);

            if (segments.empty()) {
                // No speech detected, but include preamble as a segment
                segments.emplace_back(0.0f, preamble_end);
                std::cout << "[TenVAD] No speech detected, adding preamble segment 0.0-" << preamble_end << "s\n";
            } else if (segments[0].start > 0.0f) {
                // First segment doesn't start at 0, extend it or insert preamble
                if (segments[0].start <= preamble_end) {
                    // Extend first segment to include preamble
                    segments[0].start = 0.0f;
                    std::cout << "[TenVAD] Extended first segment to include preamble (now 0.0-" << segments[0].end << "s)\n";
                } else {
                    // Insert preamble as separate segment at the beginning
                    segments.insert(segments.begin(), SpeechSegment(0.0f, preamble_end));
                    std::cout << "[TenVAD] Inserted preamble segment 0.0-" << preamble_end << "s\n";
                }
            }
        }

        std::cout << "[TenVAD] Detected " << segments.size() << " speech segments\n";
        return segments;
    }

private:
    TenVADOptions options_;
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    bool ready_ = false;
    bool using_gpu_ = false;

    // Discovered input/output names
    std::vector<std::string> input_names_;
    std::vector<const char*> input_names_ptrs_;
    std::vector<std::string> output_names_;
    std::vector<const char*> output_names_ptrs_;

    // Reused across every predict() call
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
    std::vector<float> input_data_;
    std::vector<int64_t> input_shape_;
};

// TenVAD public interface implementation

TenVAD::TenVAD(const TenVADOptions& options)
    : pimpl_(std::make_unique<Impl>(options))
{
}

TenVAD::~TenVAD() = default;

TenVAD::TenVAD(TenVAD&&) noexcept = default;
TenVAD& TenVAD::operator=(TenVAD&&) noexcept = default;

bool TenVAD::is_ready() const {
    return pimpl_ && pimpl_->is_ready();
}

void TenVAD::reset_state() {
    if (pimpl_) {
        pimpl_->reset_state();
    }
}

std::vector<SpeechSegment> TenVAD::detect_speech(
    const std::vector<float>& samples,
    int sample_rate
) {
    if (!pimpl_) return {};
    return pimpl_->detect_speech(samples, sample_rate);
}

std::vector<float> TenVAD::filter_silence(
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
            std::cout << "[TenVAD] Track is silent - skipping\n";
            silence_removed_ = static_cast<float>(samples.size()) / sample_rate;
            return {};
        }

        std::cout << "[TenVAD] No speech detected - returning original audio\n";
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

    // Measure what was actually kept rather than summing segment durations,
    // which ignored clamping and could report negative silence.
    silence_removed_ = total_duration - static_cast<float>(filtered.size()) / sample_rate;

    std::cout << "[TenVAD] Removed " << silence_removed_ << "s silence ("
              << static_cast<int>(silence_removed_ / std::max(total_duration, 1e-6f) * 100) << "%)\n";

    return filtered;
}

bool is_ten_vad_available() {
    return true;
}

} // namespace muninn

#else  // MUNINN_USE_SILERO_VAD not defined

namespace muninn {

// Stub implementation when ONNX Runtime not available

class TenVAD::Impl {};

TenVAD::TenVAD(const TenVADOptions&) {
    throw std::runtime_error("TenVAD not available - compile with MUNINN_USE_SILERO_VAD");
}

TenVAD::~TenVAD() = default;
TenVAD::TenVAD(TenVAD&&) noexcept = default;
TenVAD& TenVAD::operator=(TenVAD&&) noexcept = default;

bool TenVAD::is_ready() const { return false; }
void TenVAD::reset_state() {}

std::vector<SpeechSegment> TenVAD::detect_speech(const std::vector<float>&, int) {
    return {};
}

std::vector<float> TenVAD::filter_silence(
    const std::vector<float>& samples, int, std::vector<SpeechSegment>&
) {
    return samples;
}

bool is_ten_vad_available() {
    return false;
}

} // namespace muninn

#endif  // MUNINN_USE_SILERO_VAD
