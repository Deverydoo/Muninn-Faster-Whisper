#include "muninn/mel_spectrogram.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace muninn {

MelSpectrogram::MelSpectrogram(int sample_rate, int n_fft, int n_mels, int hop_length)
    : sample_rate_(sample_rate)
    , n_fft_(n_fft)
    , n_mels_(n_mels)
    , hop_length_(hop_length)
{
    // Create Hann window for STFT
    hann_window_ = createHannWindow(n_fft);

    // Create mel filterbank
    mel_filters_ = createMelFilters(sample_rate, n_fft, n_mels);
}

std::vector<float> MelSpectrogram::createHannWindow(int size)
{
    std::vector<float> window(size);
    for (int i = 0; i < size; i++) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (size - 1)));
    }
    return window;
}

float MelSpectrogram::hzToMel(float hz)
{
    // Whisper uses HTK formula
    float f_min = 0.0f;
    float f_sp = 200.0f / 3.0f;
    float min_log_hz = 1000.0f;
    float min_log_mel = (min_log_hz - f_min) / f_sp;
    float logstep = std::log(6.4f) / 27.0f;

    if (hz >= min_log_hz) {
        return min_log_mel + std::log(hz / min_log_hz) / logstep;
    } else {
        return (hz - f_min) / f_sp;
    }
}

float MelSpectrogram::melToHz(float mel)
{
    // Inverse of hzToMel
    float f_min = 0.0f;
    float f_sp = 200.0f / 3.0f;
    float min_log_hz = 1000.0f;
    float min_log_mel = (min_log_hz - f_min) / f_sp;
    float logstep = std::log(6.4f) / 27.0f;

    if (mel >= min_log_mel) {
        return min_log_hz * std::exp(logstep * (mel - min_log_mel));
    } else {
        return f_min + f_sp * mel;
    }
}

std::vector<std::vector<float>> MelSpectrogram::createMelFilters(int sample_rate, int n_fft, int n_mels)
{
    int n_freqs = n_fft / 2 + 1;
    std::vector<std::vector<float>> filters(n_mels, std::vector<float>(n_freqs, 0.0f));

    // Create frequency bins
    std::vector<float> fft_freqs(n_freqs);
    for (int i = 0; i < n_freqs; i++) {
        fft_freqs[i] = i * sample_rate / static_cast<float>(n_fft);
    }

    // Create mel-spaced frequencies
    float min_mel = 0.0f;
    float max_mel = hzToMel(sample_rate / 2.0f);

    std::vector<float> mel_points(n_mels + 2);
    for (int i = 0; i < n_mels + 2; i++) {
        mel_points[i] = min_mel + (max_mel - min_mel) * i / (n_mels + 1);
    }

    std::vector<float> mel_freqs(n_mels + 2);
    for (int i = 0; i < n_mels + 2; i++) {
        mel_freqs[i] = melToHz(mel_points[i]);
    }

    // Create triangular filters
    for (int m = 0; m < n_mels; m++) {
        float left = mel_freqs[m];
        float center = mel_freqs[m + 1];
        float right = mel_freqs[m + 2];

        for (int f = 0; f < n_freqs; f++) {
            float freq = fft_freqs[f];

            if (freq >= left && freq <= center) {
                filters[m][f] = (freq - left) / (center - left);
            } else if (freq > center && freq <= right) {
                filters[m][f] = (right - freq) / (right - center);
            }
        }
    }

    return filters;
}

void MelSpectrogram::computeSTFT(const std::vector<float>& samples,
                                 std::vector<std::vector<std::complex<float>>>& stft_output)
{
    int n_frames = (samples.size() - n_fft_) / hop_length_ + 1;
    int n_freqs = n_fft_ / 2 + 1;

    stft_output.resize(n_frames, std::vector<std::complex<float>>(n_freqs));

    // Simple DFT (not optimized FFT, but sufficient for proof-of-concept)
    // TODO: Replace with proper FFT library (e.g., FFTW, KissFFT) for production
    for (int frame = 0; frame < n_frames; frame++) {
        int offset = frame * hop_length_;

        // Apply window and compute DFT
        for (int k = 0; k < n_freqs; k++) {
            std::complex<float> sum(0.0f, 0.0f);

            for (int n = 0; n < n_fft_; n++) {
                if (offset + n < static_cast<int>(samples.size())) {
                    float windowed_sample = samples[offset + n] * hann_window_[n];
                    float angle = -2.0f * M_PI * k * n / n_fft_;
                    sum += windowed_sample * std::complex<float>(std::cos(angle), std::sin(angle));
                }
            }

            stft_output[frame][k] = sum;
        }
    }
}

int MelSpectrogram::compute(const std::vector<float>& samples, std::vector<std::vector<float>>& mel_output)
{
    // Compute STFT
    std::vector<std::vector<std::complex<float>>> stft;
    computeSTFT(samples, stft);

    int n_frames = stft.size();
    if (n_frames == 0) {
        return 0;
    }

    int n_freqs = stft[0].size();

    // Compute magnitude spectrogram
    std::vector<std::vector<float>> magnitude(n_frames, std::vector<float>(n_freqs));
    for (int frame = 0; frame < n_frames; frame++) {
        for (int freq = 0; freq < n_freqs; freq++) {
            float mag = std::abs(stft[frame][freq]);
            magnitude[frame][freq] = mag * mag;  // Power spectrum
        }
    }

    // Apply mel filterbank
    mel_output.resize(n_frames, std::vector<float>(n_mels_));

    for (int frame = 0; frame < n_frames; frame++) {
        for (int mel = 0; mel < n_mels_; mel++) {
            float mel_value = 0.0f;

            for (int freq = 0; freq < n_freqs; freq++) {
                mel_value += mel_filters_[mel][freq] * magnitude[frame][freq];
            }

            // Log compression (Whisper-style)
            mel_value = std::max(mel_value, 1e-10f);
            float log_mel = std::log10(mel_value);
            float max_log = std::log10(1.0f);  // Simplified normalization
            log_mel = std::max(log_mel, max_log - 8.0f);
            log_mel = (log_mel + 4.0f) / 4.0f;

            mel_output[frame][mel] = log_mel;
        }
    }

    return n_frames;
}

} // namespace muninn
