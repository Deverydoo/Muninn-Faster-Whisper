#include "muninn/diarization.h"
#include "muninn/logging.h"
#include "speaker_clustering.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <stdexcept>

#ifdef MUNINN_USE_SILERO_VAD
#include <onnxruntime_cxx_api.h>
#endif

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
// Paths arrive as UTF-8; ONNX Runtime wants a wide path on Windows.
std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}
#endif

std::string lower_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

// Speakers from cluster labels: one per label, most talkative first, ids and
// default labels by that order. final_id[label] gives each label's new id.
std::vector<Speaker> build_speakers(const std::vector<SpeakerEmbedding>& embeddings,
                                    const std::vector<int>& labels,
                                    std::vector<int>& final_id) {
    std::vector<Speaker> speakers;
    if (labels.empty()) {
        final_id.clear();
        return speakers;
    }
    const int count = *std::max_element(labels.begin(), labels.end()) + 1;
    speakers.resize(static_cast<size_t>(count));
    for (int id = 0; id < count; ++id) speakers[static_cast<size_t>(id)].speaker_id = id;

    for (size_t i = 0; i < embeddings.size(); ++i) {
        Speaker& s = speakers[static_cast<size_t>(labels[i])];
        s.embeddings.push_back(embeddings[i]);
        s.total_duration += embeddings[i].end - embeddings[i].start;
    }

    // Most active first; equal durations keep the order of first appearance.
    std::stable_sort(speakers.begin(), speakers.end(),
                     [](const Speaker& a, const Speaker& b) {
                         return a.total_duration > b.total_duration;
                     });

    final_id.assign(static_cast<size_t>(count), -1);
    for (size_t i = 0; i < speakers.size(); ++i) {
        final_id[static_cast<size_t>(speakers[i].speaker_id)] = static_cast<int>(i);
        speakers[i].speaker_id = static_cast<int>(i);
        speakers[i].label = "Speaker " + std::to_string(i);
    }
    return speakers;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// ONNX Runtime session (kept out of the public header)
// ═══════════════════════════════════════════════════════════

class Diarizer::Impl {
public:
#ifdef MUNINN_USE_SILERO_VAD
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::SessionOptions> session_options;
    std::unique_ptr<Ort::Session> session;

    std::vector<int64_t> input_shape;   // from the model; <= 0 means "any"
    std::string input_name;             // owned, so nothing dangles
    std::string output_name;
    int64_t fixed_input_samples = -1;   // > 0 when the model takes exactly this many samples
#endif

    // Loads the model. May change options.device to "cpu" when CUDA isn't there.
    void initialize(DiarizationOptions& options) {
#ifdef MUNINN_USE_SILERO_VAD
        try {
            env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MuninnDiarizer");

            session_options = std::make_unique<Ort::SessionOptions>();
            session_options->SetIntraOpNumThreads(std::max(0, options.num_threads));
            session_options->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // CPU unless CUDA was asked for and this ONNX Runtime has it.
            const std::string requested = lower_ascii(options.device);
            std::string actual = "cpu";
            if (requested == "cuda" || requested == "auto") {
                const auto providers = Ort::GetAvailableProviders();
                const bool has_cuda = std::find(providers.begin(), providers.end(),
                                                "CUDAExecutionProvider") != providers.end();
                if (has_cuda) {
                    try {
                        OrtCUDAProviderOptions cuda_options;
                        cuda_options.device_id = 0;
                        session_options->AppendExecutionProvider_CUDA(cuda_options);
                        actual = "cuda";
                    } catch (const Ort::Exception& e) {
                        Logger::warn(std::string("Diarizer: CUDA provider failed, using CPU: ") + e.what());
                    }
                } else if (requested == "cuda") {
                    Logger::warn("Diarizer: this ONNX Runtime has no CUDA provider, using CPU");
                }
            }
            options.device = actual;

#ifdef _WIN32
            const std::wstring model_path_wide = utf8_to_wide(options.embedding_model_path);
            session = std::make_unique<Ort::Session>(*env, model_path_wide.c_str(), *session_options);
#else
            session = std::make_unique<Ort::Session>(*env, options.embedding_model_path.c_str(),
                                                     *session_options);
#endif

            if (session->GetInputCount() < 1 || session->GetOutputCount() < 1) {
                throw std::runtime_error("embedding model has no input or no output");
            }

            Ort::AllocatorWithDefaultOptions allocator;
            input_name = session->GetInputNameAllocated(0, allocator).get();
            output_name = session->GetOutputNameAllocated(0, allocator).get();
            input_shape = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            if (!input_shape.empty() && input_shape.back() > 0) {
                fixed_input_samples = input_shape.back();
            }

            const int64_t window_samples =
                static_cast<int64_t>(std::lround(options.embedding_window_s * 16000.0f));
            if (fixed_input_samples > 0 && fixed_input_samples != window_samples) {
                Logger::warn("Diarizer: the model takes exactly " + std::to_string(fixed_input_samples) +
                             " samples; embedding_window_s of " + std::to_string(options.embedding_window_s) +
                             " s is padded or cut to that");
            }

            Logger::debug("Diarizer: loaded " + options.embedding_model_path + " (input '" + input_name +
                          "', " + (fixed_input_samples > 0 ? std::to_string(fixed_input_samples) + " samples"
                                                            : std::string("any length")) +
                          ", device " + actual + ")");

        } catch (const Ort::Exception& e) {
            throw std::runtime_error("Failed to initialize ONNX session: " + std::string(e.what()));
        }
#else
        (void)options;
        throw std::runtime_error("This Muninn build has no ONNX Runtime, so it can't diarize");
#endif
    }

    bool ready() const {
#ifdef MUNINN_USE_SILERO_VAD
        return session != nullptr;
#else
        return false;
#endif
    }

    // One embedding for a stretch of audio.
    std::vector<float> run(const float* audio_data, size_t num_samples) {
#ifdef MUNINN_USE_SILERO_VAD
        if (!session) throw std::runtime_error("Diarizer not initialized");
        try {
            Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
                OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

            // A fixed-length model gets exactly its length (padded or cut);
            // otherwise the audio goes in as it is.
            const size_t samples = fixed_input_samples > 0 ? static_cast<size_t>(fixed_input_samples)
                                                           : std::max<size_t>(num_samples, 1);
            std::vector<float> input_data(samples, 0.0f);
            if (audio_data && num_samples > 0) {
                std::copy(audio_data, audio_data + std::min(num_samples, samples), input_data.begin());
            }

            // The model's shape with every open dimension filled in:
            // batch 1, and the sample count last.
            std::vector<int64_t> shape = input_shape;
            if (shape.empty()) shape = {1, 0};
            for (auto& d : shape) {
                if (d <= 0) d = 1;
            }
            shape.back() = static_cast<int64_t>(samples);

            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, input_data.data(), input_data.size(), shape.data(), shape.size());

            const char* in_name = input_name.c_str();
            const char* out_name = output_name.c_str();
            auto outputs = session->Run(Ort::RunOptions{nullptr}, &in_name, &input_tensor, 1, &out_name, 1);

            // [1, embedding_dim] for pyannote
            const auto info = outputs[0].GetTensorTypeAndShapeInfo();
            const auto out_shape = info.GetShape();
            if (out_shape.empty() || out_shape.back() <= 0) {
                throw std::runtime_error("embedding model returned an empty output");
            }
            const size_t dim = static_cast<size_t>(out_shape.back());
            if (info.GetElementCount() < dim) {
                throw std::runtime_error("embedding model output is smaller than its shape says");
            }
            const float* out = outputs[0].GetTensorData<float>();
            return std::vector<float>(out, out + dim);

        } catch (const Ort::Exception& e) {
            throw std::runtime_error("ONNX embedding extraction failed: " + std::string(e.what()));
        }
#else
        (void)audio_data;
        (void)num_samples;
        throw std::runtime_error("This Muninn build has no ONNX Runtime, so it can't diarize");
#endif
    }
};

// ═══════════════════════════════════════════════════════════
// Diarizer Implementation
// ═══════════════════════════════════════════════════════════

Diarizer::Diarizer(const std::string& embedding_model_path,
                   const DiarizationOptions& options)
    : options_(options), impl_(std::make_unique<Impl>()) {

    if (embedding_model_path.empty()) {
        throw std::runtime_error("Embedding model path cannot be empty");
    }
    if (!(options_.embedding_window_s > 0.0f) || !(options_.embedding_step_s > 0.0f)) {
        throw std::invalid_argument("embedding_window_s and embedding_step_s must be greater than 0");
    }

    options_.embedding_model_path = embedding_model_path;
    impl_->initialize(options_);
}

Diarizer::~Diarizer() = default;
Diarizer::Diarizer(Diarizer&&) noexcept = default;
Diarizer& Diarizer::operator=(Diarizer&&) noexcept = default;

bool Diarizer::is_ready() const {
    return impl_ && impl_->ready();
}

// ═══════════════════════════════════════════════════════════
// Core Diarization
// ═══════════════════════════════════════════════════════════

DiarizationResult Diarizer::diarize(const float* audio_data,
                                    size_t num_samples,
                                    int sample_rate) {
    if (sample_rate != 16000) {
        throw std::runtime_error("Diarization requires 16kHz audio (got " +
                               std::to_string(sample_rate) + "Hz)");
    }

    if (!is_ready()) {
        throw std::runtime_error("Diarizer not initialized");
    }

    DiarizationResult result;

    // Step 1: Extract embeddings with sliding window
    auto embeddings = extract_embeddings(audio_data, num_samples, sample_rate);

    if (embeddings.empty()) {
        Logger::debug("Diarizer: no embeddings extracted (audio shorter than one window?)");
        return result;
    }

    Logger::debug("Diarizer: extracted " + std::to_string(embeddings.size()) + " embeddings");

    // Step 2: Cluster embeddings into speakers
    const std::vector<int> labels = detail::cluster_embeddings(
        embeddings, options_.clustering_threshold, options_.max_speakers);
    std::vector<int> final_id;
    auto speakers = build_speakers(embeddings, labels, final_id);

    Logger::debug("Diarizer: detected " + std::to_string(speakers.size()) + " speakers");

    // Step 3: Time-aligned segments, one per embedding
    std::vector<DiarizationSegment> segments;
    segments.reserve(embeddings.size());
    for (size_t i = 0; i < embeddings.size(); ++i) {
        DiarizationSegment seg;
        seg.start = embeddings[i].start;
        seg.end = embeddings[i].end;
        seg.speaker_id = final_id[static_cast<size_t>(labels[i])];
        seg.speaker_label = "Speaker " + std::to_string(seg.speaker_id);
        seg.confidence = 1.0f;
        segments.push_back(std::move(seg));
    }

    // Step 4: Merge adjacent segments from same speaker (optional)
    if (options_.merge_adjacent_same_speaker && !segments.empty()) {
        std::vector<DiarizationSegment> merged;
        merged.push_back(segments[0]);

        for (size_t i = 1; i < segments.size(); ++i) {
            auto& last = merged.back();
            auto& curr = segments[i];

            // Merge if same speaker and close in time (< 0.5s gap)
            if (curr.speaker_id == last.speaker_id && (curr.start - last.end) < 0.5f) {
                last.end = curr.end;
                last.confidence = (last.confidence + curr.confidence) / 2.0f;
            } else {
                merged.push_back(curr);
            }
        }

        segments = std::move(merged);
    }

    result.segments = std::move(segments);
    result.num_speakers = static_cast<int>(speakers.size());
    result.speakers = std::move(speakers);

    return result;
}

int Diarizer::get_speaker_at_time(const DiarizationResult& result, float time_s) {
    for (const auto& seg : result.segments) {
        if (time_s >= seg.start && time_s < seg.end) {
            return seg.speaker_id;
        }
    }
    return -1;  // No speaker at this time
}

void Diarizer::assign_speakers_to_segments(std::vector<Segment>& segments,
                                          const DiarizationResult& diarization) {
    for (auto& seg : segments) {
        // Get speaker at segment midpoint
        float midpoint = (seg.start + seg.end) / 2.0f;
        int speaker_id = get_speaker_at_time(diarization, midpoint);

        seg.speaker_id = speaker_id;

        if (speaker_id >= 0) {
            // Find speaker label
            for (const auto& speaker : diarization.speakers) {
                if (speaker.speaker_id == speaker_id) {
                    seg.speaker_label = speaker.label;
                    break;
                }
            }

            // If no custom label, use default
            if (seg.speaker_label.empty()) {
                seg.speaker_label = "Speaker " + std::to_string(speaker_id);
            }

            // Find confidence
            for (const auto& diar_seg : diarization.segments) {
                if (midpoint >= diar_seg.start && midpoint < diar_seg.end) {
                    seg.speaker_confidence = diar_seg.confidence;
                    break;
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Embedding Extraction
// ═══════════════════════════════════════════════════════════

SpeakerEmbedding Diarizer::extract_embedding(const float* audio_data, size_t num_samples) {
    SpeakerEmbedding embedding;
    embedding.features = impl_->run(audio_data, num_samples);
    return embedding;
}

std::vector<SpeakerEmbedding> Diarizer::extract_embeddings(const float* audio_data,
                                                           size_t num_samples,
                                                           int sample_rate) {
    std::vector<SpeakerEmbedding> embeddings;
    if (!audio_data || sample_rate <= 0) return embeddings;

    const size_t window_samples = static_cast<size_t>(
        std::max(1L, std::lround(options_.embedding_window_s * sample_rate)));
    const size_t step_samples = static_cast<size_t>(
        std::max(1L, std::lround(options_.embedding_step_s * sample_rate)));

    size_t pos = 0;
    while (pos + window_samples <= num_samples) {
        SpeakerEmbedding emb = extract_embedding(audio_data + pos, window_samples);

        emb.start = static_cast<float>(pos) / sample_rate;
        emb.end = static_cast<float>(pos + window_samples) / sample_rate;

        embeddings.push_back(std::move(emb));

        pos += step_samples;
    }

    return embeddings;
}

// ═══════════════════════════════════════════════════════════
// Speaker Clustering
// ═══════════════════════════════════════════════════════════

float Diarizer::cosine_similarity(const SpeakerEmbedding& emb1,
                                 const SpeakerEmbedding& emb2) {
    if (emb1.features.size() != emb2.features.size()) {
        throw std::runtime_error("Embedding size mismatch");
    }

    float dot_product = 0.0f;
    float norm1 = 0.0f;
    float norm2 = 0.0f;

    for (size_t i = 0; i < emb1.features.size(); ++i) {
        dot_product += emb1.features[i] * emb2.features[i];
        norm1 += emb1.features[i] * emb1.features[i];
        norm2 += emb2.features[i] * emb2.features[i];
    }

    if (norm1 == 0.0f || norm2 == 0.0f) {
        return 0.0f;
    }

    return dot_product / (std::sqrt(norm1) * std::sqrt(norm2));
}

std::vector<Speaker> Diarizer::cluster_speakers(const std::vector<SpeakerEmbedding>& embeddings) {
    Logger::debug("Diarizer: clustering " + std::to_string(embeddings.size()) +
                  " embeddings with threshold " + std::to_string(options_.clustering_threshold));
    const std::vector<int> labels = detail::cluster_embeddings(
        embeddings, options_.clustering_threshold, options_.max_speakers);
    std::vector<int> final_id;
    return build_speakers(embeddings, labels, final_id);
}

// ═══════════════════════════════════════════════════════════
// Speaker Management
// ═══════════════════════════════════════════════════════════

void Diarizer::set_speaker_labels(DiarizationResult& result,
                                 const std::map<int, std::string>& labels) {
    // Update speaker objects
    for (auto& speaker : result.speakers) {
        auto it = labels.find(speaker.speaker_id);
        if (it != labels.end()) {
            speaker.label = it->second;
        }
    }

    // Update segments
    for (auto& seg : result.segments) {
        auto it = labels.find(seg.speaker_id);
        if (it != labels.end()) {
            seg.speaker_label = it->second;
        }
    }
}

Speaker Diarizer::get_speaker_stats(const DiarizationResult& result, int speaker_id) {
    for (const auto& speaker : result.speakers) {
        if (speaker.speaker_id == speaker_id) {
            return speaker;
        }
    }

    // Return empty speaker if not found
    return Speaker();
}

// ═══════════════════════════════════════════════════════════
// Speaker Formatting Helpers
// ═══════════════════════════════════════════════════════════

namespace SpeakerFormatting {

std::string format_speaker_text(const Segment& segment, const std::string& format) {
    std::string result = format;

    // Replace {label}
    size_t pos = result.find("{label}");
    if (pos != std::string::npos) {
        std::string label = segment.speaker_label.empty() ?
            ("Speaker " + std::to_string(segment.speaker_id)) : segment.speaker_label;
        result.replace(pos, 7, label);
    }

    // Replace {text}
    pos = result.find("{text}");
    if (pos != std::string::npos) {
        result.replace(pos, 6, segment.text);
    }

    return result;
}

std::string build_speaker_html(const Segment& segment,
                               const std::map<int, std::string>& speaker_colors) {
    std::string color = "#FFFFFF";  // Default white

    auto it = speaker_colors.find(segment.speaker_id);
    if (it != speaker_colors.end()) {
        color = it->second;
    }
    color = html_escape(color);

    const std::string label = segment.speaker_label.empty()
        ? "Speaker " + std::to_string(segment.speaker_id)
        : segment.speaker_label;

    std::ostringstream html;

    // Speaker label (smaller, dimmed). Label and text are user content
    // ("<3", "AT&T", a name with quotes), so both are escaped.
    html << "<font size='-1' color='" << color << "'>";
    html << "<b>[" << html_escape(label) << "]</b>";
    html << "</font> ";

    // Text (normal size, speaker color)
    html << "<font color='" << color << "'>";
    html << html_escape(segment.text);
    html << "</font>";

    return html.str();
}

std::map<int, std::string> generate_speaker_colors(int num_speakers) {
    std::map<int, std::string> colors;

    // Predefined distinct colors (optimized for readability)
    const std::vector<std::string> palette = {
        "#00D9FF",  // Cyan
        "#FF6B9D",  // Pink
        "#C9F04D",  // Lime
        "#FFB84D",  // Orange
        "#A78BFA",  // Purple
        "#34D399",  // Green
        "#FBBF24",  // Yellow
        "#F87171",  // Red
        "#60A5FA",  // Blue
        "#A3E635"   // Light Green
    };

    for (int i = 0; i < num_speakers; ++i) {
        colors[i] = palette[i % palette.size()];
    }

    return colors;
}

} // namespace SpeakerFormatting

} // namespace muninn
