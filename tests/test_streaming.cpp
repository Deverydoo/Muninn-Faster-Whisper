// Checks for StreamingTranscriber.
//
//   test_streaming
//       The parts that need no model (ctest label "cpu"): the resampler's
//       phase and drift, channel downmix, sample-rate checks, the shared audio
//       buffer under a concurrent producer, GPT-2 text decoding of non-ASCII
//       tokens, word splitting, and the capped segment queue.
//
//   test_streaming --model <whisper dir> --audio <file> [--device cpu|cuda|auto]
//                  [--seconds N] [--track N]
//       A real streaming session (ctest labels "model;gpu", registered only
//       when MUNINN_TEST_MODEL and MUNINN_TEST_AUDIO are set). The audio is
//       pushed as 48 kHz stereo in 10 ms blocks, as fast as possible, so the
//       decoder falls behind and timestamps have to come from the buffer's
//       absolute offset.
//
// Exit code 0 = all checks passed.

#include "muninn/audio_extractor.h"
#include "muninn/streaming_transcriber.h"

#include "streaming_audio.h"
#include "text_decode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <mutex>
#include <thread>
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

using muninn::streaming_detail::AudioBuffer;
using muninn::streaming_detail::CappedQueue;
using muninn::streaming_detail::LinearResampler;
using muninn::streaming_detail::downmix_to_mono;

std::vector<float> sine(int rate, size_t n, double freq) {
    std::vector<float> s(n);
    for (size_t i = 0; i < n; ++i) {
        s[i] = static_cast<float>(std::sin(2.0 * kPi * freq * double(i) / rate));
    }
    return s;
}

// How many outputs a resampler must produce for n inputs: output k sits at
// input position k * in / out, and exists while that is <= n - 1.
size_t expected_outputs(uint64_t n, int in_rate, int out_rate) {
    if (n == 0) return 0;
    return static_cast<size_t>((n - 1) * static_cast<uint64_t>(out_rate) / static_cast<uint64_t>(in_rate) + 1);
}

// ---------------------------------------------------------------------------
// Resampler
// ---------------------------------------------------------------------------

void test_resampler_blocks_match_one_shot() {
    std::printf("resampler: blocks match one shot\n");
    std::mt19937 rng(1234);
    for (int in_rate : {48000, 44100, 22050, 8000, 11025, 96000}) {
        const size_t n = static_cast<size_t>(in_rate) * 3 + 17;
        const auto input = sine(in_rate, n, 440.0);

        LinearResampler whole;
        whole.set_rates(in_rate, 16000);
        std::vector<float> expect;
        whole.process(input.data(), input.size(), expect);

        LinearResampler pieces;
        pieces.set_rates(in_rate, 16000);
        std::vector<float> got;
        std::uniform_int_distribution<size_t> size_dist(1, 997);
        for (size_t pos = 0; pos < n;) {
            const size_t len = std::min(size_dist(rng), n - pos);
            pieces.process(input.data() + pos, len, got);
            pos += len;
        }

        CHECK(expect.size() == expected_outputs(n, in_rate, 16000));
        CHECK(got.size() == expect.size());
        float worst = 0.0f;
        for (size_t i = 0; i < std::min(got.size(), expect.size()); ++i) {
            worst = std::max(worst, std::fabs(got[i] - expect[i]));
        }
        CHECK(worst < 1e-6f);

        // And the values follow the sine at the output times (linear
        // interpolation error for 440 Hz is tiny at these rates).
        float err = 0.0f;
        for (size_t k = 0; k < got.size(); ++k) {
            const double t = double(k) / 16000.0;
            err = std::max(err, static_cast<float>(std::fabs(got[k] - std::sin(2.0 * kPi * 440.0 * t))));
        }
        CHECK(err < (in_rate < 16000 ? 0.05f : 0.01f));
    }
}

void test_resampler_no_drift() {
    std::printf("resampler: one hour of 44.1 kHz in 10 ms blocks, no drift\n");
    // One hour pushed as 441-sample blocks: the output must be exactly
    // 16000 * 3600 samples long (within the one-sample edge), and the last
    // sample must still be on the sine, i.e. the phase hasn't wandered.
    const int in_rate = 44100;
    const size_t block = 441;
    const size_t blocks = 3600 * 100;
    const double freq = 50.0;
    LinearResampler r;
    r.set_rates(in_rate, 16000);
    std::vector<float> in(block);
    std::vector<float> out;
    out.reserve(block);
    uint64_t produced = 0;
    float last = 0.0f;
    uint64_t index = 0;
    for (size_t b = 0; b < blocks; ++b) {
        for (size_t i = 0; i < block; ++i, ++index) {
            in[i] = static_cast<float>(std::sin(2.0 * kPi * freq * double(index) / in_rate));
        }
        out.clear();
        r.process(in.data(), block, out);
        produced += out.size();
        if (!out.empty()) last = out.back();
    }
    const uint64_t n = uint64_t(block) * blocks;
    CHECK(produced == expected_outputs(n, in_rate, 16000));
    CHECK(produced >= 16000ull * 3600 - 1 && produced <= 16000ull * 3600);
    const double t_last = double(produced - 1) / 16000.0;
    CHECK(std::fabs(last - std::sin(2.0 * kPi * freq * t_last)) < 1e-3);
}

void test_resampler_rates() {
    std::printf("resampler: rate checks\n");
    LinearResampler r;
    bool threw = false;
    try { r.set_rates(0, 16000); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { r.set_rates(-44100, 16000); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // Same rate: a straight copy.
    r.set_rates(16000, 16000);
    std::vector<float> in = {0.1f, 0.2f, 0.3f};
    std::vector<float> out;
    r.process(in.data(), in.size(), out);
    CHECK(out == in);
}

// ---------------------------------------------------------------------------
// Downmix
// ---------------------------------------------------------------------------

void test_downmix() {
    std::printf("downmix: any channel count\n");
    std::vector<float> out;

    const float stereo[] = {1.0f, 0.0f, 0.5f, 0.5f, -1.0f, 1.0f};
    downmix_to_mono(stereo, 6, 2, out);
    CHECK(out.size() == 3);
    CHECK(std::fabs(out[0] - 0.5f) < 1e-6f && std::fabs(out[1] - 0.5f) < 1e-6f && std::fabs(out[2]) < 1e-6f);

    // 5.1: six channels averaged, not treated as mono.
    std::vector<float> six;
    for (int f = 0; f < 4; ++f) {
        for (int c = 0; c < 6; ++c) six.push_back(static_cast<float>(c + f));
    }
    downmix_to_mono(six.data(), six.size(), 6, out);
    CHECK(out.size() == 4);
    for (int f = 0; f < 4; ++f) CHECK(std::fabs(out[f] - (2.5f + f)) < 1e-5f);

    // Three channels with a trailing partial frame: the partial frame is ignored.
    const float three[] = {0.3f, 0.3f, 0.3f, 0.9f, 0.0f, 0.0f, 0.7f};
    downmix_to_mono(three, 7, 3, out);
    CHECK(out.size() == 2);
    CHECK(std::fabs(out[0] - 0.3f) < 1e-6f && std::fabs(out[1] - 0.3f) < 1e-6f);

    downmix_to_mono(stereo, 6, 1, out);
    CHECK(out.size() == 6);

    bool threw = false;
    try { downmix_to_mono(stereo, 6, 0, out); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

// ---------------------------------------------------------------------------
// Audio buffer
// ---------------------------------------------------------------------------

void test_audio_buffer_concurrent() {
    std::printf("audio buffer: concurrent producer, absolute offsets, drops\n");
    // The producer writes sample i as (i mod 2^20), which floats hold exactly.
    // The consumer checks that the value at absolute position p is p mod 2^20,
    // whatever was dropped, so offsets never slip.
    constexpr uint64_t kMod = 1u << 20;
    constexpr uint64_t kTotal = 4'000'000;
    AudioBuffer buffer(16000 * 2);  // small, so the producer overruns it
    std::atomic<bool> done{false};

    std::thread producer([&] {
        std::mt19937 rng(7);
        std::uniform_int_distribution<size_t> len_dist(1, 2000);
        std::vector<float> block;
        uint64_t next = 0;
        while (next < kTotal) {
            const size_t len = static_cast<size_t>(std::min<uint64_t>(len_dist(rng), kTotal - next));
            block.resize(len);
            for (size_t i = 0; i < len; ++i) block[i] = static_cast<float>((next + i) % kMod);
            buffer.push(block.data(), len);
            next += len;
        }
        done = true;
    });

    std::mt19937 rng(99);
    std::uniform_int_distribution<size_t> take_dist(1, 6000);
    std::vector<float> copy;
    bool ordered = true;
    bool values_ok = true;
    uint64_t last_front = 0;
    uint64_t checked = 0;
    while (true) {
        const bool finished = done.load();
        const auto snap = buffer.copy_front(32000, copy);
        if (snap.front < last_front) ordered = false;
        last_front = snap.front;
        for (size_t i = 0; i < copy.size(); ++i) {
            if (copy[i] != static_cast<float>((snap.front + i) % kMod)) {
                values_ok = false;
                break;
            }
        }
        checked += copy.size();
        if (copy.empty() && finished) break;
        buffer.consume_to(snap.front + std::min<size_t>(take_dist(rng), copy.size()), snap.generation);
    }
    producer.join();

    CHECK(values_ok);
    CHECK(ordered);
    CHECK(checked > 0);
    const auto end = buffer.peek();
    CHECK(end.front + end.available == kTotal);  // everything pushed is accounted for
    CHECK(end.available == 0);
    CHECK(buffer.dropped() > 0);                  // the small buffer did overflow
    CHECK(buffer.dropped() <= kTotal);
}

void test_audio_buffer_generation() {
    std::printf("audio buffer: clear() makes old positions stale\n");
    AudioBuffer buffer(1000);
    std::vector<float> data(500, 1.0f);
    buffer.push(data.data(), data.size());
    const auto before = buffer.peek();
    buffer.clear();
    buffer.push(data.data(), 300);
    buffer.consume_to(before.front + 200, before.generation);  // stale: ignored
    CHECK(buffer.size() == 300);
    const auto after = buffer.peek();
    CHECK(after.front == 0);
    buffer.consume_to(100, after.generation);
    CHECK(buffer.size() == 200);
    CHECK(buffer.peek().front == 100);
    buffer.consume_to(10'000, after.generation);  // past the end: just empties it
    CHECK(buffer.size() == 0);
    CHECK(buffer.peek().front == 300);

    // Overflow drops the oldest and moves the front with it.
    buffer.reset(100);
    std::vector<float> seq(250);
    for (size_t i = 0; i < seq.size(); ++i) seq[i] = static_cast<float>(i);
    CHECK(buffer.push(seq.data(), seq.size()) == 150);
    std::vector<float> copy;
    const auto snap = buffer.copy_front(1000, copy);
    CHECK(snap.front == 150 && copy.size() == 100 && copy.front() == 150.0f);
}

// ---------------------------------------------------------------------------
// Capped queue
// ---------------------------------------------------------------------------

void test_capped_queue() {
    std::printf("capped queue\n");
    CappedQueue<int> q(1000);
    for (int i = 0; i < 1500; ++i) q.push(i);
    CHECK(q.size() == 1000);
    CHECK(q.dropped() == 500);
    int v = -1;
    CHECK(q.pop(v) && v == 500);
    q.set_cap(10);
    CHECK(q.size() == 10);
    CHECK(q.pop(v) && v == 1490);
    q.clear();
    CHECK(!q.pop(v));
    CHECK(q.dropped() == 0);
}

// ---------------------------------------------------------------------------
// Text decoding
// ---------------------------------------------------------------------------

// GPT-2's bytes_to_unicode, forwards: what the vocabulary stores for bytes.
std::string gpt2_encode(const std::string& bytes) {
    std::vector<int> table(256, -1);
    std::vector<int> bs;
    for (int b = '!'; b <= '~'; ++b) bs.push_back(b);
    for (int b = 0xA1; b <= 0xAC; ++b) bs.push_back(b);
    for (int b = 0xAE; b <= 0xFF; ++b) bs.push_back(b);
    for (int b : bs) table[b] = b;
    int n = 0;
    for (int b = 0; b < 256; ++b) {
        if (table[b] < 0) table[b] = 256 + n++;
    }
    std::string out;
    for (unsigned char c : bytes) {
        const int cp = table[c];
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

void test_text_decoding() {
    std::printf("text: GPT-2 byte decoding of non-ASCII tokens\n");
    using namespace muninn::text;

    // Whole words.
    for (const char* word : {u8"café", u8"日本語", u8"Привет", u8"Größe", u8"😀", u8"ﺔﺑﺮﻌﻟﺍ"}) {
        CHECK_EQ_STR(clean_token(gpt2_encode(word)), word);
    }
    // The space marker.
    CHECK_EQ_STR(clean_token(gpt2_encode(" hello")), " hello");
    CHECK_EQ_STR(clean_token_raw(gpt2_encode(" hello")), "hello");
    CHECK_EQ_STR(clean_token("\xC4\xA0" "caf"), " caf");

    // A character split across tokens comes back whole once joined, and
    // timestamps/specials are skipped.
    const std::string nihon = u8"日本";
    const std::vector<std::string> tokens = {
        "<|0.00|>", gpt2_encode(nihon.substr(0, 2)), gpt2_encode(nihon.substr(2, 3)),
        gpt2_encode(nihon.substr(5)), "<|1.20|>", "<|endoftext|>"};
    CHECK_EQ_STR(extract_text(tokens), nihon);
    CHECK_EQ_STR(extract_text({gpt2_encode(" Je"), gpt2_encode(u8" suis"), gpt2_encode(u8" à"),
                               gpt2_encode(u8" l'école"), gpt2_encode(".")}),
                 u8"Je suis à l'école.");

    // The old streaming decoder left these as byte characters.
    CHECK(clean_token(gpt2_encode(u8"é")) != gpt2_encode(u8"é"));

    CHECK(is_incomplete_utf8(nihon.substr(0, 2)));
    CHECK(!is_incomplete_utf8(nihon.substr(0, 3)));
    CHECK(!is_incomplete_utf8("abc"));
    CHECK(is_incomplete_utf8(std::string(u8"😀").substr(0, 3)));
}

void test_word_splitting() {
    std::printf("text: words from tokens\n");
    using namespace muninn::text;
    const size_t eot = 50257;

    // Spaced language: Ġ starts a word, continuations join, punctuation alone.
    {
        const std::vector<std::string> tokens = {gpt2_encode(" Hel"), gpt2_encode("lo"), gpt2_encode(","),
                                                 gpt2_encode(u8" écol"), gpt2_encode("e"), gpt2_encode("!")};
        const std::vector<size_t> ids = {1, 2, 3, 4, 5, 6};
        auto words = split_to_word_tokens(tokens, ids, eot, "fr");
        CHECK(words.size() == 4);
        if (words.size() == 4) {
            CHECK_EQ_STR(words[0].text, " Hello");
            CHECK_EQ_STR(words[1].text, ",");
            CHECK_EQ_STR(words[2].text, u8" école");
            CHECK(words[2].token_indices == (std::vector<size_t>{3, 4}));
        }
        merge_punctuations(words);
        CHECK(words.size() == 2);
        if (words.size() == 2) {
            CHECK_EQ_STR(words[0].text, " Hello,");
            CHECK_EQ_STR(words[1].text, u8" école!");
            CHECK(words[1].token_indices == (std::vector<size_t>{3, 4, 5}));
        }
    }

    // Spaceless language: words end where the bytes are whole characters,
    // even when a character is split across tokens.
    {
        const std::string text = u8"日本語";
        const std::vector<std::string> tokens = {gpt2_encode(text.substr(0, 2)), gpt2_encode(text.substr(2, 1)),
                                                 gpt2_encode(text.substr(3, 3)), gpt2_encode(text.substr(6, 3))};
        const std::vector<size_t> ids = {10, 11, 12, 13};
        auto words = split_to_word_tokens(tokens, ids, eot, "ja");
        CHECK(words.size() == 3);
        if (words.size() == 3) {
            CHECK_EQ_STR(words[0].text, u8"日");
            CHECK(words[0].token_indices == (std::vector<size_t>{0, 1}));
            CHECK_EQ_STR(words[2].text, u8"語");
        }
    }

    // Opening punctuation joins the next word.
    {
        std::vector<WordTokens> words = {{" (", {0}}, {" hey", {1}}, {")", {2}}};
        merge_punctuations(words);
        CHECK(words.size() == 1);
        if (words.size() == 1) CHECK_EQ_STR(words[0].text, " ( hey)");
    }
}

void test_compression_ratio() {
    std::printf("text: compression ratio\n");
    using muninn::text::compression_ratio_of;
    std::string loop;
    for (int i = 0; i < 30; ++i) loop += "thank you for watching ";
    CHECK(compression_ratio_of(loop) > 2.4f);
    CHECK(compression_ratio_of("The quick brown fox jumps over the lazy dog near the river bank.") < 2.4f);
    CHECK(compression_ratio_of("") == 0.0f);
}

// ---------------------------------------------------------------------------
// With a model
// ---------------------------------------------------------------------------

int run_model_test(const std::string& model, const std::string& audio, const std::string& device,
                   float seconds, int track) {
    std::printf("model: streaming %s with %s (%s)\n", audio.c_str(), model.c_str(), device.c_str());

    std::vector<float> samples;
    float duration = 0.0f;
    muninn::AudioExtractor extractor;
    const bool read_ok = track < 0 ? extractor.extract_audio(audio, samples, duration)
                                   : (extractor.open(audio) && extractor.extract_track(track, samples));
    if (!read_ok) {
        std::printf("  FAIL could not read %s: %s\n", audio.c_str(), extractor.get_last_error().c_str());
        return 1;
    }
    if (seconds > 0.0f && samples.size() > static_cast<size_t>(seconds * 16000)) {
        samples.resize(static_cast<size_t>(seconds * 16000));
    }
    duration = static_cast<float>(samples.size()) / 16000.0f;

    // 16 kHz mono -> 48 kHz stereo, so push_audio has to downmix and resample.
    std::vector<float> stereo48;
    stereo48.reserve(samples.size() * 6);
    for (size_t i = 0; i < samples.size(); ++i) {
        const float a = samples[i];
        const float b = (i + 1 < samples.size()) ? samples[i + 1] : a;
        for (int k = 0; k < 3; ++k) {
            const float v = a + (b - a) * (static_cast<float>(k) / 3.0f);
            stereo48.push_back(v);
            stereo48.push_back(v);
        }
    }

    muninn::ModelOptions opts;
    opts.model_path = model;
    opts.device = device == "cpu" ? muninn::DeviceType::CPU
                : device == "cuda" ? muninn::DeviceType::CUDA
                                   : muninn::DeviceType::Auto;
    opts.compute_type = device == "cpu" ? muninn::ComputeType::Int8 : muninn::ComputeType::Float16;

    try {
        muninn::StreamingTranscriber stream(opts);

        muninn::StreamingOptions so;
        so.language = "auto";
        so.chunk_length_s = 5.0f;
        so.overlap_s = 1.0f;
        so.max_buffered_s = duration + 10.0f;  // keep it all: the decoder will be behind
        so.word_timestamps = true;

        std::atomic<bool> stop_refused{false};
        std::atomic<int> callbacks{0};
        std::vector<std::string> errors;
        std::mutex errors_mutex;
        stream.set_error_callback([&](const std::string& m) {
            std::lock_guard<std::mutex> lock(errors_mutex);
            errors.push_back(m);
        });
        stream.start(so, [&](const muninn::Segment& seg) {
            if (callbacks++ == 0) {
                try {
                    stream.stop();
                } catch (const std::logic_error&) {
                    stop_refused = true;
                }
            }
            std::printf("  [%7.2f - %7.2f] %s\n", seg.start, seg.end, seg.text.c_str());
            return true;
        });

        CHECK(!stream.push_audio(stereo48.data(), 100, 0, 2));
        CHECK(!stream.push_audio(stereo48.data(), 100, -48000, 2));
        CHECK(!stream.push_audio(stereo48.data(), 100, 48000, 0));

        const size_t block = 480 * 2;  // 10 ms of 48 kHz stereo
        for (size_t pos = 0; pos < stereo48.size(); pos += block) {
            const size_t len = std::min(block, stereo48.size() - pos);
            CHECK(stream.push_audio(stereo48.data() + pos, len, 48000, 2));
        }
        const auto stats_before = stream.get_stats();
        CHECK(std::fabs(stats_before.total_audio_duration_s - duration) < 0.01f);

        const auto segments = stream.stop();
        CHECK(!segments.empty());
        CHECK(stop_refused.load());
        CHECK(callbacks.load() == static_cast<int>(segments.size()));

        float prev_start = -1.0f;
        float prev_word_end = -1.0f;
        std::string prev_last_word;
        bool times_ok = true;
        bool words_ordered = true;
        bool repeated_at_join = false;
        for (const auto& seg : segments) {
            if (!(seg.start <= seg.end) || seg.start < prev_start - 0.01f || seg.end > duration + 0.5f) {
                times_ok = false;
            }
            prev_start = seg.start;
            if (!seg.words.empty()) {
                if (seg.words.front().start < prev_word_end - 0.05f) words_ordered = false;
                if (!prev_last_word.empty() && seg.words.front().word == prev_last_word &&
                    seg.words.front().start - prev_word_end < 0.3f) {
                    repeated_at_join = true;  // the overlap came out twice
                }
                prev_word_end = seg.words.back().end;
                prev_last_word = seg.words.back().word;
            }
            CHECK(!seg.language.empty() && seg.language != "auto");
        }
        CHECK(times_ok);
        CHECK(words_ordered);
        if (repeated_at_join) std::printf("  note: a word repeats across a window join (may be real speech)\n");
        CHECK(stream.get_stats().chunks_failed == 0);
        {
            std::lock_guard<std::mutex> lock(errors_mutex);
            for (const auto& e : errors) std::printf("  error reported: %s\n", e.c_str());
        }
        std::printf("  %zu segments, language %s\n", segments.size(), segments.front().language.c_str());
    } catch (const std::exception& e) {
        std::printf("  FAIL %s\n", e.what());
        ++g_failures;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string model, audio, device = "auto";
    float seconds = 0.0f;
    int track = -1;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--model") model = argv[i + 1];
        else if (key == "--audio") audio = argv[i + 1];
        else if (key == "--device") device = argv[i + 1];
        else if (key == "--seconds") seconds = static_cast<float>(std::atof(argv[i + 1]));
        else if (key == "--track") track = std::atoi(argv[i + 1]);
    }

    if (!model.empty() || !audio.empty()) {
        if (model.empty() || audio.empty()) {
            std::printf("usage: test_streaming --model <dir> --audio <file> [--device cpu|cuda|auto] [--seconds N] [--track N]\n");
            return 2;
        }
        run_model_test(model, audio, device, seconds, track);
    } else {
        test_resampler_blocks_match_one_shot();
        test_resampler_no_drift();
        test_resampler_rates();
        test_downmix();
        test_audio_buffer_concurrent();
        test_audio_buffer_generation();
        test_capped_queue();
        test_text_decoding();
        test_word_splitting();
        test_compression_ratio();
    }

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
