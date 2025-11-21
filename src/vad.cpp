#include "muninn/vad.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>

namespace muninn {

VAD::VAD(const VADOptions& options)
    : options_(options)
{
}

float VAD::calculate_rms(const float* samples, int count) {
    if (count <= 0) return 0.0f;

    float sum_sq = 0.0f;
    for (int i = 0; i < count; ++i) {
        sum_sq += samples[i] * samples[i];
    }
    return std::sqrt(sum_sq / count);
}

float VAD::estimate_noise_floor(const std::vector<float>& energies) {
    if (energies.empty()) return options_.threshold;

    // Sort energies to find percentiles
    std::vector<float> sorted = energies;
    std::sort(sorted.begin(), sorted.end());

    // Get noise floor at low percentile (e.g., 10th percentile)
    size_t noise_idx = static_cast<size_t>(sorted.size() * options_.noise_floor_percentile);
    float noise_floor = sorted[std::min(noise_idx, sorted.size() - 1)];

    // Get speech level at high percentile (e.g., 90th percentile)
    size_t speech_idx = static_cast<size_t>(sorted.size() * 0.9);
    float speech_level = sorted[std::min(speech_idx, sorted.size() - 1)];

    // Calculate dynamic range
    float dynamic_range = speech_level - noise_floor;

    // Threshold is noise floor + fraction of dynamic range
    // This adapts to both quiet and loud audio
    float threshold = noise_floor + (dynamic_range * 0.25f);

    // Clamp relative to signal level, not absolute
    // Minimum: twice noise floor or user-specified threshold
    threshold = std::max(threshold, noise_floor * 2.0f);
    threshold = std::max(threshold, options_.threshold);

    // Maximum: halfway between noise and speech level (for very loud audio)
    float max_threshold = noise_floor + (dynamic_range * 0.5f);
    threshold = std::min(threshold, max_threshold);

    std::cout << "[VAD] Noise floor: " << noise_floor
              << ", Speech level: " << speech_level
              << ", Dynamic range: " << dynamic_range << "\n";

    return threshold;
}

std::vector<SpeechSegment> VAD::detect_speech(
    const std::vector<float>& samples,
    int sample_rate
) {
    std::vector<SpeechSegment> segments;

    if (samples.empty()) return segments;

    // Frame size: 32ms (512 samples at 16kHz)
    int frame_size = sample_rate * 32 / 1000;
    int hop_size = frame_size / 2;  // 50% overlap

    // Calculate energy for each frame
    std::vector<float> energies;
    std::vector<int> frame_starts;

    for (size_t i = 0; i + frame_size <= samples.size(); i += hop_size) {
        float rms = calculate_rms(&samples[i], frame_size);
        energies.push_back(rms);
        frame_starts.push_back(static_cast<int>(i));
    }

    if (energies.empty()) return segments;

    // Determine threshold
    float threshold = options_.threshold;
    if (options_.adaptive_threshold) {
        threshold = estimate_noise_floor(energies);
        std::cout << "[VAD] Adaptive threshold: " << threshold << "\n";
    }

    // Detect speech frames
    bool in_speech = false;
    int speech_start = 0;

    for (size_t i = 0; i < energies.size(); ++i) {
        bool is_speech = energies[i] > threshold;

        if (is_speech && !in_speech) {
            // Speech started
            in_speech = true;
            speech_start = frame_starts[i];
        } else if (!is_speech && in_speech) {
            // Speech ended
            in_speech = false;
            int speech_end = frame_starts[i] + frame_size;

            float start_sec = static_cast<float>(speech_start) / sample_rate;
            float end_sec = static_cast<float>(speech_end) / sample_rate;
            segments.emplace_back(start_sec, end_sec);
        }
    }

    // Handle case where speech continues to end
    if (in_speech) {
        float start_sec = static_cast<float>(speech_start) / sample_rate;
        float end_sec = static_cast<float>(samples.size()) / sample_rate;
        segments.emplace_back(start_sec, end_sec);
    }

    // Post-process: merge close segments, filter short ones
    return post_process_segments(segments, sample_rate);
}

std::vector<SpeechSegment> VAD::post_process_segments(
    const std::vector<SpeechSegment>& segments,
    int sample_rate
) {
    if (segments.empty()) return segments;

    float min_speech_sec = options_.min_speech_duration_ms / 1000.0f;
    float min_silence_sec = options_.min_silence_duration_ms / 1000.0f;
    float pad_sec = options_.speech_pad_ms / 1000.0f;

    std::vector<SpeechSegment> merged;
    SpeechSegment current = segments[0];

    // Merge segments that are close together
    for (size_t i = 1; i < segments.size(); ++i) {
        float gap = segments[i].start - current.end;

        if (gap < min_silence_sec) {
            // Merge with current segment
            current.end = segments[i].end;
        } else {
            // Save current and start new
            merged.push_back(current);
            current = segments[i];
        }
    }
    merged.push_back(current);

    // Filter short segments and add padding
    std::vector<SpeechSegment> result;
    for (auto& seg : merged) {
        float duration = seg.end - seg.start;
        if (duration >= min_speech_sec) {
            // Add padding
            seg.start = std::max(0.0f, seg.start - pad_sec);
            seg.end += pad_sec;
            result.push_back(seg);
        }
    }

    return result;
}

std::vector<float> VAD::filter_silence(
    const std::vector<float>& samples,
    int sample_rate,
    std::vector<SpeechSegment>& segments
) {
    // Detect speech segments
    segments = detect_speech(samples, sample_rate);

    if (segments.empty()) {
        // Check if this is truly silent audio (no signal at all)
        float max_sample = 0.0f;
        for (size_t i = 0; i < samples.size(); i += 100) {  // Sample every 100th
            max_sample = std::max(max_sample, std::abs(samples[i]));
        }

        if (max_sample < 0.001f) {
            // Truly silent - return empty to skip transcription
            std::cout << "[VAD] Track is silent (max amplitude: " << max_sample << ") - skipping\n";
            silence_removed_ = static_cast<float>(samples.size()) / sample_rate;
            return {};  // Empty = skip this track
        }

        std::cout << "[VAD] No speech detected - returning original audio\n";
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

        // Clamp to valid range
        start_sample = std::max(0, start_sample);
        end_sample = std::min(static_cast<int>(samples.size()), end_sample);

        // Copy samples
        filtered.insert(filtered.end(),
                       samples.begin() + start_sample,
                       samples.begin() + end_sample);

        speech_duration += seg.end - seg.start;
    }

    silence_removed_ = total_duration - speech_duration;

    std::cout << "[VAD] Detected " << segments.size() << " speech segment(s)\n";
    std::cout << "[VAD] Removed " << silence_removed_ << "s of silence ("
              << static_cast<int>(silence_removed_ / total_duration * 100) << "%)\n";

    return filtered;
}

} // namespace muninn
