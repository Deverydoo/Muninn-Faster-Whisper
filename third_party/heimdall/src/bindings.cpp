#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "heimdall.h"

namespace py = pybind11;
using namespace heimdall;

PYBIND11_MODULE(heimdall, m) {
    m.doc() = "Heimdall - The Vigilant Guardian of Audio Waveforms\n\n"
              "Named after the Norse god with incredible hearing,\n"
              "provides ultra-fast waveform visualization with SIMD optimization.";

    // AudioInfo struct
    py::class_<AudioInfo>(m, "AudioInfo")
        .def_readonly("duration_ms", &AudioInfo::duration_ms,
            "Duration in milliseconds")
        .def_readonly("sample_rate", &AudioInfo::sample_rate,
            "Sample rate in Hz")
        .def_readonly("channels", &AudioInfo::channels,
            "Number of channels per stream")
        .def_readonly("stream_count", &AudioInfo::stream_count,
            "Total number of audio streams")
        .def("__repr__", [](const AudioInfo& info) {
            return "<AudioInfo duration=" + std::to_string(info.duration_ms) + "ms "
                   "rate=" + std::to_string(info.sample_rate) + "Hz "
                   "channels=" + std::to_string(info.channels) + " "
                   "streams=" + std::to_string(info.stream_count) + ">";
        });

    // Heimdall class
    py::class_<Heimdall>(m, "Heimdall",
        "The vigilant guardian watches over your audio streams,\n"
        "providing instant waveform visualization with incredible accuracy.")
        .def(py::init<>())
        .def("generate_peaks", &Heimdall::generate_peaks,
            py::call_guard<py::gil_scoped_release>(),  // Release GIL during C++ execution
            py::arg("audio_file"),
            py::arg("stream_index") = 0,
            py::arg("width") = 800,
            py::arg("height") = 60,
            py::arg("samples_per_pixel") = 512,
            py::arg("normalize") = true,
            "Generate waveform peaks for visualization\n\n"
            "Args:\n"
            "    audio_file: Path to audio/video file\n"
            "    stream_index: Audio stream index (0 for first stream)\n"
            "    width: Output width in pixels\n"
            "    height: Output height in pixels\n"
            "    samples_per_pixel: Downsampling factor (higher = faster)\n"
            "    normalize: Auto-normalize peaks to 0.0-1.0 range\n"
            "Returns:\n"
            "    List of peak values (width * 2 for min/max pairs)")
        .def("generate_batch", &Heimdall::generate_batch,
            py::call_guard<py::gil_scoped_release>(),  // Release GIL during C++ execution
            py::arg("audio_file"),
            py::arg("stream_indices"),
            py::arg("width") = 800,
            py::arg("height") = 60,
            py::arg("target_sample_rate") = 48000,
            py::arg("packet_quality") = 10,
            "Generate waveforms for multiple streams in one pass\n\n"
            "As Heimdall guards multiple realms, this efficiently processes\n"
            "multiple audio streams simultaneously.\n\n"
            "Args:\n"
            "    audio_file: Path to audio/video file\n"
            "    stream_indices: List of stream indices to process\n"
            "    width: Output width in pixels\n"
            "    height: Output height in pixels\n"
            "    target_sample_rate: Target sample rate (8000/16000/44100/48000)\n"
            "    packet_quality: Quality percentage (10=fastest, 100=max quality)\n"
            "Returns:\n"
            "    Dict mapping stream_index -> peak values")
        .def("get_audio_info", &Heimdall::get_audio_info,
            py::arg("audio_file"),
            "Get audio file metadata (fast query)\n\n"
            "Heimdall's keen senses detect audio properties instantly.\n\n"
            "Args:\n"
            "    audio_file: Path to audio/video file\n"
            "Returns:\n"
            "    AudioInfo object with metadata")
        .def("extract_audio", &Heimdall::extract_audio,
            py::call_guard<py::gil_scoped_release>(),
            py::arg("audio_file"),
            py::arg("sample_rate") = 16000,
            py::arg("stream_indices") = std::vector<int>{},
            py::arg("quality") = 100,
            "Extract audio from streams (core extraction method)\n\n"
            "Heimdall's acute hearing extracts every sound with perfect fidelity.\n"
            "Each audio track is preserved separately - no mixing or merging.\n"
            "Output is mono float32 samples ready for CTranslate2/Whisper.\n\n"
            "Args:\n"
            "    audio_file: Path to audio/video file\n"
            "    sample_rate: Target sample rate (default 16000 for Whisper, up to 48000)\n"
            "    stream_indices: List of stream indices to extract (empty = all streams)\n"
            "    quality: Decode quality 1-100 (100=full for transcription, 10=fast for waveforms)\n"
            "Returns:\n"
            "    Dict mapping stream_index -> list of float32 samples\n\n"
            "Examples:\n"
            "    # Transcription (full quality, 16kHz)\n"
            "    tracks = h.extract_audio('video.mp4')\n"
            "    # Fast waveform extraction\n"
            "    tracks = h.extract_audio('video.mp4', 48000, [], 10)\n"
            "    # Specific streams only\n"
            "    tracks = h.extract_audio('video.mp4', 16000, [0, 2])");

    // Version and credits
    m.attr("__version__") = "2.0.0";
    m.attr("__author__") = "NordIQ AI / Loki Studio";
    m.attr("__description__") = "The Vigilant Guardian - Audio Extraction & Waveform Engine";
}
