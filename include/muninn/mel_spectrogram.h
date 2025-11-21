#pragma once

#include <vector>
#include <complex>

namespace muninn {

/**
 * @brief Whisper-compatible mel-spectrogram converter
 *
 * Converts audio samples to mel-filterbank features for Whisper models.
 * Implements the same mel-spectrogram generation as OpenAI Whisper / faster-whisper.
 *
 * Parameters match Whisper's defaults:
 * - 128 mel bins (large-v3/large-v3-turbo models)
 * - 16kHz sample rate
 * - 400-point FFT (25ms @ 16kHz)
 * - 160-sample hop (10ms @ 16kHz)
 */
class MelSpectrogram {
public:
    /**
     * @brief Construct mel-spectrogram converter
     *
     * @param sample_rate Audio sample rate (default: 16000 Hz)
     * @param n_fft FFT window size (default: 400)
     * @param n_mels Number of mel bins (default: 128)
     * @param hop_length Hop size between frames (default: 160)
     */
    MelSpectrogram(int sample_rate = 16000,
                   int n_fft = 400,
                   int n_mels = 128,
                   int hop_length = 160);

    /**
     * @brief Convert audio samples to mel-spectrogram
     *
     * @param samples Audio samples (mono, float32, normalized to [-1, 1])
     * @param mel_output Output mel-spectrogram (n_frames x n_mels)
     * @return Number of frames generated
     */
    int compute(const std::vector<float>& samples,
                std::vector<std::vector<float>>& mel_output);

    /**
     * @brief Get number of mel bins
     */
    int getMelBins() const { return n_mels_; }

private:
    // Compute Short-Time Fourier Transform
    void computeSTFT(const std::vector<float>& samples,
                     std::vector<std::vector<std::complex<float>>>& stft_output);

    // Create Hann window
    std::vector<float> createHannWindow(int size);

    // Create mel filterbank matrix
    std::vector<std::vector<float>> createMelFilters(int sample_rate, int n_fft, int n_mels);

    // Convert frequency to mel scale (HTK formula for Whisper compatibility)
    float hzToMel(float hz);

    // Convert mel scale to frequency
    float melToHz(float mel);

    int sample_rate_;
    int n_fft_;
    int n_mels_;
    int hop_length_;
    std::vector<float> hann_window_;
    std::vector<std::vector<float>> mel_filters_;
};

} // namespace muninn
