#include "muninn/diarization.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <iostream>

namespace muninn {

// ═══════════════════════════════════════════════════════════
// Diarizer Implementation
// ═══════════════════════════════════════════════════════════

Diarizer::Diarizer(const std::string& embedding_model_path,
                   const DiarizationOptions& options)
    : options_(options) {

    if (embedding_model_path.empty()) {
        throw std::runtime_error("Embedding model path cannot be empty");
    }

    options_.embedding_model_path = embedding_model_path;

    // Initialize ONNX Runtime
    initialize_onnx_session();
}

Diarizer::~Diarizer() {
    // ONNX Runtime cleanup handled by unique_ptr
}

void Diarizer::initialize_onnx_session() {
    try {
        // Create ONNX Runtime environment
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MuninnDiarizer");

        // Session options
        session_options_ = std::make_unique<Ort::SessionOptions>();
        session_options_->SetIntraOpNumThreads(options_.num_threads);
        session_options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Set execution provider (CUDA or CPU)
        if (options_.device == "cuda") {
            OrtCUDAProviderOptions cuda_options;
            cuda_options.device_id = 0;
            session_options_->AppendExecutionProvider_CUDA(cuda_options);
        }

        // Load embedding model
        #ifdef _WIN32
        std::wstring model_path_wide(options_.embedding_model_path.begin(),
                                     options_.embedding_model_path.end());
        embedding_session_ = std::make_unique<Ort::Session>(*env_, model_path_wide.c_str(),
                                                            *session_options_);
        #else
        embedding_session_ = std::make_unique<Ort::Session>(*env_, options_.embedding_model_path.c_str(),
                                                            *session_options_);
        #endif

        // Get input/output metadata
        Ort::AllocatorWithDefaultOptions allocator;

        // Input shape (pyannote expects: [batch, samples])
        auto input_info = embedding_session_->GetInputTypeInfo(0);
        auto tensor_info = input_info.GetTensorTypeAndShapeInfo();
        input_shape_ = tensor_info.GetShape();

        // Input/output names
        auto input_name = embedding_session_->GetInputNameAllocated(0, allocator);
        auto output_name = embedding_session_->GetOutputNameAllocated(0, allocator);

        input_names_.push_back(input_name.get());
        output_names_.push_back(output_name.get());

        std::cout << "[Diarizer] Loaded embedding model: " << options_.embedding_model_path << "\n";
        std::cout << "[Diarizer] Device: " << options_.device << "\n";

    } catch (const Ort::Exception& e) {
        throw std::runtime_error("Failed to initialize ONNX session: " + std::string(e.what()));
    }
}

bool Diarizer::is_ready() const {
    return embedding_session_ != nullptr;
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
        std::cerr << "[Diarizer] No embeddings extracted (audio too short?)\n";
        return result;
    }

    std::cout << "[Diarizer] Extracted " << embeddings.size() << " embeddings\n";

    // Step 2: Cluster embeddings into speakers
    auto speakers = cluster_speakers(embeddings);

    std::cout << "[Diarizer] Detected " << speakers.size() << " speakers\n";

    // Step 3: Convert to time-aligned segments
    auto segments = embeddings_to_segments(embeddings, speakers);

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

        segments = merged;
    }

    result.segments = segments;
    result.speakers = speakers;
    result.num_speakers = speakers.size();

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

    // Run ONNX model
    auto features = run_embedding_model(audio_data, num_samples);

    embedding.features = features;

    return embedding;
}

std::vector<SpeakerEmbedding> Diarizer::extract_embeddings(const float* audio_data,
                                                           size_t num_samples,
                                                           int sample_rate) {
    std::vector<SpeakerEmbedding> embeddings;

    const size_t window_samples = static_cast<size_t>(options_.embedding_window_s * sample_rate);
    const size_t step_samples = static_cast<size_t>(options_.embedding_step_s * sample_rate);

    size_t pos = 0;
    while (pos + window_samples <= num_samples) {
        SpeakerEmbedding emb = extract_embedding(audio_data + pos, window_samples);

        emb.start = static_cast<float>(pos) / sample_rate;
        emb.end = static_cast<float>(pos + window_samples) / sample_rate;

        embeddings.push_back(emb);

        pos += step_samples;
    }

    return embeddings;
}

std::vector<float> Diarizer::run_embedding_model(const float* audio_data, size_t num_samples) {
    try {
        Ort::AllocatorWithDefaultOptions allocator;
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

        // This model expects exactly [1, 16000] shape (1 second of audio)
        // Pad or truncate to 16000 samples
        const size_t expected_samples = 16000;
        std::vector<float> input_data(expected_samples, 0.0f);

        size_t copy_size = std::min(num_samples, expected_samples);
        std::copy(audio_data, audio_data + copy_size, input_data.begin());

        // Shape: [batch=1, num_samples=16000]
        std::vector<int64_t> input_shape = {1, 16000};

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_data.data(), input_data.size(),
            input_shape.data(), input_shape.size());

        // Use "waveform" as the input name (from our ONNX conversion script)
        const char* input_name = "waveform";

        // Get output name dynamically (this model may have different output names)
        auto output_name_ort = embedding_session_->GetOutputNameAllocated(0, allocator);
        const char* output_name = output_name_ort.get();

        // Run inference
        auto output_tensors = embedding_session_->Run(
            Ort::RunOptions{nullptr},
            &input_name, &input_tensor, 1,
            &output_name, 1);

        // Extract output (should be [1, embedding_dim] for pyannote)
        float* output_data = output_tensors[0].GetTensorMutableData<float>();
        auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

        size_t embedding_size = output_shape.back();  // Embedding dimension
        std::vector<float> embedding(output_data, output_data + embedding_size);

        return embedding;

    } catch (const Ort::Exception& e) {
        throw std::runtime_error("ONNX embedding extraction failed: " + std::string(e.what()));
    }
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
    std::vector<Speaker> speakers;

    if (embeddings.empty()) {
        return speakers;
    }

    // Simple agglomerative clustering using cosine similarity
    std::vector<int> cluster_labels(embeddings.size(), -1);
    int next_speaker_id = 0;

    for (size_t i = 0; i < embeddings.size(); ++i) {
        if (cluster_labels[i] >= 0) {
            continue;  // Already assigned
        }

        // Start new cluster
        int current_speaker_id = next_speaker_id++;
        cluster_labels[i] = current_speaker_id;

        // Find all similar embeddings
        for (size_t j = i + 1; j < embeddings.size(); ++j) {
            if (cluster_labels[j] >= 0) {
                continue;
            }

            float similarity = cosine_similarity(embeddings[i], embeddings[j]);

            if (similarity >= options_.clustering_threshold) {
                cluster_labels[j] = current_speaker_id;
            }
        }
    }

    // Build Speaker objects
    int max_speaker_id = *std::max_element(cluster_labels.begin(), cluster_labels.end());

    for (int speaker_id = 0; speaker_id <= max_speaker_id; ++speaker_id) {
        Speaker speaker;
        speaker.speaker_id = speaker_id;
        speaker.label = "Speaker " + std::to_string(speaker_id);

        // Collect embeddings for this speaker
        for (size_t i = 0; i < embeddings.size(); ++i) {
            if (cluster_labels[i] == speaker_id) {
                speaker.embeddings.push_back(embeddings[i]);
                speaker.total_duration += (embeddings[i].end - embeddings[i].start);
            }
        }

        speakers.push_back(speaker);
    }

    // Sort by total duration (most active speaker first)
    std::sort(speakers.begin(), speakers.end(),
              [](const Speaker& a, const Speaker& b) {
                  return a.total_duration > b.total_duration;
              });

    // Reassign speaker IDs based on duration
    for (size_t i = 0; i < speakers.size(); ++i) {
        speakers[i].speaker_id = i;
        speakers[i].label = "Speaker " + std::to_string(i);
    }

    return speakers;
}

std::vector<DiarizationSegment> Diarizer::embeddings_to_segments(
    const std::vector<SpeakerEmbedding>& embeddings,
    const std::vector<Speaker>& speakers) {

    std::vector<DiarizationSegment> segments;

    for (const auto& emb : embeddings) {
        // Find which speaker this embedding belongs to
        int speaker_id = -1;
        float max_similarity = -1.0f;

        for (const auto& speaker : speakers) {
            for (const auto& speaker_emb : speaker.embeddings) {
                // Check if this embedding matches this speaker's embeddings
                if (std::abs(emb.start - speaker_emb.start) < 0.01f &&
                    std::abs(emb.end - speaker_emb.end) < 0.01f) {
                    speaker_id = speaker.speaker_id;
                    max_similarity = 1.0f;  // Perfect match
                    break;
                }
            }
            if (speaker_id >= 0) break;
        }

        if (speaker_id >= 0) {
            DiarizationSegment seg;
            seg.start = emb.start;
            seg.end = emb.end;
            seg.speaker_id = speaker_id;
            seg.speaker_label = "Speaker " + std::to_string(speaker_id);
            seg.confidence = max_similarity;

            segments.push_back(seg);
        }
    }

    return segments;
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

    std::ostringstream html;

    // Speaker label (smaller, dimmed)
    html << "<font size='-1' color='" << color << "'>";
    html << "<b>[" << segment.speaker_label << "]</b>";
    html << "</font> ";

    // Text (normal size, speaker color)
    html << "<font color='" << color << "'>";
    html << segment.text;
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
