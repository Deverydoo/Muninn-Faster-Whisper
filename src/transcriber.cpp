#include "muninn/transcriber.h"
#include "muninn/mel_spectrogram.h"
#include "muninn/audio_extractor.h"
#include <ctranslate2/models/whisper.h>
#include <ctranslate2/utils.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <regex>
#include <map>

namespace muninn {

// =======================
// Transcriber::Impl
// =======================

class Transcriber::Impl {
public:
    std::unique_ptr<ctranslate2::models::Whisper> model;
    MelSpectrogram mel_converter;
    bool model_loaded = false;
    std::string device_str;
    std::string compute_type_str;

    Impl() : mel_converter(16000, 400, 128, 160) {}

    // Convert audio samples to mel-spectrogram
    std::vector<std::vector<float>> compute_mel(const std::vector<float>& samples) {
        std::vector<std::vector<float>> mel_features;
        int n_frames = mel_converter.compute(samples, mel_features);

        if (n_frames == 0) {
            throw std::runtime_error("Failed to compute mel-spectrogram");
        }

        return mel_features;
    }

    // Transcribe a single chunk (≤30 seconds)
    std::vector<Segment> transcribe_chunk(
        const std::vector<std::vector<float>>& mel_features,
        float chunk_start_time,
        const TranscribeOptions& options
    );

    // Extract text from CTranslate2 result, filtering special tokens
    std::string extract_text(const std::vector<std::string>& tokens);

    // Check if segment is likely a hallucination
    bool is_hallucination(
        const Segment& segment,
        size_t num_tokens,
        float avg_logprob,
        float no_speech_prob,
        const TranscribeOptions& options
    );
};

std::string Transcriber::Impl::extract_text(const std::vector<std::string>& tokens) {
    std::string text;

    for (const auto& token : tokens) {
        // Skip special tokens (timestamps, language tokens, etc.)
        // Format: <|0.00|>, <|en|>, <|transcribe|>, etc.
        if (token.size() >= 4 && token.substr(0, 2) == "<|" && token.substr(token.size() - 2) == "|>") {
            continue;
        }

        // Replace GPT-2 BPE space marker (Ġ = U+0120) with actual space
        // UTF-8 encoding of U+0120 is: 0xC4 0xA0 (2 bytes)
        std::string processed = token;
        const std::string gpt2_space = "\xC4\xA0";  // Ġ in UTF-8
        size_t pos = 0;
        while ((pos = processed.find(gpt2_space, pos)) != std::string::npos) {
            processed.replace(pos, 2, " ");
            pos += 1;
        }

        text += processed;
    }

    // Trim whitespace
    text.erase(0, text.find_first_not_of(" \t\n\r"));
    text.erase(text.find_last_not_of(" \t\n\r") + 1);

    return text;
}

bool Transcriber::Impl::is_hallucination(
    const Segment& segment,
    size_t num_tokens,
    float avg_logprob,
    float no_speech_prob,
    const TranscribeOptions& options
) {
    // 1. No-speech detection (both conditions must be met)
    if (no_speech_prob > options.no_speech_threshold && avg_logprob < options.log_prob_threshold) {
        std::cerr << "[Muninn] Skipping no-speech segment (no_speech: " << no_speech_prob
                  << ", avg_logprob: " << avg_logprob << ")\n";
        return true;
    }

    // 2. Skip suspiciously short segments
    if (segment.text.length() <= 3) {
        std::cerr << "[Muninn] Skipping suspiciously short segment: '" << segment.text << "'\n";
        return true;
    }

    // 3. Skip very low token count with poor confidence
    if (num_tokens <= 2 && avg_logprob < -0.5f) {
        std::cerr << "[Muninn] Skipping low-token hallucination: '" << segment.text
                  << "' (tokens: " << num_tokens << ", avg_logprob: " << avg_logprob << ")\n";
        return true;
    }

    // 4. Repetition detection - catch patterns like "Thank you Thank you" or "A A A A"
    std::vector<std::string> words;
    std::istringstream iss(segment.text);
    std::string word;
    while (iss >> word) {
        words.push_back(word);
    }

    if (words.size() >= 3) {
        // Check if same word repeated multiple times consecutively
        int max_repeat_count = 1;
        for (size_t i = 0; i < words.size(); i++) {
            int repeat_count = 1;
            for (size_t j = i + 1; j < words.size() && words[j] == words[i]; j++) {
                repeat_count++;
            }
            max_repeat_count = std::max(max_repeat_count, repeat_count);
        }

        // If >50% of words are repetitions, likely hallucination
        if (max_repeat_count >= static_cast<int>(words.size()) / 2) {
            std::cerr << "[Muninn] Skipping repetitive hallucination: '"
                      << segment.text.substr(0, std::min(size_t(50), segment.text.length()))
                      << "' (repeat: " << max_repeat_count << "/" << words.size() << " words)\n";
            return true;
        }

        // Check for phrase repetition (e.g., "I'm going to be like" repeated)
        // Look for repeating n-grams where n = 3-6 words
        for (int ngram_size = 3; ngram_size <= 6 && ngram_size <= static_cast<int>(words.size()) / 2; ngram_size++) {
            std::map<std::string, int> ngram_counts;
            for (size_t i = 0; i + ngram_size <= words.size(); i++) {
                std::string ngram;
                for (int j = 0; j < ngram_size; j++) {
                    if (j > 0) ngram += " ";
                    ngram += words[i + j];
                }
                ngram_counts[ngram]++;
            }

            // If any n-gram appears 3+ times, it's a hallucination
            for (const auto& [ngram, count] : ngram_counts) {
                if (count >= 3) {
                    std::cerr << "[Muninn] Skipping phrase-repetition hallucination: '"
                              << ngram << "' repeated " << count << " times\n";
                    return true;
                }
            }
        }
    }

    // 5. Compression ratio check (like faster-whisper)
    float compression_ratio = static_cast<float>(num_tokens) /
                             static_cast<float>(std::max(1, static_cast<int>(segment.text.length())));

    if (compression_ratio > options.compression_ratio_threshold && avg_logprob < -0.5f) {
        std::cerr << "[Muninn] Skipping high-compression hallucination: '"
                  << segment.text.substr(0, std::min(size_t(50), segment.text.length()))
                  << "' (ratio: " << compression_ratio << ", logprob: " << avg_logprob << ")\n";
        return true;
    }

    return false;
}

std::vector<Segment> Transcriber::Impl::transcribe_chunk(
    const std::vector<std::vector<float>>& mel_features,
    float chunk_start_time,
    const TranscribeOptions& options
) {
    std::vector<Segment> segments;

    try {
        int n_frames = mel_features.size();
        int n_mels = mel_features[0].size();
        float chunk_duration = n_frames * 0.01f;  // 10ms per frame

        // Flatten mel_features to 1D array for StorageView
        // Whisper expects shape [batch, n_mels, n_frames] in row-major order
        // Our mel_features is [n_frames][n_mels], so we need to transpose it
        std::vector<float> flat_features;
        flat_features.reserve(n_frames * n_mels);

        // Transpose: iterate mel-by-mel (outer), then frame-by-frame (inner)
        for (int mel = 0; mel < n_mels; mel++) {
            for (int frame = 0; frame < n_frames; frame++) {
                flat_features.push_back(mel_features[frame][mel]);
            }
        }

        // Create StorageView with shape [1, n_mels, n_frames]
        ctranslate2::StorageView features(
            ctranslate2::Shape{1, static_cast<ctranslate2::dim_t>(n_mels), static_cast<ctranslate2::dim_t>(n_frames)},
            flat_features
        );

        // Prepare prompts with Whisper special tokens
        // Format: [<|startoftranscript|>, <|en|>, <|transcribe|>]
        // NOTE: We enable timestamps - Whisper is designed for timestamped output!
        std::vector<std::vector<std::string>> prompts = {{
            "<|startoftranscript|>",
            "<|" + options.language + "|>",  // Language token
            "<|" + options.task + "|>"       // Task token
        }};

        // Configure Whisper options (matching faster-whisper defaults)
        ctranslate2::models::WhisperOptions whisper_options;
        whisper_options.beam_size = options.beam_size;
        whisper_options.patience = options.patience;
        whisper_options.length_penalty = options.length_penalty;
        whisper_options.repetition_penalty = options.repetition_penalty;
        whisper_options.no_repeat_ngram_size = options.no_repeat_ngram_size;
        whisper_options.max_length = options.max_length;
        whisper_options.sampling_topk = 1;  // Greedy search
        whisper_options.sampling_temperature = options.temperature;
        whisper_options.num_hypotheses = 1;
        whisper_options.return_scores = true;
        whisper_options.return_no_speech_prob = true;
        whisper_options.max_initial_timestamp_index = 50;
        whisper_options.suppress_blank = true;
        whisper_options.suppress_tokens = {-1};  // Load defaults from config.json

        // Run Whisper generation
        auto future_results = model->generate(features, prompts, whisper_options);

        if (future_results.empty()) {
            std::cerr << "[Muninn] No results from Whisper inference for chunk\n";
            return segments;
        }

        // Get result from first future
        auto result = future_results[0].get();

        // Calculate average log probability
        float avg_logprob = 0.0f;
        if (result.has_scores() && !result.scores.empty() && !result.sequences_ids.empty()) {
            size_t seq_len = result.sequences_ids[0].size();
            float cum_logprob = result.scores[0];
            avg_logprob = cum_logprob / static_cast<float>(seq_len + 1);
        }

        // Extract text from sequences
        if (!result.sequences.empty()) {
            Segment segment;
            segment.start = chunk_start_time;
            segment.end = chunk_start_time + chunk_duration;
            segment.text = extract_text(result.sequences[0]);
            segment.avg_logprob = avg_logprob;
            segment.no_speech_prob = result.no_speech_prob;

            size_t num_tokens = result.sequences_ids[0].size();
            segment.compression_ratio = static_cast<float>(num_tokens) /
                                       static_cast<float>(std::max(1, static_cast<int>(segment.text.length())));

            // Check for hallucinations
            if (is_hallucination(segment, num_tokens, avg_logprob, result.no_speech_prob, options)) {
                return segments;  // Return empty
            }

            if (!segment.text.empty()) {
                segments.push_back(segment);
                std::cout << "[Muninn] Chunk [" << segment.start << "-" << segment.end << "]: "
                         << segment.text.substr(0, std::min(size_t(80), segment.text.length())) << "\n";
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[Muninn] Chunk transcription failed: " << e.what() << "\n";
    }

    return segments;
}

// =======================
// Transcriber Public API
// =======================

Transcriber::Transcriber(
    const std::string& model_path,
    const std::string& device,
    const std::string& compute_type
) : pimpl_(std::make_unique<Impl>()) {

    try {
        std::cout << "═══════════════════════════════════════════════════════════\n";
        std::cout << "MUNINN FASTER-WHISPER - LOADING MODEL\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

        // Determine short model name from path
        std::string model_name = "unknown";
        if (model_path.find("large-v3-turbo") != std::string::npos) {
            model_name = "large-v3-turbo";
        } else if (model_path.find("large-v3") != std::string::npos) {
            model_name = "large-v3";
        } else if (model_path.find("large-v2") != std::string::npos) {
            model_name = "large-v2";
        } else if (model_path.find("medium") != std::string::npos) {
            model_name = "medium";
        } else if (model_path.find("small") != std::string::npos) {
            model_name = "small";
        } else if (model_path.find("base") != std::string::npos) {
            model_name = "base";
        } else if (model_path.find("tiny") != std::string::npos) {
            model_name = "tiny";
        }

        std::cout << "Model: " << model_name << "\n";

        // Determine device
        ctranslate2::Device ct_device;
        if (device == "cuda") {
            ct_device = ctranslate2::Device::CUDA;
            std::cout << "Device: CUDA (GPU)\n";
        } else if (device == "cpu") {
            ct_device = ctranslate2::Device::CPU;
            std::cout << "Device: CPU\n";
        } else {
            // Auto-detect
            ct_device = ctranslate2::Device::CUDA;  // Try CUDA first
            std::cout << "Device: Auto (trying CUDA)\n";
        }

        // Load the model
        pimpl_->model = std::make_unique<ctranslate2::models::Whisper>(
            model_path,
            ct_device
        );

        // Get model information
        size_t num_languages = pimpl_->model->num_languages();
        bool is_multilingual = pimpl_->model->is_multilingual();
        size_t n_mels = pimpl_->model->n_mels();

        std::cout << "Languages: " << (is_multilingual ? "Multilingual" : "English-only")
                  << " (" << num_languages << " languages)\n";
        std::cout << "Mel features: " << n_mels << "\n";

        // Reconfigure mel-spectrogram converter to match model's expected mel bins
        if (n_mels != static_cast<size_t>(pimpl_->mel_converter.getMelBins())) {
            std::cout << "Reconfiguring mel-spectrogram: " << pimpl_->mel_converter.getMelBins()
                      << " -> " << n_mels << " mel bins\n";
            pimpl_->mel_converter = MelSpectrogram(16000, 400, static_cast<int>(n_mels), 160);
        }

        pimpl_->model_loaded = true;
        pimpl_->device_str = device;
        pimpl_->compute_type_str = compute_type;

        std::cout << "✓ Model loaded successfully\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

    } catch (const std::exception& e) {
        std::cerr << "✗ Failed to load Whisper model: " << e.what() << "\n";
        std::cerr << "═══════════════════════════════════════════════════════════\n";
        pimpl_->model_loaded = false;
        throw;
    }
}

Transcriber::~Transcriber() = default;

Transcriber::Transcriber(Transcriber&&) noexcept = default;
Transcriber& Transcriber::operator=(Transcriber&&) noexcept = default;

TranscribeResult Transcriber::transcribe(
    const std::vector<float>& audio_samples,
    int sample_rate,
    const TranscribeOptions& options
) {
    TranscribeResult result;

    if (!pimpl_->model_loaded) {
        throw std::runtime_error("Whisper model not loaded");
    }

    try {
        // TODO: Resample if sample_rate != 16000
        if (sample_rate != 16000) {
            throw std::runtime_error("Only 16kHz audio is currently supported. Resampling not yet implemented.");
        }

        std::cout << "[Muninn] Audio: " << audio_samples.size() << " samples, duration: "
                  << (audio_samples.size() / 16000.0f) << "s\n";

        // Convert to mel-spectrogram
        std::cout << "[Muninn] Converting to mel-spectrogram\n";
        auto mel_features = pimpl_->compute_mel(audio_samples);

        int n_frames = mel_features.size();
        std::cout << "[Muninn] Mel-spectrogram: " << n_frames << " frames x "
                  << pimpl_->mel_converter.getMelBins() << " mels\n";

        result.duration = audio_samples.size() / 16000.0f;

        // Track repeated segments across chunks to detect hallucinations like "Thank you" repeated
        std::map<std::string, int> segment_text_counts;

        // Whisper CTranslate2 has a maximum input length of 3000 frames (30 seconds)
        constexpr int MAX_FRAMES = 3000;

        if (n_frames > MAX_FRAMES) {
            std::cout << "[Muninn] Audio too long (" << n_frames << " frames), splitting into chunks of "
                      << MAX_FRAMES << " frames\n";

            // Calculate number of chunks needed
            int num_chunks = (n_frames + MAX_FRAMES - 1) / MAX_FRAMES;
            std::cout << "[Muninn] Processing " << num_chunks << " chunk(s)\n";

            // Process each chunk
            for (int chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
                int start_frame = chunk_idx * MAX_FRAMES;
                int end_frame = std::min(start_frame + MAX_FRAMES, n_frames);
                int chunk_frames = end_frame - start_frame;

                float chunk_start_time = start_frame * 0.01f;  // 10ms per frame
                float chunk_duration = chunk_frames * 0.01f;

                std::cout << "[Muninn] Processing chunk " << (chunk_idx + 1) << "/" << num_chunks
                          << ": frames " << start_frame << "-" << end_frame
                          << " (" << chunk_start_time << "s - " << (chunk_start_time + chunk_duration) << "s)\n";

                // Extract chunk from mel_features
                std::vector<std::vector<float>> chunk_features(
                    mel_features.begin() + start_frame,
                    mel_features.begin() + end_frame
                );

                // Transcribe this chunk
                auto chunk_segments = pimpl_->transcribe_chunk(chunk_features, chunk_start_time, options);

                // Filter repeated segments across chunks (hallucination detection)
                for (auto& seg : chunk_segments) {
                    // Normalize text for comparison (lowercase, trim)
                    std::string normalized = seg.text;
                    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

                    segment_text_counts[normalized]++;

                    // If same text appears 3+ times across chunks, it's a hallucination
                    if (segment_text_counts[normalized] >= 3) {
                        std::cerr << "[Muninn] Skipping cross-chunk hallucination (appears "
                                  << segment_text_counts[normalized] << " times): '" << seg.text << "'\n";
                        continue;  // Skip this segment
                    }

                    result.segments.push_back(seg);
                }
            }

            std::cout << "[Muninn] Completed chunked transcription: " << result.segments.size() << " total segments\n";

        } else {
            // Single chunk processing (audio <= 30 seconds)
            std::cout << "[Muninn] Audio short enough for single-pass transcription\n";
            result.segments = pimpl_->transcribe_chunk(mel_features, 0.0f, options);
        }

        // Set language (for now, just use the option)
        result.language = options.language;
        result.language_probability = 1.0f;  // TODO: Implement actual language detection

    } catch (const std::exception& e) {
        std::cerr << "[Muninn] Transcription failed: " << e.what() << "\n";
        throw;
    }

    return result;
}

TranscribeResult Transcriber::transcribe(
    const std::string& audio_path,
    const TranscribeOptions& options
) {
    TranscribeResult combined_result;

    // Open audio file to get track count
    AudioExtractor extractor;

    std::cout << "[Muninn] Loading audio from: " << audio_path << "\n";

    if (!extractor.open(audio_path)) {
        throw std::runtime_error("Failed to open audio file: " + extractor.get_last_error());
    }

    int track_count = extractor.get_track_count();
    float duration = extractor.get_duration();

    std::cout << "[Muninn] Found " << track_count << " audio track(s), duration: " << duration << "s\n";

    combined_result.duration = duration;
    combined_result.language = options.language;
    combined_result.language_probability = 1.0f;

    // Process each track
    for (int track = 0; track < track_count; ++track) {
        std::cout << "\n═══════════════════════════════════════════════════════════\n";
        std::cout << "[Muninn] Processing Track " << track << "/" << track_count << "\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

        std::vector<float> samples;
        if (!extractor.extract_track(track, samples)) {
            std::cerr << "[Muninn] WARNING: Failed to extract track " << track << ": "
                      << extractor.get_last_error() << "\n";
            continue;
        }

        std::cout << "[Muninn] Track " << track << ": " << samples.size() << " samples\n";

        // Transcribe this track
        try {
            auto track_result = transcribe(samples, 16000, options);

            // Add track label prefix to each segment
            for (auto& segment : track_result.segments) {
                segment.text = "[Track " + std::to_string(track) + "] " + segment.text;
            }

            // Merge into combined result
            combined_result.segments.insert(combined_result.segments.end(),
                track_result.segments.begin(), track_result.segments.end());

            std::cout << "[Muninn] Track " << track << ": " << track_result.segments.size() << " segment(s)\n";

        } catch (const std::exception& e) {
            std::cerr << "[Muninn] WARNING: Track " << track << " transcription failed: " << e.what() << "\n";
        }
    }

    extractor.close();

    std::cout << "\n═══════════════════════════════════════════════════════════\n";
    std::cout << "[Muninn] All tracks complete. Total segments: " << combined_result.segments.size() << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";

    return combined_result;
}

Transcriber::ModelInfo Transcriber::get_model_info() const {
    ModelInfo info;

    if (!pimpl_->model_loaded) {
        throw std::runtime_error("Model not loaded");
    }

    info.is_multilingual = pimpl_->model->is_multilingual();
    info.n_mels = pimpl_->model->n_mels();
    info.num_languages = pimpl_->model->num_languages();

    // Determine model type from n_mels or other characteristics
    // This is a simplified detection
    info.model_type = "unknown";

    return info;
}

} // namespace muninn
