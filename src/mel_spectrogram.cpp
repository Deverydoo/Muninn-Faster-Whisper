#include "muninn/mel_spectrogram.h"
#include "muninn/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

extern "C" {
#include <libavutil/tx.h>
#include <libavutil/mem.h>
}

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
    buildSparseMelFilters();
}

std::vector<float> MelSpectrogram::createHannWindow(int size)
{
    std::vector<float> window(size);
    // Prevent division by zero if size <= 1
    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }
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

void MelSpectrogram::buildSparseMelFilters()
{
    const int n_freqs = n_fft_ / 2 + 1;
    mel_bin_start_.assign(n_mels_, 0);
    mel_bin_weights_.assign(n_mels_, {});

    for (int m = 0; m < n_mels_; m++) {
        const std::vector<float>& row = mel_filters_[m];

        int first = n_freqs;
        int last = -1;
        for (int f = 0; f < n_freqs; f++) {
            if (row[f] != 0.0f) {
                if (first == n_freqs) first = f;
                last = f;
            }
        }

        if (last < first) continue;  // empty band (can happen at very low n_mels)

        mel_bin_start_[m] = first;
        mel_bin_weights_[m].assign(row.begin() + first, row.begin() + last + 1);
    }
}

void MelSpectrogram::dftFrame(const float* windowed, std::complex<float>* out) const
{
    const int n_freqs = n_fft_ / 2 + 1;

    for (int k = 0; k < n_freqs; k++) {
        float re = 0.0f;
        float im = 0.0f;
        for (int n = 0; n < n_fft_; n++) {
            const float angle = -2.0f * static_cast<float>(M_PI) * k * n / n_fft_;
            re += windowed[n] * std::cos(angle);
            im += windowed[n] * std::sin(angle);
        }
        out[k] = std::complex<float>(re, im);
    }
}

int MelSpectrogram::compute(const std::vector<float>& samples, std::vector<std::vector<float>>& mel_output)
{
    const int n_freqs = n_fft_ / 2 + 1;

    // samples.size() is unsigned: a clip shorter than one window used to wrap
    // around to a nonsense frame count instead of producing nothing.
    if (n_fft_ <= 0 || hop_length_ <= 0 || samples.size() < static_cast<size_t>(n_fft_)) {
        mel_output.clear();
        return 0;
    }

    const int n_frames = static_cast<int>((samples.size() - n_fft_) / hop_length_) + 1;

    // FFmpeg's transform, already linked for audio decode. RDFT needs a length
    // divisible by 4 and Whisper's 400-point window qualifies (400 = 4 x 100).
    // Built per call rather than cached so copies of this object never share a
    // context: it holds scratch state and is not reentrant.
    AVTXContext* tx_ctx = nullptr;
    av_tx_fn tx_fn = nullptr;
    float tx_scale = 1.0f;  // match the unnormalised reference DFT
    int tx_err = av_tx_init(&tx_ctx, &tx_fn, AV_TX_FLOAT_RDFT, 0, n_fft_, &tx_scale, 0);
    bool use_fft = (tx_err >= 0 && tx_ctx && tx_fn);

    // av_tx wants its own aligned buffers; the output holds n_fft_/2+1 complex
    // values but we over-allocate to n_fft_ so no codelet can run off the end.
    float* in_buf = static_cast<float*>(av_malloc(sizeof(float) * n_fft_));
    AVComplexFloat* out_buf = static_cast<AVComplexFloat*>(av_malloc(sizeof(AVComplexFloat) * n_fft_));
    if (!in_buf || !out_buf) {
        av_free(in_buf);
        av_free(out_buf);
        if (tx_ctx) av_tx_uninit(&tx_ctx);
        mel_output.clear();
        return 0;
    }

    // Prove the fast path agrees with the reference before trusting it. This
    // catches a wrong sign convention or output packing on any FFmpeg build,
    // and costs one frame's worth of naive DFT (~80k ops) per call.
    if (use_fft) {
        for (int n = 0; n < n_fft_; n++) {
            in_buf[n] = std::sin(0.017f * n) + 0.5f * std::cos(0.11f * n + 0.3f);
        }
        std::vector<std::complex<float>> reference(n_freqs);
        std::vector<float> probe(in_buf, in_buf + n_fft_);
        dftFrame(probe.data(), reference.data());

        tx_fn(tx_ctx, out_buf, in_buf, sizeof(float));

        float max_delta = 0.0f;
        for (int k = 0; k < n_freqs; k++) {
            max_delta = std::max(max_delta, std::abs(out_buf[k].re - reference[k].real()));
            max_delta = std::max(max_delta, std::abs(out_buf[k].im - reference[k].imag()));
        }

        // Tolerance is relative to the transform's own magnitude scale: a
        // 400-point DFT of a unit-amplitude signal has peaks in the hundreds,
        // so float32 rounding alone accounts for a few thousandths.
        if (max_delta > 0.05f) {
            Logger::warn("mel-spectrogram: FFT self-check failed (max delta " +
                         std::to_string(max_delta) + "), falling back to reference DFT");
            use_fft = false;
        }
    } else {
        Logger::warn("mel-spectrogram: av_tx_init failed for n_fft=" + std::to_string(n_fft_) +
                     ", falling back to reference DFT");
    }

    // Fused STFT -> power -> mel: one frame at a time. Materialising the full
    // complex spectrogram first cost ~1.5 KB per frame (around 380 MB on a
    // 40-minute track) for data that is consumed immediately.
    mel_output.assign(n_frames, std::vector<float>(n_mels_));
    std::vector<float> power(n_freqs);
    std::vector<std::complex<float>> fallback(n_freqs);

    for (int frame = 0; frame < n_frames; frame++) {
        const float* src = samples.data() + static_cast<size_t>(frame) * hop_length_;
        for (int n = 0; n < n_fft_; n++) {
            in_buf[n] = src[n] * hann_window_[n];
        }

        if (use_fft) {
            tx_fn(tx_ctx, out_buf, in_buf, sizeof(float));
            for (int k = 0; k < n_freqs; k++) {
                power[k] = out_buf[k].re * out_buf[k].re + out_buf[k].im * out_buf[k].im;
            }
        } else {
            dftFrame(in_buf, fallback.data());
            for (int k = 0; k < n_freqs; k++) {
                power[k] = fallback[k].real() * fallback[k].real() +
                           fallback[k].imag() * fallback[k].imag();
            }
        }

        std::vector<float>& row = mel_output[frame];
        for (int mel = 0; mel < n_mels_; mel++) {
            const std::vector<float>& weights = mel_bin_weights_[mel];
            const float* p = power.data() + mel_bin_start_[mel];

            float mel_value = 0.0f;
            for (size_t i = 0; i < weights.size(); i++) {
                mel_value += weights[i] * p[i];
            }

            // Log compression (Whisper-style)
            mel_value = std::max(mel_value, 1e-10f);
            float log_mel = std::log10(mel_value);
            float max_log = std::log10(1.0f);  // Simplified normalization
            log_mel = std::max(log_mel, max_log - 8.0f);
            log_mel = (log_mel + 4.0f) / 4.0f;

            row[mel] = log_mel;
        }
    }

    av_free(in_buf);
    av_free(out_buf);
    if (tx_ctx) av_tx_uninit(&tx_ctx);

    return n_frames;
}

} // namespace muninn
