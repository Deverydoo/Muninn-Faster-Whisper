#include "peak_detector.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace heimdall {

PeakDetector::PeakDetector() {
    use_avx2_ = is_avx2_available();
    if (use_avx2_) {
        std::cout << "[Heimdall] AVX2 acceleration enabled - processing 8 samples at once" << std::endl;
    } else {
        std::cout << "[Heimdall] Using scalar processing (AVX2 not available)" << std::endl;
    }
}

PeakDetector::~PeakDetector() {
}

bool PeakDetector::is_avx2_available() {
#ifdef _MSC_VER
    int cpu_info[4];
    __cpuid(cpu_info, 0);
    int n_ids = cpu_info[0];

    if (n_ids >= 7) {
        __cpuidex(cpu_info, 7, 0);
        return (cpu_info[1] & (1 << 5)) != 0;  // Check AVX2 bit
    }
#elif defined(__GNUC__)
    return __builtin_cpu_supports("avx2");
#endif
    return false;
}

std::vector<float> PeakDetector::compute_peaks(
    const float* samples,
    int sample_count,
    int width,
    bool normalize
) {
    if (sample_count == 0 || width == 0) {
        return std::vector<float>();
    }

    // Calculate samples per pixel
    int samples_per_pixel = std::max(1, sample_count / width);

    // Allocate output (2 values per pixel: min, max)
    std::vector<float> peaks;
    peaks.reserve(width * 2);

    // Choose SIMD or scalar path
    if (use_avx2_) {
        compute_peaks_avx2(samples, sample_count, samples_per_pixel, peaks);
    } else {
        compute_peaks_scalar(samples, sample_count, samples_per_pixel, peaks);
    }

    // Normalize if requested
    if (normalize) {
        normalize_peaks(peaks);
    }

    return peaks;
}

void PeakDetector::compute_peaks_avx2(
    const float* samples,
    int sample_count,
    int samples_per_pixel,
    std::vector<float>& peaks
) {
    int pixel = 0;
    int sample_idx = 0;

    while (sample_idx < sample_count) {
        // Initialize min/max for this pixel
        __m256 vmin = _mm256_set1_ps(FLT_MAX);
        __m256 vmax = _mm256_set1_ps(-FLT_MAX);

        // Process samples for this pixel
        int pixel_end = std::min(sample_idx + samples_per_pixel, sample_count);
        int i = sample_idx;

        // Process 8 samples at a time with AVX2
        for (; i + 8 <= pixel_end; i += 8) {
            __m256 v = _mm256_loadu_ps(&samples[i]);
            vmin = _mm256_min_ps(vmin, v);
            vmax = _mm256_max_ps(vmax, v);
        }

        // Horizontal min/max reduction
        float min_vals[8], max_vals[8];
        _mm256_storeu_ps(min_vals, vmin);
        _mm256_storeu_ps(max_vals, vmax);

        float min_val = min_vals[0];
        float max_val = max_vals[0];
        for (int j = 1; j < 8; j++) {
            min_val = std::min(min_val, min_vals[j]);
            max_val = std::max(max_val, max_vals[j]);
        }

        // Process remaining samples (< 8) with scalar
        for (; i < pixel_end; i++) {
            min_val = std::min(min_val, samples[i]);
            max_val = std::max(max_val, samples[i]);
        }

        // Store min/max pair
        peaks.push_back(min_val);
        peaks.push_back(max_val);

        sample_idx = pixel_end;
        pixel++;
    }
}

void PeakDetector::compute_peaks_scalar(
    const float* samples,
    int sample_count,
    int samples_per_pixel,
    std::vector<float>& peaks
) {
    int pixel = 0;
    int sample_idx = 0;

    while (sample_idx < sample_count) {
        float min_val = FLT_MAX;
        float max_val = -FLT_MAX;

        // Process samples for this pixel
        int pixel_end = std::min(sample_idx + samples_per_pixel, sample_count);
        for (int i = sample_idx; i < pixel_end; i++) {
            min_val = std::min(min_val, samples[i]);
            max_val = std::max(max_val, samples[i]);
        }

        // Store min/max pair
        peaks.push_back(min_val);
        peaks.push_back(max_val);

        sample_idx = pixel_end;
        pixel++;
    }
}

void PeakDetector::normalize_peaks(std::vector<float>& peaks) {
    if (peaks.empty()) return;

    // Find absolute maximum
    float abs_max = 0.0f;
    for (float val : peaks) {
        abs_max = std::max(abs_max, std::abs(val));
    }

    if (abs_max > 0.0f) {
        // Normalize to [-1.0, 1.0] range
        float scale = 1.0f / abs_max;
        for (float& val : peaks) {
            val *= scale;
        }
    }
}

} // namespace heimdall
