// muninn_accuracy: transcribe one audio file with Loki Studio's settings and
// write the segments as JSON, for tests/accuracy/compare.py to score against
// faster-whisper. Not shipped.
//
//   muninn_accuracy <model dir> <audio> <out.json> [silero_vad.onnx]

#include "muninn/transcriber.h"

#include <chrono>
#include <cstdlib>
#include <io.h>
#include <process.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

static std::string jsonEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: muninn_accuracy <model dir> <audio> <out.json> [silero_vad.onnx]\n");
        return 2;
    }
    try {
        muninn::Transcriber transcriber(argv[1], "cuda", "float16");

        // As Loki Studio's GPU worker sends them, with the owner's settings
        // (app_settings.json, October 2026).
        muninn::TranscribeOptions o;
        o.language = "en";
        o.task = "transcribe";
        o.beam_size = 10;
        o.batch_size = 24;
        o.patience = 2.0f;
        o.word_timestamps = true;
        o.vad_filter = true;
        o.vad_type = argc > 4 ? muninn::VADType::Silero : muninn::VADType::Energy;
        if (argc > 4) {
            o.silero_model_path = argv[4];
            o.vad_threshold = 0.35f;
        }
        o.compression_ratio_threshold = 2.4f;
        o.no_speech_threshold = 0.6f;
        o.log_prob_threshold = -1.0f;
        o.hallucination_silence_threshold = 0.1f;
        o.vad_speech_pad_ms = 500;
        // MUNINN_SEQUENTIAL=0: the older batched path (sequential is the default).
        if (const char* seq = std::getenv("MUNINN_SEQUENTIAL")) {
            o.sequential = std::string(seq) == "1";
        }
        // MUNINN_PROMPT: initial prompt (Loki's Application Settings > Initial Prompt).
        if (const char* prompt = std::getenv("MUNINN_PROMPT")) {
            o.initial_prompt = prompt;
        }

        const auto t0 = std::chrono::steady_clock::now();
        const auto result = transcriber.transcribe(argv[2], o);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        std::ofstream out(argv[3], std::ios::binary);
        out << "{\"seconds\": " << took << ", \"segments\": [\n";
        for (size_t i = 0; i < result.segments.size(); ++i) {
            const auto& s = result.segments[i];
            out << "  {\"start\": " << s.start << ", \"end\": " << s.end
                << ", \"text\": \"" << jsonEscape(s.text) << "\"}" << (i + 1 < result.segments.size() ? "," : "") << "\n";
        }
        out << "]}\n";
        out.close();
        std::cerr << "[accuracy] " << result.segments.size() << " segments in " << took << " s\n";
        // CTranslate2's CUDA teardown can hang at exit (Loki Studio ends with
        // _exit for the same reason), so leave without running destructors.
        std::fflush(nullptr);
        _exit(0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "failed: %s\n", e.what());
        return 1;
    }
}
