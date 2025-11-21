#include "muninn/silero_vad.h"
#include <stdexcept>

#ifdef MUNINN_USE_SILERO_VAD

#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace muninn {

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
        std::wstring model_path_w(options.model_path.begin(), options.model_path.end());
        session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), session_options);
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
        triggered_ = false;
        temp_end_ = 0;
        current_sample_ = 0;
    }

    float predict(const std::vector<float>& chunk) {
        // Build augmented input: context (64 samples) + current chunk (512 samples) = 576 samples
        std::vector<float> input_data;
        input_data.reserve(context_size_ + chunk.size());
        input_data.insert(input_data.end(), context_.begin(), context_.end());
        input_data.insert(input_data.end(), chunk.begin(), chunk.end());

        // Update context with last 64 samples from input for next iteration
        size_t context_start = input_data.size() - context_size_;
        std::copy(input_data.begin() + context_start, input_data.end(), context_.begin());

        // Prepare input tensor
        std::vector<int64_t> input_shape = {1, static_cast<int64_t>(input_data.size())};

        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

        // Create input tensors
        std::vector<Ort::Value> input_tensors;

        // Input audio (context + chunk)
        input_tensors.push_back(Ort::Value::CreateTensor<float>(
            memory_info, input_data.data(), input_data.size(),
            input_shape.data(), input_shape.size()));

        // State tensor [2, 1, 128]
        std::vector<int64_t> state_shape = {2, 1, 128};
        input_tensors.push_back(Ort::Value::CreateTensor<float>(
            memory_info, state_.data(), state_.size(),
            state_shape.data(), state_shape.size()));

        // Sample rate
        std::vector<int64_t> sr_data = {options_.sample_rate};
        std::vector<int64_t> sr_shape = {1};
        input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
            memory_info, sr_data.data(), sr_data.size(),
            sr_shape.data(), sr_shape.size()));

        // Input/output names (order: input, state, sr -> output, stateN)
        const char* input_names[] = {"input", "state", "sr"};
        const char* output_names[] = {"output", "stateN"};

        // Run inference
        auto output_tensors = session_->Run(
            Ort::RunOptions{nullptr},
            input_names, input_tensors.data(), input_tensors.size(),
            output_names, 2);

        // Get speech probability
        float* output_data = output_tensors[0].GetTensorMutableData<float>();
        float speech_prob = output_data[0];

        // Update state for next iteration
        float* stateN_data = output_tensors[1].GetTensorMutableData<float>();
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

    // Detection state
    bool triggered_ = false;
    int temp_end_ = 0;
    int current_sample_ = 0;
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

    std::cout << "[SileroVAD] Removed " << silence_removed_ << "s silence ("
              << static_cast<int>(silence_removed_ / total_duration * 100) << "%)\n";

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
