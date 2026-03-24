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
#include <cmath>

namespace muninn {

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
        std::wstring model_path_w(options.model_path.begin(), options.model_path.end());
        session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), session_options);
        #else
        session_ = std::make_unique<Ort::Session>(env_, options.model_path.c_str(), session_options);
        #endif

        // Discover input/output names from model
        Ort::AllocatorWithDefaultOptions allocator;

        // Get input info
        size_t num_inputs = session_->GetInputCount();
        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator);
            input_names_.push_back(std::string(name.get()));
            input_names_ptrs_.push_back(input_names_.back().c_str());

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
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            output_names_.push_back(std::string(name.get()));
            output_names_ptrs_.push_back(output_names_.back().c_str());

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

    float predict(const std::vector<float>& chunk) {
        if (!ready_ || input_names_.empty() || output_names_.empty()) {
            return 0.0f;
        }

        // Prepare input tensor
        std::vector<int64_t> input_shape = {1, static_cast<int64_t>(chunk.size())};

        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

        // Create input tensors
        std::vector<Ort::Value> input_tensors;

        // Audio input
        std::vector<float> input_data = chunk;  // Copy for non-const access
        input_tensors.push_back(Ort::Value::CreateTensor<float>(
            memory_info, input_data.data(), input_data.size(),
            input_shape.data(), input_shape.size()));

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
            std::vector<float> chunk(samples.begin() + i, samples.begin() + i + window_size);
            float speech_prob = predict(chunk);

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
    float speech_duration = 0.0f;

    for (const auto& seg : segments) {
        int start_sample = static_cast<int>(seg.start * sample_rate);
        int end_sample = static_cast<int>(seg.end * sample_rate);

        start_sample = std::max(0, start_sample);
        end_sample = std::min(static_cast<int>(samples.size()), end_sample);

        filtered.insert(filtered.end(),
                       samples.begin() + start_sample,
                       samples.begin() + end_sample);

        speech_duration += seg.end - seg.start;
    }

    silence_removed_ = total_duration - speech_duration;

    std::cout << "[TenVAD] Removed " << silence_removed_ << "s silence ("
              << static_cast<int>(silence_removed_ / total_duration * 100) << "%)\n";

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
