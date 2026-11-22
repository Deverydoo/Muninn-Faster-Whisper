// Checks for the parts of Muninn that need no Whisper model and no GPU:
// audio decoding, the mel spectrogram, energy VAD, and subtitle formatting.
// CI runs this (ctest -L cpu). Everything that needs a model is a separate
// test, registered only when a model is configured (see CMakeLists.txt).
//
// Usage: test_core            (exit code 0 = all checks passed)

#include "muninn/audio_extractor.h"
#include "muninn/mel_spectrogram.h"
#include "muninn/silero_vad.h"
#include "muninn/subtitle_export.h"
#include "muninn/vad.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                   \
    do {                                                                     \
        ++g_checks;                                                          \
        const std::string _a = (a), _b = (b);                                \
        if (_a != _b) {                                                      \
            ++g_failures;                                                    \
            std::printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,        \
                        __LINE__, _a.c_str(), _b.c_str());                   \
        }                                                                    \
    } while (0)

constexpr double kPi = 3.14159265358979323846;

// Silence, then a tone from start_s to end_s, then silence.
std::vector<float> tone_in_silence(int rate, float total_s, float start_s, float end_s,
                                   float amplitude = 0.3f, float freq = 440.0f) {
    std::vector<float> s(static_cast<size_t>(rate * total_s), 0.0f);
    for (size_t i = static_cast<size_t>(rate * start_s);
         i < static_cast<size_t>(rate * end_s) && i < s.size(); ++i) {
        s[i] = amplitude * static_cast<float>(std::sin(2.0 * kPi * freq * double(i) / rate));
    }
    return s;
}

// 16-bit PCM WAV, interleaved.
bool write_wav(const std::filesystem::path& path, int rate, int channels, float seconds) {
    const uint32_t frames = static_cast<uint32_t>(rate * seconds);
    const uint32_t data_bytes = frames * channels * 2;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + data_bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(static_cast<uint16_t>(channels));
    u32(rate); u32(rate * channels * 2); u16(static_cast<uint16_t>(channels * 2)); u16(16);
    f.write("data", 4); u32(data_bytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const double v = 0.5 * std::sin(2.0 * kPi * 440.0 * double(i) / rate);
        const int16_t sample = static_cast<int16_t>(v * 32767.0);
        for (int c = 0; c < channels; ++c) u16(static_cast<uint16_t>(sample));
    }
    return static_cast<bool>(f);
}

void test_subtitle_timestamps() {
    std::printf("[subtitle timestamps]\n");
    using muninn::SubtitleExporter;
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(0.0f), "00:00:00,000");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(61.25f), "00:01:01,250");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(3725.5f), "01:02:05,500");
    CHECK_EQ_STR(SubtitleExporter::format_vtt_timestamp(3725.5f), "01:02:05.500");

    muninn::SubtitleEntry e;
    e.index = 3;
    e.start = 1.5f;
    e.end = 4.0f;
    e.text = "Hello there";
    CHECK_EQ_STR(SubtitleExporter::format_srt_entry(e),
                 "3\n00:00:01,500 --> 00:00:04,000\nHello there\n");
}

void test_mel_spectrogram() {
    std::printf("[mel spectrogram]\n");
    muninn::MelSpectrogram mel;  // 16 kHz, n_fft 400, 128 bins, hop 160
    CHECK(mel.getMelBins() == 128);

    std::vector<std::vector<float>> out;
    const auto tone = tone_in_silence(16000, 1.0f, 0.0f, 1.0f);
    const int frames = mel.compute(tone, out);
    // (16000 - 400) / 160 + 1
    CHECK(frames == 98);
    CHECK(out.size() == static_cast<size_t>(frames));

    bool all_finite = true, rows_ok = true;
    double tone_sum = 0.0;
    for (const auto& row : out) {
        if (row.size() != 128) rows_ok = false;
        for (float v : row) {
            if (!std::isfinite(v)) all_finite = false;
            tone_sum += v;
        }
    }
    CHECK(rows_ok);
    CHECK(all_finite);

    std::vector<std::vector<float>> quiet_out;
    const std::vector<float> quiet(16000, 0.0f);
    CHECK(mel.compute(quiet, quiet_out) == 98);
    double quiet_sum = 0.0;
    for (const auto& row : quiet_out)
        for (float v : row) quiet_sum += v;
    // A tone carries more energy than silence in the log-mel domain.
    CHECK(tone_sum > quiet_sum);

    // Shorter than one window: no frames, no wrap-around.
    std::vector<std::vector<float>> tiny_out;
    CHECK(mel.compute(std::vector<float>(100, 0.1f), tiny_out) == 0);
    CHECK(tiny_out.empty());
}

void test_energy_vad() {
    std::printf("[energy VAD]\n");
    muninn::VADOptions opts;
    opts.adaptive_threshold = false;
    opts.threshold = 0.02f;
    opts.audio_preamble_ms = 0;
    muninn::VAD vad(opts);

    const auto audio = tone_in_silence(16000, 3.0f, 1.0f, 2.0f);
    const auto segs = vad.detect_speech(audio, 16000);
    CHECK(segs.size() == 1);
    if (segs.size() == 1) {
        std::printf("  segment %.3f - %.3f s\n", segs[0].start, segs[0].end);
        CHECK(segs[0].start > 0.7f && segs[0].start < 1.05f);
        CHECK(segs[0].end > 1.95f && segs[0].end < 2.3f);
    }

    std::vector<muninn::SpeechSegment> filtered_segs;
    const auto kept = vad.filter_silence(audio, 16000, filtered_segs);
    CHECK(!filtered_segs.empty());
    // Roughly the one second of tone plus padding, not the full three seconds.
    CHECK(kept.size() > 16000 * 0.9 && kept.size() < 16000 * 1.6);

    const std::vector<float> silence(16000 * 2, 0.0f);
    CHECK(vad.detect_speech(silence, 16000).empty());

    const auto merged = muninn::merge_overlapping_segments(
        {{2.0f, 3.0f}, {0.0f, 1.0f}, {0.5f, 1.5f}});
    CHECK(merged.size() == 2);
    if (merged.size() == 2) {
        CHECK(merged[0].start == 0.0f && merged[0].end == 1.5f);
        CHECK(merged[1].start == 2.0f && merged[1].end == 3.0f);
    }

    const auto quiet = muninn::analyze_audio_characteristics(silence);
    CHECK(quiet.is_silent);
    // 330 Hz: analyze_audio_characteristics reads every 1000th sample, and at 440 Hz
    // every one of those lands on a zero crossing.
    const auto loud = muninn::analyze_audio_characteristics(tone_in_silence(16000, 3.0f, 0.0f, 3.0f, 0.3f, 330.0f));
    CHECK(!loud.is_silent);
}

void test_audio_extractor() {
    std::printf("[audio extractor]\n");
    const auto path = std::filesystem::temp_directory_path() / "muninn_test_core.wav";
    // Mono, so the level is not changed by a downmix matrix; 44.1 kHz so it is resampled.
    CHECK(write_wav(path, 44100, 1, 2.0f));

    muninn::AudioExtractor ex;
    const bool opened = ex.open(path.string());
    CHECK(opened);
    if (opened) {
        CHECK(ex.get_track_count() == 1);
        CHECK(std::fabs(ex.get_duration() - 2.0f) < 0.05f);

        std::vector<float> samples;
        const bool ok = ex.extract_track(0, samples);
        CHECK(ok);
        // Resampled to 16 kHz mono.
        std::printf("  %zu samples\n", samples.size());
        CHECK(samples.size() > 31500 && samples.size() < 32500);

        double sum = 0.0;
        float peak = 0.0f;
        for (float s : samples) {
            sum += double(s) * s;
            peak = std::max(peak, std::fabs(s));
        }
        const double rms = samples.empty() ? 0.0 : std::sqrt(sum / samples.size());
        // A 0.5-amplitude sine: RMS about 0.35.
        CHECK(rms > 0.33 && rms < 0.37);
        CHECK(peak <= 1.0f);
        ex.close();
    } else {
        std::printf("  open failed: %s\n", ex.get_last_error().c_str());
    }

    muninn::AudioExtractor missing;
    CHECK(!missing.open((std::filesystem::temp_directory_path() / "muninn_no_such_file.wav").string()));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void test_build_features() {
    std::printf("[build features]\n");
#ifdef MUNINN_TEST_EXPECT_SILERO
    CHECK(muninn::is_silero_vad_available());
#else
    std::printf("  Silero VAD not compiled in; skipping\n");
#endif
}

}  // namespace

int main() {
    test_subtitle_timestamps();
    test_mel_spectrogram();
    test_energy_vad();
    test_audio_extractor();
    test_build_features();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
