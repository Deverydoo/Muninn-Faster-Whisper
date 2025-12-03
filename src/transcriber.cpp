#include "muninn/transcriber.h"
#include "muninn/mel_spectrogram.h"
#include "muninn/audio_extractor.h"
#include "muninn/vad.h"
#include "muninn/silero_vad.h"
#include "muninn/diarization.h"
#include <ctranslate2/models/whisper.h>
#include <ctranslate2/utils.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

/**
 * @brief Remap a timestamp from filtered audio back to original timeline
 *
 * When VAD removes silence, we concatenate speech segments. This function
 * maps a timestamp in the filtered audio back to the original audio timeline.
 *
 * @param filtered_time Timestamp in filtered (concatenated) audio
 * @param segments VAD speech segments with original timeline start/end
 * @return Timestamp in original audio timeline
 */
float remap_timestamp_to_original(float filtered_time, const std::vector<muninn::SpeechSegment>& segments) {
    if (segments.empty()) {
        return filtered_time;  // No VAD applied
    }

    // Walk through segments to find which one contains this timestamp
    float accumulated_duration = 0.0f;

    for (const auto& seg : segments) {
        float seg_duration = seg.end - seg.start;

        if (filtered_time <= accumulated_duration + seg_duration) {
            // This timestamp falls within this segment
            float offset_in_segment = filtered_time - accumulated_duration;
            return seg.start + offset_in_segment;
        }

        accumulated_duration += seg_duration;
    }

    // Past all segments - return end of last segment
    if (!segments.empty()) {
        return segments.back().end;
    }
    return filtered_time;
}

} // anonymous namespace
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

    // Token IDs for alignment (cached after model load)
    size_t sot_id = 0;           // Start of transcript
    size_t eot_id = 0;           // End of text
    size_t no_timestamps_id = 0; // No timestamps token
    size_t timestamp_begin = 0;  // First timestamp token ID
    bool tokens_initialized = false;

    Impl() : mel_converter(16000, 400, 128, 160) {}

    // Initialize token IDs from vocabulary
    void initialize_token_ids() {
        if (tokens_initialized || !model) return;

        // Get vocabulary through the replica
        const auto& replica = model->get_first_replica();

        // For Whisper models, standard token IDs are:
        // <|startoftranscript|> = vocabulary.bos_id()
        // <|endoftext|> = vocabulary.eos_id()
        // <|notimestamps|> is typically at 50362 (multilingual) or 50257+100 = 50363
        // Timestamp tokens start at 50364 (multilingual) or 50257+101 for en-only

        // We can determine if multilingual by checking num_languages
        bool is_multilingual = model->is_multilingual();

        if (is_multilingual) {
            // Multilingual model (e.g., large-v3)
            sot_id = 50258;           // <|startoftranscript|>
            eot_id = 50257;           // <|endoftext|>
            no_timestamps_id = 50363; // <|notimestamps|>
            timestamp_begin = 50364;  // First timestamp token <|0.00|>
        } else {
            // English-only model
            sot_id = 50257;           // <|startoftranscript|>
            eot_id = 50256;           // <|endoftext|>
            no_timestamps_id = 50362; // <|notimestamps|>
            timestamp_begin = 50363;  // First timestamp token
        }

        tokens_initialized = true;
        std::cout << "[Muninn] Token IDs initialized: sot=" << sot_id
                  << ", eot=" << eot_id << ", timestamp_begin=" << timestamp_begin << "\n";
    }

    // Detect language from mel-spectrogram features
    std::pair<std::string, float> detect_language(
        const std::vector<std::vector<float>>& mel_features
    ) {
        int n_frames = mel_features.size();
        int n_mels = mel_features[0].size();

        // Flatten and transpose mel features for CTranslate2
        std::vector<float> flat_features;
        flat_features.reserve(n_frames * n_mels);

        for (int mel = 0; mel < n_mels; mel++) {
            for (int frame = 0; frame < n_frames; frame++) {
                flat_features.push_back(mel_features[frame][mel]);
            }
        }

        ctranslate2::StorageView features(
            ctranslate2::Shape{1, static_cast<ctranslate2::dim_t>(n_mels), static_cast<ctranslate2::dim_t>(n_frames)},
            flat_features
        );

        auto future_results = model->detect_language(features);
        if (future_results.empty()) {
            return {"en", 0.0f};  // Default fallback
        }

        auto lang_probs = future_results[0].get();
        if (lang_probs.empty()) {
            return {"en", 0.0f};
        }

        // Find language with highest probability
        auto best = std::max_element(lang_probs.begin(), lang_probs.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        // Strip <| and |> markers from language code if present
        std::string lang_code = best->first;
        if (lang_code.size() >= 4 && lang_code.substr(0, 2) == "<|" &&
            lang_code.substr(lang_code.size() - 2) == "|>") {
            lang_code = lang_code.substr(2, lang_code.size() - 4);
        }

        std::cout << "[Muninn] Detected language: " << lang_code
                  << " (probability: " << best->second << ")\n";

        return {lang_code, best->second};
    }

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
    // previous_text: Optional text from previous segment for context conditioning
    // previous_temperature: Temperature used for previous segment (for prompt reset logic)
    std::vector<Segment> transcribe_chunk(
        const std::vector<std::vector<float>>& mel_features,
        float chunk_start_time,
        const TranscribeOptions& options,
        const std::string& previous_text = "",
        float previous_temperature = 0.0f
    );

    // Batch transcribe multiple chunks at once (GPU parallel)
    std::vector<std::vector<Segment>> transcribe_batch(
        const std::vector<std::vector<std::vector<float>>>& batch_mel_features,
        const std::vector<float>& chunk_start_times,
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

/**
 * @brief Parse a timestamp token like "<|0.00|>" and return the time in seconds
 * @param token The token string
 * @return Time in seconds, or -1.0f if not a valid timestamp token
 */
float parse_timestamp_token(const std::string& token) {
    // Whisper timestamp tokens: <|0.00|>, <|0.02|>, ..., <|30.00|>
    // Format: <|XX.XX|> where XX.XX is time in seconds
    if (token.size() >= 6 && token.substr(0, 2) == "<|" && token.substr(token.size() - 2) == "|>") {
        std::string inner = token.substr(2, token.size() - 4);
        // Check if it looks like a number (timestamp)
        bool is_timestamp = true;
        bool has_dot = false;
        for (char c : inner) {
            if (c == '.') {
                if (has_dot) { is_timestamp = false; break; }
                has_dot = true;
            } else if (!std::isdigit(static_cast<unsigned char>(c))) {
                is_timestamp = false;
                break;
            }
        }
        if (is_timestamp && has_dot) {
            try {
                return std::stof(inner);
            } catch (...) {}
        }
    }
    return -1.0f;
}

/**
 * @brief Check if a token starts a new word (has GPT-2 BPE space marker Ġ)
 *
 * In GPT-2/Whisper tokenization:
 * - Tokens starting with Ġ (U+0120) indicate a new word (space before)
 * - Tokens WITHOUT Ġ are continuations of the previous token
 *
 * Examples:
 * - "Ġdon" → starts word "don"
 * - "'t" → continues previous word → "don't"
 * - "Ġlike" → starts word "like"
 */
bool is_word_start(const std::string& token) {
    const std::string gpt2_space = "\xC4\xA0";  // Ġ in UTF-8 (U+0120)
    return token.size() >= 2 && token.substr(0, 2) == gpt2_space;
}

/**
 * @brief Check if a token is punctuation-only (no alphanumeric content)
 */
bool is_punctuation_only(const std::string& token) {
    // Strip the Ġ prefix if present
    std::string text = token;
    const std::string gpt2_space = "\xC4\xA0";
    if (text.size() >= 2 && text.substr(0, 2) == gpt2_space) {
        text = text.substr(2);
    }

    // Empty after stripping = just a space marker
    if (text.empty()) return true;

    // Check if any alphanumeric character exists
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            return false;  // Has alphanumeric content = real word
        }
    }
    return true;  // Only punctuation/symbols
}

/**
 * @brief Clean a text token by removing GPT-2 BPE markers entirely (no space replacement)
 */
std::string clean_token_raw(const std::string& token) {
    std::string processed = token;
    const std::string gpt2_space = "\xC4\xA0";  // Ġ in UTF-8 (U+0120)
    size_t pos = 0;
    while ((pos = processed.find(gpt2_space, pos)) != std::string::npos) {
        processed.erase(pos, 2);  // Remove the BPE marker entirely
    }
    return processed;
}

/**
 * @brief Clean a text token by replacing GPT-2 BPE markers with spaces
 */
std::string clean_token(const std::string& token) {
    std::string processed = token;
    const std::string gpt2_space = "\xC4\xA0";  // Ġ in UTF-8 (U+0120)
    size_t pos = 0;
    while ((pos = processed.find(gpt2_space, pos)) != std::string::npos) {
        processed.replace(pos, 2, " ");
        pos += 1;
    }
    return processed;
}

/**
 * @brief Check if token is a special non-timestamp token (language, task, etc.)
 */
bool is_special_token(const std::string& token) {
    if (token.size() < 4) return false;
    if (token.substr(0, 2) != "<|" || token.substr(token.size() - 2) != "|>") return false;

    // Check if it's NOT a timestamp (timestamps are numbers)
    std::string inner = token.substr(2, token.size() - 4);
    if (inner.empty()) return true;

    // If first char is digit, might be timestamp
    if (std::isdigit(static_cast<unsigned char>(inner[0]))) {
        return false;  // Let parse_timestamp_token handle it
    }

    // Known special tokens
    return inner == "startoftranscript" || inner == "endoftext" ||
           inner == "transcribe" || inner == "translate" ||
           inner == "notimestamps" || inner == "startoflm" ||
           inner.length() == 2;  // Language codes like "en", "es", etc.
}

std::string Transcriber::Impl::extract_text(const std::vector<std::string>& tokens) {
    std::string text;

    for (const auto& token : tokens) {
        // Skip special tokens (timestamps, language tokens, etc.)
        // Format: <|0.00|>, <|en|>, <|transcribe|>, etc.
        if (token.size() >= 4 && token.substr(0, 2) == "<|" && token.substr(token.size() - 2) == "|>") {
            continue;
        }

        text += clean_token(token);
    }

    // Trim whitespace
    text.erase(0, text.find_first_not_of(" \t\n\r"));
    text.erase(text.find_last_not_of(" \t\n\r") + 1);

    return text;
}

/**
 * @brief Extract word-level timestamps using CTranslate2 alignment data
 *
 * Uses cross-attention weights from Whisper to determine when each token
 * was spoken in the audio. This provides much more accurate word timing
 * than character-count distribution.
 *
 * @param seg The segment to add words to (modified in place)
 * @param alignment Cross-attention alignment data for each token
 * @param token_ids Token IDs from the result
 * @param tokens Text tokens from the result
 * @param word_buffer Buffer of (word_text, token_indices) pairs
 * @param seg_start Segment start time
 * @param seg_end Segment end time
 */
void extract_words_from_alignment(
    Segment& seg,
    const std::vector<std::vector<float>>& alignment,
    const std::vector<size_t>& token_ids,
    const std::vector<std::string>& tokens,
    const std::vector<std::pair<std::string, std::vector<size_t>>>& word_buffer,
    float seg_start,
    float seg_end
) {
    if (word_buffer.empty()) return;

    // Frames are 20ms apart (50 frames per second) in Whisper
    constexpr float FRAME_DURATION = 0.02f;  // 20ms per frame

    // Check alignment format:
    // New format from align(): each entry is [start_frame, end_frame, probability]
    // Old format (attention weights): each entry is vector of weights per frame
    bool use_new_format = !alignment.empty() && alignment[0].size() == 3;

    float prev_word_end = seg_start;

    // Track which alignment index corresponds to which text token
    // We need to map word_buffer token indices to alignment indices
    // The alignment data is for text tokens only (no timestamps/special tokens)
    size_t align_idx = 0;

    for (size_t wi = 0; wi < word_buffer.size(); ++wi) {
        const auto& [word_text, token_indices] = word_buffer[wi];

        if (token_indices.empty()) continue;

        float word_start_time = seg_end;
        float word_end_time = seg_start;
        float total_prob = 0.0f;

        if (use_new_format) {
            // New format: alignment[i] = [start_frame, end_frame, probability]
            // Each alignment entry corresponds to one text token
            for (size_t i = 0; i < token_indices.size() && align_idx < alignment.size(); ++i, ++align_idx) {
                const auto& align_entry = alignment[align_idx];
                float start_frame = align_entry[0];
                float end_frame = align_entry[1];
                float prob = align_entry[2];

                float token_start = seg_start + (start_frame * FRAME_DURATION);
                float token_end = seg_start + (end_frame * FRAME_DURATION);

                word_start_time = std::min(word_start_time, token_start);
                word_end_time = std::max(word_end_time, token_end);
                total_prob += prob;
            }
        } else {
            // Old format: attention weights per frame
            for (size_t tok_idx : token_indices) {
                if (tok_idx >= alignment.size()) continue;

                const auto& attn = alignment[tok_idx];
                if (attn.empty()) continue;

                // Find peak attention frame for this token
                float max_weight = 0.0f;
                size_t peak_frame = 0;
                for (size_t frame = 0; frame < attn.size(); ++frame) {
                    if (attn[frame] > max_weight) {
                        max_weight = attn[frame];
                        peak_frame = frame;
                    }
                }

                // Convert frame to time
                float token_time = seg_start + (peak_frame * FRAME_DURATION);
                word_start_time = std::min(word_start_time, token_time);
                word_end_time = std::max(word_end_time, token_time + FRAME_DURATION);
                total_prob += max_weight;
            }
        }

        // Ensure word timing is valid and sequential
        word_start_time = std::max(word_start_time, prev_word_end);
        word_end_time = std::max(word_end_time, word_start_time + 0.05f);  // Min 50ms per word
        word_end_time = std::min(word_end_time, seg_end);

        Word w;
        w.word = word_text;
        w.start = word_start_time;
        w.end = word_end_time;
        w.probability = total_prob / std::max(1.0f, static_cast<float>(token_indices.size()));

        seg.words.push_back(w);
        prev_word_end = word_end_time;
    }
}

/**
 * @brief Extract timestamped segments from Whisper output tokens
 *
 * Whisper outputs text interleaved with timestamp tokens:
 * <|0.00|> Hello world <|2.50|> How are you <|5.00|>
 *
 * This function parses these into separate segments with accurate timing.
 * When alignment data is available, it uses cross-attention weights for
 * accurate word-level timestamps instead of character-count distribution.
 *
 * @param tokens Output tokens from Whisper
 * @param token_ids Token IDs (for alignment mapping)
 * @param alignment Cross-attention alignment data (optional, for word timestamps)
 * @param chunk_start_time Base time offset for this chunk
 * @param word_timestamps If true, also extract word-level timing
 * @return Vector of segments with timestamps
 */
std::vector<Segment> extract_timestamped_segments(
    const std::vector<std::string>& tokens,
    const std::vector<size_t>& token_ids,
    const std::vector<std::vector<float>>& alignment,
    float chunk_start_time,
    bool word_timestamps
) {
    std::vector<Segment> segments;

    float current_start = -1.0f;
    std::string current_text;

    // Word buffer now stores (word_text, token_indices) for alignment lookup
    std::vector<std::pair<std::string, std::vector<size_t>>> word_buffer;

    // Track token index for alignment correlation
    size_t text_token_idx = 0;  // Index into non-timestamp tokens

    bool has_alignment = !alignment.empty();

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];

        // Check if this is a timestamp token
        float timestamp = parse_timestamp_token(token);

        if (timestamp >= 0.0f) {
            // This is a timestamp token
            float absolute_time = chunk_start_time + timestamp;

            if (current_start < 0.0f) {
                // First timestamp - marks start of first segment
                current_start = absolute_time;
            } else if (!current_text.empty()) {
                // End of segment - create it
                Segment seg;
                seg.start = current_start;
                seg.end = absolute_time;

                // Trim text
                current_text.erase(0, current_text.find_first_not_of(" \t\n\r"));
                if (!current_text.empty()) {
                    current_text.erase(current_text.find_last_not_of(" \t\n\r") + 1);
                }
                seg.text = current_text;

                // Extract word timestamps if requested
                if (word_timestamps && !word_buffer.empty()) {
                    if (has_alignment) {
                        // Use alignment data for accurate word timing
                        extract_words_from_alignment(seg, alignment, token_ids, tokens,
                                                      word_buffer, seg.start, seg.end);
                    } else {
                        // Fallback: Heuristic timing (align speech to end of segment)
                        // Key insight: Whisper segments often have silence at the START
                        // Speech typically occurs near the END of the segment

                        float seg_duration = seg.end - seg.start;
                        size_t word_count = word_buffer.size();

                        // Estimate actual speech duration based on word count
                        // Use ~0.35 sec/word as baseline (slightly faster than average)
                        float estimated_speech_duration = word_count * 0.35f;

                        // Cap at segment duration
                        if (estimated_speech_duration > seg_duration) {
                            estimated_speech_duration = seg_duration;
                        }

                        // Align speech to END of segment (where speech usually is)
                        float speech_start = seg.end - estimated_speech_duration;

                        // Distribute words proportionally by character count within speech window
                        size_t total_chars = 0;
                        for (const auto& [text, indices] : word_buffer) {
                            total_chars += text.length();
                        }

                        float word_start = speech_start;

                        for (const auto& [text, indices] : word_buffer) {
                            Word w;
                            w.word = text;
                            w.start = word_start;
                            float word_duration = (total_chars > 0)
                                ? estimated_speech_duration * (float(text.length()) / float(total_chars))
                                : estimated_speech_duration / float(word_buffer.size());
                            w.end = word_start + word_duration;
                            w.probability = 1.0f;
                            seg.words.push_back(w);
                            word_start = w.end;
                        }
                    }
                }

                if (!seg.text.empty()) {
                    segments.push_back(seg);
                }

                // Start new segment
                current_start = absolute_time;
                current_text.clear();
                word_buffer.clear();
            }
        } else if (!is_special_token(token)) {
            // Regular text token
            std::string cleaned = clean_token(token);
            current_text += cleaned;

            // For word timestamps, use BPE-aware token merging
            // GPT-2/Whisper BPE rules:
            // - Tokens starting with Ġ (U+0120) are word starts
            // - Tokens without Ġ are continuations of previous word
            // - Punctuation-only tokens attach to previous word
            if (word_timestamps) {
                std::string token_text = clean_token_raw(token);  // Remove Ġ without adding space

                if (token_text.empty()) {
                    // Empty token (just a space marker), skip
                    text_token_idx++;
                    continue;
                }

                bool starts_new_word = is_word_start(token);
                bool is_punct = is_punctuation_only(token);

                if (word_buffer.empty()) {
                    // First word token - always start a new word
                    word_buffer.push_back({token_text, {i}});
                } else if (is_punct) {
                    // Punctuation: attach to previous word (e.g., "them" + "," = "them,")
                    word_buffer.back().first += token_text;
                    word_buffer.back().second.push_back(i);
                } else if (!starts_new_word) {
                    // BPE continuation (no Ġ prefix): append to previous word
                    // Examples: "don" + "'t" = "don't", "Mo" + "e" = "Moe"
                    word_buffer.back().first += token_text;
                    word_buffer.back().second.push_back(i);
                } else {
                    // New word (has Ġ prefix): start a new entry
                    word_buffer.push_back({token_text, {i}});
                }
            }

            text_token_idx++;
        }
    }

    return segments;
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

/**
 * @brief Check if transcription result needs retry with higher temperature
 *
 * Based on faster-whisper's logic: retry if compression_ratio > threshold OR avg_logprob < threshold
 */
bool needs_temperature_fallback(float compression_ratio, float avg_logprob, const TranscribeOptions& options) {
    return compression_ratio > options.compression_ratio_threshold ||
           avg_logprob < options.log_prob_threshold;
}

/**
 * @brief Calculate how much of a segment overlaps with speech regions
 *
 * Returns a value between 0.0 (no overlap) and 1.0 (fully within speech regions)
 *
 * @param seg_start Segment start time (original timeline)
 * @param seg_end Segment end time (original timeline)
 * @param speech_segments VAD speech regions
 * @return Fraction of segment that overlaps with speech (0.0-1.0)
 */
float calculate_speech_overlap(
    float seg_start,
    float seg_end,
    const std::vector<SpeechSegment>& speech_segments
) {
    if (speech_segments.empty()) {
        return 1.0f;  // No VAD info, assume all speech
    }

    float seg_duration = seg_end - seg_start;
    if (seg_duration <= 0.0f) {
        return 0.0f;
    }

    float overlap_duration = 0.0f;

    for (const auto& speech : speech_segments) {
        // Calculate overlap between segment and this speech region
        float overlap_start = std::max(seg_start, speech.start);
        float overlap_end = std::min(seg_end, speech.end);

        if (overlap_end > overlap_start) {
            overlap_duration += (overlap_end - overlap_start);
        }
    }

    return std::min(1.0f, overlap_duration / seg_duration);
}

/**
 * @brief Filter segments that fall in silent regions (hallucination silence threshold)
 *
 * Based on faster-whisper's hallucination_silence_threshold parameter.
 * Removes segments where less than threshold fraction overlaps with speech regions.
 *
 * @param segments Segments to filter (modified in place)
 * @param speech_segments VAD speech regions
 * @param threshold Minimum speech overlap required (0.0 = disabled, 0.5 = 50% overlap required)
 */
void filter_silence_hallucinations(
    std::vector<Segment>& segments,
    const std::vector<SpeechSegment>& speech_segments,
    float threshold
) {
    if (threshold <= 0.0f || speech_segments.empty()) {
        return;  // Disabled or no VAD info
    }

    auto it = segments.begin();
    while (it != segments.end()) {
        float overlap = calculate_speech_overlap(it->start, it->end, speech_segments);

        if (overlap < threshold) {
            std::cerr << "[Muninn] Skipping silence hallucination: '"
                      << it->text.substr(0, std::min(size_t(50), it->text.length()))
                      << "' (speech overlap: " << (overlap * 100.0f) << "%)\n";
            it = segments.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<Segment> Transcriber::Impl::transcribe_chunk(
    const std::vector<std::vector<float>>& mel_features,
    float chunk_start_time,
    const TranscribeOptions& options,
    const std::string& previous_text,
    float previous_temperature
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
        // Format: [<|startoftranscript|>, <|en|>, <|transcribe|>, <|prev_text|>]
        // NOTE: We enable timestamps - Whisper is designed for timestamped output!
        std::vector<std::string> prompt_tokens = {
            "<|startoftranscript|>",
            "<|" + options.language + "|>",  // Language token
            "<|" + options.task + "|>"       // Task token
        };

        // Add previous text context if condition_on_previous is enabled
        // BUT reset context if previous segment used high temperature (unreliable output)
        std::string context_text = previous_text;
        bool should_use_context = options.condition_on_previous && !context_text.empty();

        // Prompt reset on high temperature - don't use previous context if it was unreliable
        if (should_use_context && previous_temperature >= options.prompt_reset_on_temperature) {
            std::cout << "[Muninn] Resetting prompt context (prev temp=" << previous_temperature
                      << " >= threshold=" << options.prompt_reset_on_temperature << ")\n";
            should_use_context = false;
        }

        if (should_use_context) {
            // Whisper expects previous text as a "<|startofprev|>" block
            // Trim to last ~224 tokens worth of text (~1000 chars) to avoid context overflow
            if (context_text.length() > 1000) {
                context_text = context_text.substr(context_text.length() - 1000);
                // Find word boundary
                size_t space_pos = context_text.find(' ');
                if (space_pos != std::string::npos) {
                    context_text = context_text.substr(space_pos + 1);
                }
            }
            prompt_tokens.push_back("<|startofprev|>");
            prompt_tokens.push_back(context_text);
            prompt_tokens.push_back("<|startoftranscript|>");
        }

        // Add initial prompt if specified
        if (!options.initial_prompt.empty()) {
            prompt_tokens.push_back("<|startofprev|>");
            prompt_tokens.push_back(options.initial_prompt);
            prompt_tokens.push_back("<|startoftranscript|>");
        }

        std::vector<std::vector<std::string>> prompts = {prompt_tokens};

        // Temperature fallback loop - try increasing temperatures on failure
        const auto& temperatures = options.temperature_fallback.empty()
            ? std::vector<float>{options.temperature}
            : options.temperature_fallback;

        float used_temperature = temperatures[0];

        for (size_t temp_idx = 0; temp_idx < temperatures.size(); ++temp_idx) {
            float current_temp = temperatures[temp_idx];

            // Configure Whisper options (matching faster-whisper defaults)
            ctranslate2::models::WhisperOptions whisper_options;
            whisper_options.beam_size = options.beam_size;
            whisper_options.patience = options.patience;
            whisper_options.length_penalty = options.length_penalty;
            whisper_options.repetition_penalty = options.repetition_penalty;
            whisper_options.no_repeat_ngram_size = options.no_repeat_ngram_size;
            whisper_options.max_length = options.max_length;
            whisper_options.sampling_topk = (current_temp > 0) ? 0 : 1;  // Use sampling for T > 0
            whisper_options.sampling_temperature = current_temp;
            whisper_options.num_hypotheses = 1;
            whisper_options.return_scores = true;
            whisper_options.return_no_speech_prob = true;
            whisper_options.max_initial_timestamp_index = 50;
            whisper_options.suppress_blank = options.suppress_blank;
            whisper_options.suppress_tokens = options.suppress_tokens;

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

            // Calculate compression ratio for fallback decision
            size_t num_tokens = result.sequences_ids.empty() ? 0 : result.sequences_ids[0].size();
            std::string full_text = extract_text(result.sequences[0]);
            float compression_ratio = static_cast<float>(num_tokens) /
                static_cast<float>(std::max(1, static_cast<int>(full_text.length())));

            // Check if we need to retry with higher temperature
            bool should_retry = needs_temperature_fallback(compression_ratio, avg_logprob, options);

            if (should_retry && temp_idx + 1 < temperatures.size()) {
                std::cout << "[Muninn] Temperature fallback: T=" << current_temp
                         << " -> T=" << temperatures[temp_idx + 1]
                         << " (compression=" << compression_ratio
                         << ", logprob=" << avg_logprob << ")\n";
                continue;  // Try next temperature
            }

            // Use this result (either good enough or last attempt)
            used_temperature = current_temp;

            // Extract text from sequences with proper timestamps
            if (!result.sequences.empty()) {
                // Get token IDs for alignment correlation
                std::vector<size_t> token_ids;
                if (!result.sequences_ids.empty()) {
                    token_ids = result.sequences_ids[0];
                }

                // Get alignment data using CTranslate2's align() for word timestamps
                std::vector<std::vector<float>> alignment_data;

                if (options.word_timestamps && tokens_initialized && !token_ids.empty()) {
                    try {
                        // Filter text tokens (exclude timestamps and special tokens)
                        std::vector<size_t> text_tokens;
                        for (size_t tok_id : token_ids) {
                            // Skip timestamp tokens (>= timestamp_begin)
                            if (tok_id >= timestamp_begin) continue;
                            // Skip special tokens (sot, eot, no_timestamps)
                            if (tok_id == sot_id || tok_id == eot_id || tok_id == no_timestamps_id) continue;
                            // Skip language/task tokens (usually in range 50259-50363 for multilingual)
                            if (tok_id >= 50259 && tok_id < timestamp_begin) continue;
                            text_tokens.push_back(tok_id);
                        }

                        if (!text_tokens.empty()) {
                            // Build start sequence: just [sot_id] for alignment
                            std::vector<size_t> start_sequence = {sot_id};

                            // Number of mel frames in this chunk
                            std::vector<size_t> num_frames_vec = {static_cast<size_t>(n_frames)};

                            // Median filter width (7 is standard)
                            ctranslate2::dim_t median_filter_width = 7;

                            // Call align() to get frame-accurate word alignments
                            auto align_futures = model->align(
                                features,
                                start_sequence,
                                {text_tokens},
                                num_frames_vec,
                                median_filter_width
                            );

                            if (!align_futures.empty()) {
                                auto align_result = align_futures[0].get();

                                // Alignments are DTW path entries: (token_index, frame_index)
                                // Group by token to get frame ranges for each token
                                std::vector<int64_t> token_start_frames(text_tokens.size(), -1);
                                std::vector<int64_t> token_end_frames(text_tokens.size(), -1);

                                for (const auto& [token_idx, frame_idx] : align_result.alignments) {
                                    if (token_idx >= 0 && static_cast<size_t>(token_idx) < text_tokens.size()) {
                                        if (token_start_frames[token_idx] < 0) {
                                            token_start_frames[token_idx] = frame_idx;
                                        }
                                        token_end_frames[token_idx] = frame_idx;
                                    }
                                }

                                // Build alignment_data with [start_frame, end_frame, probability]
                                alignment_data.reserve(text_tokens.size());
                                for (size_t i = 0; i < text_tokens.size(); ++i) {
                                    int64_t start_f = token_start_frames[i];
                                    int64_t end_f = token_end_frames[i];
                                    if (start_f < 0) start_f = 0;
                                    if (end_f < 0) end_f = start_f;

                                    float prob = (i < align_result.text_token_probs.size())
                                        ? align_result.text_token_probs[i] : 1.0f;

                                    alignment_data.push_back({
                                        static_cast<float>(start_f),
                                        static_cast<float>(end_f),
                                        prob
                                    });
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        // Alignment failed - fall back to heuristic approach
                        std::cerr << "[Muninn] Alignment failed, using heuristic: " << e.what() << "\n";
                        alignment_data.clear();
                    }
                }

                // Try to extract timestamped segments from Whisper's output
                auto timestamped_segs = extract_timestamped_segments(
                    result.sequences[0], token_ids, alignment_data,
                    chunk_start_time, options.word_timestamps);

                if (!timestamped_segs.empty()) {
                    // Use Whisper's native timestamp tokens for accurate timing
                    for (auto& seg : timestamped_segs) {
                        seg.avg_logprob = avg_logprob;
                        seg.no_speech_prob = result.no_speech_prob;
                        seg.temperature = used_temperature;
                        seg.compression_ratio = static_cast<float>(num_tokens) /
                            static_cast<float>(std::max(1, static_cast<int>(seg.text.length())));

                        // Check for hallucinations
                        if (is_hallucination(seg, num_tokens / std::max(size_t(1), timestamped_segs.size()),
                                            avg_logprob, result.no_speech_prob, options)) {
                            continue;  // Skip this segment
                        }

                        if (!seg.text.empty()) {
                            segments.push_back(seg);
                            std::cout << "[Muninn] Segment [" << seg.start << "-" << seg.end << "]: "
                                     << seg.text.substr(0, std::min(size_t(80), seg.text.length())) << "\n";
                        }
                    }
                } else {
                    // Fallback: No timestamp tokens found, use chunk boundaries
                    Segment segment;
                    segment.start = chunk_start_time;
                    segment.end = chunk_start_time + chunk_duration;
                    segment.text = full_text;
                    segment.avg_logprob = avg_logprob;
                    segment.no_speech_prob = result.no_speech_prob;
                    segment.temperature = used_temperature;
                    segment.compression_ratio = compression_ratio;

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
            }

            break;  // Success - exit temperature loop
        }

    } catch (const std::exception& e) {
        std::cerr << "[Muninn] Chunk transcription failed: " << e.what() << "\n";
    }

    return segments;
}

std::vector<std::vector<Segment>> Transcriber::Impl::transcribe_batch(
    const std::vector<std::vector<std::vector<float>>>& batch_mel_features,
    const std::vector<float>& chunk_start_times,
    const TranscribeOptions& options
) {
    std::vector<std::vector<Segment>> all_segments(batch_mel_features.size());

    if (batch_mel_features.empty()) {
        return all_segments;
    }

    try {
        size_t batch_size = batch_mel_features.size();
        std::cout << "[Muninn] Batch inference: " << batch_size << " chunks\n";

        // Find max frames across batch for padding
        int max_frames = 0;
        int n_mels = batch_mel_features[0][0].size();
        for (const auto& mel : batch_mel_features) {
            max_frames = std::max(max_frames, static_cast<int>(mel.size()));
        }

        // Build batched features tensor [batch_size, n_mels, max_frames]
        // Pad shorter sequences with zeros
        std::vector<float> flat_batch;
        flat_batch.reserve(batch_size * n_mels * max_frames);

        for (size_t b = 0; b < batch_size; ++b) {
            const auto& mel_features = batch_mel_features[b];
            int n_frames = mel_features.size();

            // Transpose and pad: [n_mels][max_frames]
            for (int mel = 0; mel < n_mels; mel++) {
                for (int frame = 0; frame < max_frames; frame++) {
                    if (frame < n_frames) {
                        flat_batch.push_back(mel_features[frame][mel]);
                    } else {
                        flat_batch.push_back(0.0f);  // Zero-pad
                    }
                }
            }
        }

        // Create batched StorageView [batch_size, n_mels, max_frames]
        ctranslate2::StorageView features(
            ctranslate2::Shape{
                static_cast<ctranslate2::dim_t>(batch_size),
                static_cast<ctranslate2::dim_t>(n_mels),
                static_cast<ctranslate2::dim_t>(max_frames)
            },
            flat_batch
        );

        // Prepare prompts for each batch item (same prompt for all)
        std::vector<std::vector<std::string>> prompts;
        for (size_t b = 0; b < batch_size; ++b) {
            prompts.push_back({
                "<|startoftranscript|>",
                "<|" + options.language + "|>",
                "<|" + options.task + "|>"
            });
        }

        // Configure Whisper options
        ctranslate2::models::WhisperOptions whisper_options;
        whisper_options.beam_size = options.beam_size;
        whisper_options.patience = options.patience;
        whisper_options.length_penalty = options.length_penalty;
        whisper_options.repetition_penalty = options.repetition_penalty;
        whisper_options.no_repeat_ngram_size = options.no_repeat_ngram_size;
        whisper_options.max_length = options.max_length;
        whisper_options.sampling_topk = 1;
        whisper_options.sampling_temperature = options.temperature;
        whisper_options.num_hypotheses = 1;
        whisper_options.return_scores = true;
        whisper_options.return_no_speech_prob = true;
        whisper_options.max_initial_timestamp_index = 50;
        whisper_options.suppress_blank = true;
        whisper_options.suppress_tokens = {-1};

        // Run batched Whisper generation
        auto future_results = model->generate(features, prompts, whisper_options);

        // Process results for each batch item
        for (size_t b = 0; b < batch_size; ++b) {
            auto result = future_results[b].get();

            int n_frames = batch_mel_features[b].size();
            float chunk_duration = n_frames * 0.01f;
            float chunk_start_time = chunk_start_times[b];

            // Calculate average log probability
            float avg_logprob = 0.0f;
            if (result.has_scores() && !result.scores.empty() && !result.sequences_ids.empty()) {
                size_t seq_len = result.sequences_ids[0].size();
                float cum_logprob = result.scores[0];
                avg_logprob = cum_logprob / static_cast<float>(seq_len + 1);
            }

            // Extract text with timestamps
            if (!result.sequences.empty()) {
                // Get token IDs for alignment correlation
                std::vector<size_t> token_ids;
                if (!result.sequences_ids.empty()) {
                    token_ids = result.sequences_ids[0];
                }

                // Get alignment data using CTranslate2's align() for word timestamps
                std::vector<std::vector<float>> alignment_data;

                if (options.word_timestamps && tokens_initialized && !token_ids.empty()) {
                    try {
                        // Filter text tokens (exclude timestamps and special tokens)
                        std::vector<size_t> text_tokens;
                        for (size_t tok_id : token_ids) {
                            if (tok_id >= timestamp_begin) continue;
                            if (tok_id == sot_id || tok_id == eot_id || tok_id == no_timestamps_id) continue;
                            if (tok_id >= 50259 && tok_id < timestamp_begin) continue;
                            text_tokens.push_back(tok_id);
                        }

                        if (!text_tokens.empty()) {
                            // Build features for this single chunk from the batch
                            // Note: We need to extract just this chunk's features
                            const auto& mel_features = batch_mel_features[b];
                            int chunk_n_frames = mel_features.size();
                            int chunk_n_mels = mel_features[0].size();

                            std::vector<float> chunk_flat;
                            chunk_flat.reserve(chunk_n_mels * chunk_n_frames);
                            for (int mel = 0; mel < chunk_n_mels; mel++) {
                                for (int frame = 0; frame < chunk_n_frames; frame++) {
                                    chunk_flat.push_back(mel_features[frame][mel]);
                                }
                            }

                            ctranslate2::StorageView chunk_features(
                                ctranslate2::Shape{1, static_cast<ctranslate2::dim_t>(chunk_n_mels),
                                                   static_cast<ctranslate2::dim_t>(chunk_n_frames)},
                                chunk_flat
                            );

                            std::vector<size_t> start_sequence = {sot_id};
                            std::vector<size_t> num_frames_vec = {static_cast<size_t>(chunk_n_frames)};
                            ctranslate2::dim_t median_filter_width = 7;

                            auto align_futures = model->align(
                                chunk_features, start_sequence, {text_tokens},
                                num_frames_vec, median_filter_width
                            );

                            if (!align_futures.empty()) {
                                auto align_result = align_futures[0].get();

                                // Alignments are DTW path entries: (token_index, frame_index)
                                // Group by token to get frame ranges for each token
                                std::vector<int64_t> token_start_frames(text_tokens.size(), -1);
                                std::vector<int64_t> token_end_frames(text_tokens.size(), -1);

                                for (const auto& [token_idx, frame_idx] : align_result.alignments) {
                                    if (token_idx >= 0 && static_cast<size_t>(token_idx) < text_tokens.size()) {
                                        if (token_start_frames[token_idx] < 0) {
                                            token_start_frames[token_idx] = frame_idx;
                                        }
                                        token_end_frames[token_idx] = frame_idx;
                                    }
                                }

                                // Build alignment_data with [start_frame, end_frame, probability]
                                alignment_data.reserve(text_tokens.size());
                                for (size_t i = 0; i < text_tokens.size(); ++i) {
                                    int64_t start_f = token_start_frames[i];
                                    int64_t end_f = token_end_frames[i];
                                    if (start_f < 0) start_f = 0;
                                    if (end_f < 0) end_f = start_f;

                                    float prob = (i < align_result.text_token_probs.size())
                                        ? align_result.text_token_probs[i] : 1.0f;

                                    alignment_data.push_back({
                                        static_cast<float>(start_f),
                                        static_cast<float>(end_f),
                                        prob
                                    });
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        // Alignment failed - fall back to heuristic
                        alignment_data.clear();
                    }
                }

                // Try to extract timestamped segments from Whisper's output
                auto timestamped_segs = extract_timestamped_segments(
                    result.sequences[0], token_ids, alignment_data,
                    chunk_start_time, options.word_timestamps);

                size_t num_tokens = result.sequences_ids[0].size();

                if (!timestamped_segs.empty()) {
                    // Use Whisper's native timestamp tokens
                    for (auto& seg : timestamped_segs) {
                        seg.avg_logprob = avg_logprob;
                        seg.no_speech_prob = result.no_speech_prob;
                        seg.compression_ratio = static_cast<float>(num_tokens) /
                            static_cast<float>(std::max(1, static_cast<int>(seg.text.length())));

                        if (!is_hallucination(seg, num_tokens / std::max(size_t(1), timestamped_segs.size()),
                                             avg_logprob, result.no_speech_prob, options)) {
                            if (!seg.text.empty()) {
                                all_segments[b].push_back(seg);
                                std::cout << "[Muninn] Batch[" << b << "] [" << seg.start << "-" << seg.end << "]: "
                                         << seg.text.substr(0, std::min(size_t(60), seg.text.length())) << "\n";
                            }
                        }
                    }
                } else {
                    // Fallback: No timestamp tokens, use chunk boundaries
                    Segment segment;
                    segment.start = chunk_start_time;
                    segment.end = chunk_start_time + chunk_duration;
                    segment.text = extract_text(result.sequences[0]);
                    segment.avg_logprob = avg_logprob;
                    segment.no_speech_prob = result.no_speech_prob;
                    segment.compression_ratio = static_cast<float>(num_tokens) /
                                               static_cast<float>(std::max(1, static_cast<int>(segment.text.length())));

                    if (!is_hallucination(segment, num_tokens, avg_logprob, result.no_speech_prob, options)) {
                        if (!segment.text.empty()) {
                            all_segments[b].push_back(segment);
                            std::cout << "[Muninn] Batch[" << b << "] [" << segment.start << "-" << segment.end << "]: "
                                     << segment.text.substr(0, std::min(size_t(60), segment.text.length())) << "\n";
                        }
                    }
                }
            }
        }

        std::cout << "[Muninn] Batch complete\n";

    } catch (const std::exception& e) {
        std::cerr << "[Muninn] Batch transcription failed: " << e.what() << "\n";
    }

    return all_segments;
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

        // Initialize token IDs for word-level alignment
        pimpl_->initialize_token_ids();

        std::cout << "✓ Model loaded successfully\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

    } catch (const std::exception& e) {
        std::cerr << "✗ Failed to load Whisper model: " << e.what() << "\n";
        std::cerr << "═══════════════════════════════════════════════════════════\n";
        pimpl_->model_loaded = false;
        throw;
    }
}

// Constructor using ModelOptions struct
Transcriber::Transcriber(const ModelOptions& options)
    : Transcriber(options.model_path, options.device_string(), options.compute_type_string())
{
    // Additional configuration from ModelOptions can be applied here
    // Threading options would be passed to CTranslate2 if supported
}

Transcriber::~Transcriber() = default;

Transcriber::Transcriber(Transcriber&&) noexcept = default;
Transcriber& Transcriber::operator=(Transcriber&&) noexcept = default;

// Static method to get audio info without loading model
AudioInfo Transcriber::get_audio_info(const std::string& audio_path) {
    AudioInfo info{};

#ifdef WITH_HEIMDALL
    AudioExtractor extractor;
    if (!extractor.open(audio_path)) {
        throw std::runtime_error("Failed to open audio file: " + extractor.get_last_error());
    }

    info.duration = extractor.get_duration();
    info.sample_rate = 16000;  // Muninn always resamples to 16kHz
    info.num_tracks = extractor.get_track_count();

    extractor.close();
#else
    throw std::runtime_error("Audio info requires Heimdall library");
#endif

    return info;
}

TranscribeResult Transcriber::transcribe(
    const std::vector<float>& audio_samples,
    int sample_rate,
    const TranscribeOptions& options,
    int track_id,
    int total_tracks
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

        float original_duration = audio_samples.size() / 16000.0f;
        std::cout << "[Muninn] Audio: " << audio_samples.size() << " samples, duration: "
                  << original_duration << "s\n";

        // Apply clip timestamps if specified
        std::vector<float> clipped_samples;
        float clip_offset = 0.0f;  // Time offset to add back to results
        if (options.clip_start > 0.0f || (options.clip_end >= 0.0f && options.clip_end < original_duration)) {
            float clip_start = std::max(0.0f, options.clip_start);
            float clip_end = (options.clip_end < 0.0f) ? original_duration : std::min(original_duration, options.clip_end);

            if (clip_start < clip_end) {
                size_t start_sample = static_cast<size_t>(clip_start * 16000.0f);
                size_t end_sample = static_cast<size_t>(clip_end * 16000.0f);
                end_sample = std::min(end_sample, audio_samples.size());

                clipped_samples.assign(audio_samples.begin() + start_sample, audio_samples.begin() + end_sample);
                clip_offset = clip_start;

                std::cout << "[Muninn] Clipping audio: " << clip_start << "s - " << clip_end << "s ("
                          << clipped_samples.size() << " samples)\n";
            } else {
                clipped_samples = audio_samples;
            }
        } else {
            clipped_samples = audio_samples;
        }

        // Update original_duration for clipped audio
        original_duration = clipped_samples.size() / 16000.0f;

        // Apply VAD filtering based on selected type
        std::vector<float> processed_samples;
        std::vector<SpeechSegment> speech_segments;

        bool apply_vad = options.vad_filter && options.vad_type != VADType::None;

        if (apply_vad) {
            // Auto-detect VAD type if requested
            VADType effective_vad_type = options.vad_type;
            if (options.vad_type == VADType::Auto) {
                effective_vad_type = auto_detect_vad_type(clipped_samples, track_id, total_tracks);
            }

            switch (effective_vad_type) {
                case VADType::Auto:
                    // Should not reach here (already resolved above)
                    effective_vad_type = VADType::Energy;
                    [[fallthrough]];

                case VADType::Silero: {
                    std::cout << "[Muninn] Applying Silero VAD filter...\n";

                    if (!is_silero_vad_available()) {
                        std::cerr << "[Muninn] Silero VAD not available, falling back to Energy VAD\n";
                        goto energy_vad;
                    }

                    if (options.silero_model_path.empty()) {
                        std::cerr << "[Muninn] Silero model path not specified, falling back to Energy VAD\n";
                        goto energy_vad;
                    }

                    try {
                        SileroVADOptions silero_opts;
                        silero_opts.model_path = options.silero_model_path;
                        // Silero VAD threshold (0.25 is lenient, 0.5 is aggressive)
                        silero_opts.threshold = options.vad_threshold > 0.1f ? options.vad_threshold : 0.25f;
                        silero_opts.min_speech_duration_ms = options.vad_min_speech_duration_ms;
                        silero_opts.min_silence_duration_ms = options.vad_min_silence_duration_ms > 200 ? 100 : options.vad_min_silence_duration_ms;
                        silero_opts.speech_pad_ms = options.vad_speech_pad_ms;
                        silero_opts.max_speech_duration_s = options.vad_max_speech_duration_s;

                        SileroVAD silero(silero_opts);
                        processed_samples = silero.filter_silence(clipped_samples, 16000, speech_segments);

                        if (processed_samples.empty()) {
                            std::cout << "[Muninn] No speech detected (Silero VAD)\n";
                            result.duration = original_duration;
                            result.language = options.language;
                            return result;
                        }

                        std::cout << "[Muninn] Silero VAD: " << speech_segments.size() << " speech segments, "
                                  << (processed_samples.size() / 16000.0f) << "s of speech\n";
                    } catch (const std::exception& e) {
                        std::cerr << "[Muninn] Silero VAD failed: " << e.what() << ", falling back to Energy VAD\n";
                        goto energy_vad;
                    }
                    break;
                }

                case VADType::WebRTC:
                    std::cerr << "[Muninn] WebRTC VAD not yet implemented, falling back to Energy VAD\n";
                    // Fall through to Energy VAD
                    [[fallthrough]];

                case VADType::Energy:
                energy_vad: {
                    std::cout << "[Muninn] Applying Energy VAD filter...\n";

                    VADOptions vad_opts;
                    vad_opts.threshold = options.vad_threshold;
                    vad_opts.min_speech_duration_ms = options.vad_min_speech_duration_ms;
                    vad_opts.min_silence_duration_ms = options.vad_min_silence_duration_ms;
                    vad_opts.speech_pad_ms = options.vad_speech_pad_ms;

                    VAD vad(vad_opts);
                    processed_samples = vad.filter_silence(clipped_samples, 16000, speech_segments);

                    if (processed_samples.empty()) {
                        std::cout << "[Muninn] No speech detected (Energy VAD)\n";
                        result.duration = original_duration;
                        result.language = options.language;
                        return result;
                    }

                    std::cout << "[Muninn] Energy VAD: " << speech_segments.size() << " speech segments, "
                              << (processed_samples.size() / 16000.0f) << "s of speech\n";
                    break;
                }

                case VADType::None:
                default:
                    processed_samples = clipped_samples;
                    break;
            }
        } else {
            processed_samples = clipped_samples;
        }

        // Convert to mel-spectrogram
        std::cout << "[Muninn] Converting to mel-spectrogram\n";
        auto mel_features = pimpl_->compute_mel(processed_samples);

        int n_frames = mel_features.size();
        std::cout << "[Muninn] Mel-spectrogram: " << n_frames << " frames x "
                  << pimpl_->mel_converter.getMelBins() << " mels\n";

        result.duration = original_duration;  // Report original duration, not filtered

        // Create mutable copy of options for language detection
        TranscribeOptions effective_options = options;

        // Perform language detection if requested
        if (options.language == "auto" && pimpl_->model->is_multilingual()) {
            std::cout << "[Muninn] Detecting language from audio...\n";

            // Use first 30s of mel features for language detection
            int detect_frames = std::min(n_frames, 3000);  // 30 seconds
            std::vector<std::vector<float>> detect_mel(
                mel_features.begin(),
                mel_features.begin() + detect_frames
            );

            try {
                auto [detected_lang, lang_prob] = pimpl_->detect_language(detect_mel);
                effective_options.language = detected_lang;
                result.language = detected_lang;
                result.language_probability = lang_prob;
            } catch (const std::exception& e) {
                std::cerr << "[Muninn] Language detection failed: " << e.what()
                          << ", defaulting to English\n";
                effective_options.language = "en";
                result.language = "en";
                result.language_probability = 0.0f;
            }
        } else {
            result.language = options.language;
            result.language_probability = 1.0f;
        }

        // Track repeated segments across chunks to detect hallucinations like "Thank you" repeated
        std::map<std::string, int> segment_text_counts;

        // Whisper CTranslate2 has a maximum input length of 3000 frames (30 seconds)
        constexpr int MAX_FRAMES = 3000;
        constexpr int BATCH_SIZE = 4;  // Process 4 chunks in parallel on GPU

        if (n_frames > MAX_FRAMES) {
            std::cout << "[Muninn] Audio too long (" << n_frames << " frames), splitting into chunks of "
                      << MAX_FRAMES << " frames\n";

            // Calculate number of chunks needed
            int num_chunks = (n_frames + MAX_FRAMES - 1) / MAX_FRAMES;
            std::cout << "[Muninn] Processing " << num_chunks << " chunk(s) with batch size " << BATCH_SIZE << "\n";

            // Prepare all chunks upfront
            std::vector<std::vector<std::vector<float>>> all_chunk_features;
            std::vector<float> all_chunk_start_times;

            for (int chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
                int start_frame = chunk_idx * MAX_FRAMES;
                int end_frame = std::min(start_frame + MAX_FRAMES, n_frames);
                float chunk_start_time = start_frame * 0.01f;

                all_chunk_features.emplace_back(
                    mel_features.begin() + start_frame,
                    mel_features.begin() + end_frame
                );
                all_chunk_start_times.push_back(chunk_start_time);
            }

            // Process in batches
            for (int batch_start = 0; batch_start < num_chunks; batch_start += BATCH_SIZE) {
                int batch_end = std::min(batch_start + BATCH_SIZE, num_chunks);
                int current_batch_size = batch_end - batch_start;

                std::cout << "[Muninn] Processing batch " << (batch_start / BATCH_SIZE + 1)
                          << "/" << ((num_chunks + BATCH_SIZE - 1) / BATCH_SIZE)
                          << " (chunks " << (batch_start + 1) << "-" << batch_end << ")\n";

                // Extract batch
                std::vector<std::vector<std::vector<float>>> batch_features(
                    all_chunk_features.begin() + batch_start,
                    all_chunk_features.begin() + batch_end
                );
                std::vector<float> batch_start_times(
                    all_chunk_start_times.begin() + batch_start,
                    all_chunk_start_times.begin() + batch_end
                );

                // Batch transcribe
                auto batch_results = pimpl_->transcribe_batch(batch_features, batch_start_times, effective_options);

                // Process results and filter hallucinations
                for (int i = 0; i < current_batch_size; ++i) {
                    for (auto& seg : batch_results[i]) {
                        std::string normalized = seg.text;
                        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

                        segment_text_counts[normalized]++;

                        if (segment_text_counts[normalized] >= 3) {
                            std::cerr << "[Muninn] Skipping cross-chunk hallucination (appears "
                                      << segment_text_counts[normalized] << " times): '" << seg.text << "'\n";
                            continue;
                        }

                        result.segments.push_back(seg);
                    }
                }
            }

            std::cout << "[Muninn] Completed batched transcription: " << result.segments.size() << " total segments\n";

            // Remap timestamps from filtered audio back to original timeline if VAD was applied
            if (!speech_segments.empty()) {
                std::cout << "[Muninn] Remapping timestamps to original timeline...\n";
                for (auto& seg : result.segments) {
                    float orig_start = remap_timestamp_to_original(seg.start, speech_segments);
                    float orig_end = remap_timestamp_to_original(seg.end, speech_segments);
                    seg.start = orig_start;
                    seg.end = orig_end;
                    // Also remap word timestamps to original timeline
                    for (auto& word : seg.words) {
                        word.start = remap_timestamp_to_original(word.start, speech_segments);
                        word.end = remap_timestamp_to_original(word.end, speech_segments);
                    }
                }

                // Filter segments in silent regions (hallucination silence threshold)
                filter_silence_hallucinations(result.segments, speech_segments,
                                              options.hallucination_silence_threshold);
            }

        } else {
            // Single chunk processing (audio <= 30 seconds)
            std::cout << "[Muninn] Audio short enough for single-pass transcription\n";

            // Use initial prompt as previous text for context conditioning
            std::string prev_text = effective_options.initial_prompt;
            result.segments = pimpl_->transcribe_chunk(mel_features, 0.0f, effective_options, prev_text);

            // Remap timestamps from filtered audio back to original timeline if VAD was applied
            if (!speech_segments.empty()) {
                std::cout << "[Muninn] Remapping timestamps to original timeline...\n";
                for (auto& seg : result.segments) {
                    float orig_start = remap_timestamp_to_original(seg.start, speech_segments);
                    float orig_end = remap_timestamp_to_original(seg.end, speech_segments);
                    seg.start = orig_start;
                    seg.end = orig_end;
                    // Also remap word timestamps to original timeline
                    for (auto& word : seg.words) {
                        word.start = remap_timestamp_to_original(word.start, speech_segments);
                        word.end = remap_timestamp_to_original(word.end, speech_segments);
                    }
                }

                // Filter segments in silent regions (hallucination silence threshold)
                filter_silence_hallucinations(result.segments, speech_segments,
                                              options.hallucination_silence_threshold);
            }
        }

        // Add clip_offset to all segment timestamps to reflect original timeline
        if (clip_offset > 0.0f) {
            std::cout << "[Muninn] Adjusting timestamps by clip offset: +" << clip_offset << "s\n";
            for (auto& seg : result.segments) {
                seg.start += clip_offset;
                seg.end += clip_offset;
                // Also adjust word timestamps if present
                for (auto& word : seg.words) {
                    word.start += clip_offset;
                    word.end += clip_offset;
                }
            }
        }

        // Language is already set above during language detection or from options

    } catch (const std::exception& e) {
        std::cerr << "[Muninn] Transcription failed: " << e.what() << "\n";
        throw;
    }

    return result;
}

TranscribeResult Transcriber::transcribe(
    const std::string& audio_path,
    const TranscribeOptions& options,
    ProgressCallback progress_callback
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
        // Check if track should be skipped (user-specified)
        if (options.skip_tracks.count(track) > 0) {
            std::cout << "[Muninn] Skipping Track " << track << " (user-specified)\n";
            continue;
        }

        std::cout << "\n═══════════════════════════════════════════════════════════\n";
        std::cout << "[Muninn] Processing Track " << track << "/" << track_count << "\n";
        std::cout << "═══════════════════════════════════════════════════════════\n";

        // Report progress to GUI
        if (progress_callback) {
            bool should_continue = progress_callback(
                track, track_count, 0.0f,
                "Processing track " + std::to_string(track + 1) + "/" + std::to_string(track_count)
            );
            if (!should_continue) {
                std::cout << "[Muninn] Transcription cancelled by user\n";
                break;
            }
        }

        std::vector<float> samples;
        if (!extractor.extract_track(track, samples)) {
            std::cerr << "[Muninn] WARNING: Failed to extract track " << track << ": "
                      << extractor.get_last_error() << "\n";
            continue;
        }

        std::cout << "[Muninn] Track " << track << ": " << samples.size() << " samples\n";

        // Transcribe this track
        try {
            auto track_result = transcribe(samples, 16000, options, track, track_count);

            // Set track_id for each segment (GUI handles formatting with track names)
            for (auto& segment : track_result.segments) {
                segment.track_id = track;
            }

            // Merge into combined result
            combined_result.segments.insert(combined_result.segments.end(),
                track_result.segments.begin(), track_result.segments.end());

            std::cout << "[Muninn] Track " << track << ": " << track_result.segments.size() << " segment(s)\n";

            // Report track completion
            if (progress_callback) {
                progress_callback(
                    track, track_count, 1.0f,
                    "Completed track " + std::to_string(track + 1) + "/" + std::to_string(track_count)
                );
            }

        } catch (const std::exception& e) {
            std::cerr << "[Muninn] WARNING: Track " << track << " transcription failed: " << e.what() << "\n";
        }
    }

    extractor.close();

    std::cout << "\n═══════════════════════════════════════════════════════════\n";
    std::cout << "[Muninn] All tracks complete. Total segments: " << combined_result.segments.size() << "\n";
    std::cout << "═══════════════════════════════════════════════════════════\n";

    // ═══════════════════════════════════════════════════════════
    // Speaker Diarization (if enabled)
    // ═══════════════════════════════════════════════════════════
    if (options.enable_diarization && !options.diarization_model_path.empty()) {
        try {
            std::cout << "\n[Muninn] Running speaker diarization...\n";

            // Initialize diarizer
            DiarizationOptions diar_opts;
            diar_opts.embedding_model_path = options.diarization_model_path;
            diar_opts.clustering_threshold = options.diarization_threshold;
            diar_opts.min_speakers = options.diarization_min_speakers;
            diar_opts.max_speakers = options.diarization_max_speakers;

            Diarizer diarizer(options.diarization_model_path, diar_opts);

            // Re-open audio file for diarization
            AudioExtractor diar_extractor;
            if (!diar_extractor.open(audio_path)) {
                throw std::runtime_error("Failed to open audio for diarization: " + diar_extractor.get_last_error());
            }

            // Run diarization on each track that has segments
            std::map<int, DiarizationResult> track_diarization;
            for (int track = 0; track < track_count; ++track) {
                // Check if this track has any segments
                bool has_segments = false;
                for (const auto& seg : combined_result.segments) {
                    if (seg.track_id == track) {
                        has_segments = true;
                        break;
                    }
                }

                if (!has_segments) continue;

                std::cout << "[Diarization] Processing Track " << track << "...\n";

                // Extract audio for this track
                std::vector<float> track_audio;
                if (!diar_extractor.extract_track(track, track_audio)) {
                    std::cerr << "[Diarization] WARNING: Failed to extract track " << track << "\n";
                    continue;
                }

                // Run diarization
                auto diar_result = diarizer.diarize(track_audio.data(), track_audio.size(), 16000);

                std::cout << "[Diarization] Track " << track << ": Detected "
                          << diar_result.num_speakers << " speaker(s)\n";

                track_diarization[track] = diar_result;
            }

            diar_extractor.close();

            // Assign speakers to all segments
            for (auto& segment : combined_result.segments) {
                if (track_diarization.count(segment.track_id) > 0) {
                    const auto& diar_result = track_diarization[segment.track_id];

                    // Find speaker at segment midpoint
                    float midpoint = (segment.start + segment.end) / 2.0f;
                    int speaker_id = Diarizer::get_speaker_at_time(diar_result, midpoint);

                    if (speaker_id >= 0) {
                        segment.speaker_id = speaker_id;
                        segment.speaker_label = "Speaker " + std::to_string(speaker_id);

                        // Calculate confidence based on segment overlap
                        segment.speaker_confidence = 0.8f;  // Default confidence
                    }
                }
            }

            std::cout << "[Muninn] ✓ Speaker diarization complete\n";

        } catch (const std::exception& e) {
            std::cerr << "[Muninn] Warning: Diarization failed: " << e.what() << "\n";
            std::cerr << "[Muninn] Continuing without speaker labels...\n";
        }
    }

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
