#pragma once

#include <vector>
#include <immintrin.h>  // AVX2/SSE intrinsics

namespace heimdall {

/**
 * PeakDetector - Heimdall's SIMD-Accelerated Senses
 *
 * Uses AVX2 SIMD instructions to process 8 audio samples simultaneously,
 * detecting peaks with the speed and precision of Heimdall's legendary hearing.
 */
class PeakDetector {
public:
    PeakDetector();
    ~PeakDetector();

    /**
     * Compute min/max peaks for waveform visualization
     *
     * Processes audio samples with SIMD acceleration, extracting peak values
     * suitable for drawing waveforms.
     *
     * @param samples Raw audio samples (mono float)
     * @param sample_count Total number of samples
     * @param width Output width in pixels
     * @param normalize Normalize to 0.0-1.0 range
     * @return Vector of peak values (width * 2 for min/max pairs)
     */
    std::vector<float> compute_peaks(
        const float* samples,
        int sample_count,
        int width,
        bool normalize = true
    );

    /**
     * Check if AVX2 is available on this CPU
     */
    static bool is_avx2_available();

private:
    /**
     * Compute peaks using AVX2 (8 samples at once)
     */
    void compute_peaks_avx2(
        const float* samples,
        int sample_count,
        int samples_per_pixel,
        std::vector<float>& peaks
    );

    /**
     * Compute peaks using scalar fallback
     */
    void compute_peaks_scalar(
        const float* samples,
        int sample_count,
        int samples_per_pixel,
        std::vector<float>& peaks
    );

    /**
     * Normalize peaks to 0.0-1.0 range
     */
    void normalize_peaks(std::vector<float>& peaks);

    bool use_avx2_;
};

} // namespace heimdall
