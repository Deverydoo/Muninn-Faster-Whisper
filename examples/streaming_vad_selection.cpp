// Shows how StreamingTranscriber picks its VAD for each session.
//
// Usage: streaming_vad_selection <whisper_model> <silero_vad.onnx>
//
// Runs four short sessions on a synthetic tone:
//   1. Silero requested with a valid model  -> Silero
//   2. Silero requested with no model path  -> falls back to energy
//   3. Silero again                         -> reuses the loaded session
//   4. Energy explicitly                    -> energy
// The VAD chosen is in Muninn's log lines (stderr).
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

#include "muninn/streaming_transcriber.h"

namespace {

std::vector<float> make_tone(int rate, int seconds) {
    std::vector<float> samples(size_t(rate) * seconds, 0.0f);
    for (int i = rate; i < rate * 2 && i < int(samples.size()); ++i) {
        const double t = double(i - rate) / rate;
        samples[size_t(i)] = float(
            0.35 * std::sin(2.0 * 3.14159265 * 140.0 * t) +
            0.20 * std::sin(2.0 * 3.14159265 * 280.0 * t));
    }
    return samples;
}

void run_session(muninn::StreamingTranscriber &stream,
                 const char *label,
                 muninn::VADType type,
                 const std::string &vad_model) {
    std::printf("\n--- %s ---\n", label);

    muninn::StreamingOptions opts;
    opts.language = "en";
    opts.enable_vad = true;
    opts.vad_type = type;
    opts.vad_model_path = vad_model;
    opts.chunk_length_s = 1.5f;
    opts.overlap_s = 0.3f;
    opts.word_timestamps = false;

    stream.reset();
    stream.start(opts);

    const auto audio = make_tone(16000, 3);
    stream.push_audio(audio.data(), audio.size(), 16000, 1);

    auto tail = stream.stop();
    std::printf("    session ended cleanly, %zu tail segment(s)\n", tail.size());
}

} // namespace

int main(int argc, char **argv) {
    const std::string whisper = (argc > 1)
        ? argv[1]
        : "models/faster-whisper-large-v3-turbo";
    const std::string silero = (argc > 2)
        ? argv[2]
        : "models/silero_vad.onnx";

    try {
        muninn::ModelOptions model_opts;
        model_opts.model_path = whisper;
        model_opts.device = muninn::DeviceType::Auto;
        model_opts.compute_type = muninn::ComputeType::Float16;

        muninn::StreamingTranscriber stream(model_opts);

        run_session(stream, "1. Silero requested, model supplied", muninn::VADType::Silero, silero);
        run_session(stream, "2. Silero requested, NO model path", muninn::VADType::Silero, "");
        run_session(stream, "3. Silero again (should reuse)", muninn::VADType::Silero, silero);
        run_session(stream, "4. Energy explicitly", muninn::VADType::Energy, silero);

        std::printf("\nRESULT: streaming VAD selection exercised without error.\n");
        std::printf("Check the [Muninn] lines above: 1 and 3 must say Silero, 2 and 4 energy.\n");
        return 0;
    } catch (const std::exception &e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
