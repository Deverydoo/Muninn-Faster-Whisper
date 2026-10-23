"""Transcription accuracy check: Muninn against faster-whisper on fixed clips.

    python -I tests/accuracy/compare.py [--muninn build_static/Release/Release/muninn_accuracy.exe]
                                        [--label NAME] [--only CLIP]

For each clip in clips.json: cut it to 16 kHz mono (cached), transcribe it with
faster-whisper (the reference, cached) and with Muninn using Loki Studio's
settings, then report word error rate against the reference, word counts,
short segments and repeated lines. Results go to tests/accuracy/results/.

faster-whisper is not ground truth, but it is the implementation Muninn ports,
so a large gap is worth reading, and the passages where they differ say which
one heard it right.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CACHE = os.path.join(HERE, "cache")
RESULTS = os.path.join(HERE, "results")
FFMPEG = r"D:/Vibe_Projects/Loki-Studio/cpp_modules/ffmpeg/bin/ffmpeg.exe"
MODEL = os.path.expandvars(r"%LOCALAPPDATA%/LokiStudio/Models/whisper/faster-whisper-large-v3-turbo")
LOKI_BIN = r"D:/Vibe_Projects/Loki-Studio/loki_studio-cpp/build/Release"  # CUDA/cuDNN DLLs for Muninn
SILERO = r"D:/Vibe_Projects/Loki-Studio/loki_studio-cpp/build/Release/models/vad/silero_vad.onnx"


def words(text):
    text = text.lower().replace("’", "'")
    text = re.sub(r"[^a-z0-9' ]+", " ", text)
    return text.split()


def wer(ref, hyp):
    """Word error rate with substitution/deletion/insertion counts."""
    n, m = len(ref), len(hyp)
    prev = list(range(m + 1))
    ops = [(0, 0, j) for j in range(m + 1)]
    for i in range(1, n + 1):
        cur = [i] + [0] * m
        cur_ops = [(0, i, 0)] + [None] * m
        for j in range(1, m + 1):
            cost = 0 if ref[i - 1] == hyp[j - 1] else 1
            choices = [
                (prev[j - 1] + cost, "sub"),
                (prev[j] + 1, "del"),
                (cur[j - 1] + 1, "ins"),
            ]
            cur[j], kind = min(choices)
            if kind == "sub":
                s, d, ins = ops[j - 1]
                cur_ops[j] = (s + cost, d, ins)
            elif kind == "del":
                s, d, ins = ops[j]
                cur_ops[j] = (s, d + 1, ins)
            else:
                s, d, ins = cur_ops[j - 1]
                cur_ops[j] = (s, d, ins + 1)
        prev, ops = cur, cur_ops
    s, d, ins = ops[m]
    return (prev[m] / max(1, n)), s, d, ins


def cut(clip):
    os.makedirs(CACHE, exist_ok=True)
    wav = os.path.join(CACHE, clip["name"] + ".wav")
    if not os.path.exists(wav):
        subprocess.run([FFMPEG, "-v", "error", "-y", "-ss", str(clip["start"]), "-t", str(clip["seconds"]),
                        "-i", clip["file"], "-map", "0:a:%d" % clip["track"], "-ac", "1", "-ar", "16000", wav],
                       check=True)
    return wav


def reference(clip, wav):
    path = os.path.join(CACHE, clip["name"] + ".faster-whisper.json")
    if os.path.exists(path):
        return json.load(open(path, encoding="utf-8"))
    # CTranslate2 and NumPy each ship Intel OpenMP in this env; the reference run only.
    os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
    from faster_whisper import WhisperModel
    try:
        model = WhisperModel(MODEL, device="cuda", compute_type="float16")
    except Exception:
        model = WhisperModel(MODEL, device="cpu", compute_type="int8")
    # Loki's turbo folder has no preprocessor_config.json, so faster-whisper
    # assumes 80 mel bins; turbo (and large-v3) need the model's own 128.
    from faster_whisper.feature_extractor import FeatureExtractor
    if model.model.n_mels != model.feature_extractor.mel_filters.shape[0]:
        model.feature_extractor = FeatureExtractor(feature_size=model.model.n_mels)
    t0 = time.time()
    segments, _ = model.transcribe(
        wav, language="en", beam_size=10, patience=2.0,
        vad_filter=True, vad_parameters={"threshold": 0.35},
        compression_ratio_threshold=2.4, log_prob_threshold=-1.0, no_speech_threshold=0.6,
        condition_on_previous_text=True)
    segs = [{"start": s.start, "end": s.end, "text": s.text.strip()} for s in segments]
    out = {"seconds": time.time() - t0, "segments": segs}
    json.dump(out, open(path, "w", encoding="utf-8"), indent=1)
    return out


def stats(result):
    segs = result["segments"]
    texts = [s["text"].strip() for s in segs]
    norm = [" ".join(words(t)) for t in texts]
    repeated = sum(1 for i in range(1, len(norm)) if norm[i] and norm[i] == norm[i - 1])
    short = sum(1 for t in norm if 0 < len(t) <= 3)
    return {"segments": len(segs), "words": sum(len(words(t)) for t in texts),
            "repeated_lines": repeated, "short_lines": short}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--muninn", default=os.path.join(ROOT, "build_static/Release/Release/muninn_accuracy.exe"))
    ap.add_argument("--label", default="run")
    ap.add_argument("--only", default="")
    ap.add_argument("--batched", action="store_true", help="Muninn's older batched path (sequential is the default)")
    args = ap.parse_args()
    args.muninn = os.path.abspath(args.muninn)

    clips = json.load(open(os.path.join(HERE, "clips.json"), encoding="utf-8"))
    os.makedirs(RESULTS, exist_ok=True)
    report = {"label": args.label, "clips": []}
    for clip in clips:
        if args.only and clip["name"] != args.only:
            continue
        wav = cut(clip)
        ref = reference(clip, wav)
        out_json = os.path.join(CACHE, "%s.muninn.%s.json" % (clip["name"], args.label))
        env = dict(os.environ, PATH=LOKI_BIN + os.pathsep + os.environ.get("PATH", ""))
        if args.batched:
            env["MUNINN_SEQUENTIAL"] = "0"
        if os.path.exists(out_json):
            os.remove(out_json)
        proc = subprocess.Popen([args.muninn, MODEL, wav, out_json, SILERO], env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Builds before the fix hang on exit in CTranslate2's CUDA teardown:
        # wait for the finished JSON, then stop the process.
        while True:
            if proc.poll() is not None:
                break
            if os.path.exists(out_json) and open(out_json, encoding="utf-8").read().rstrip().endswith("]}"):
                time.sleep(1)
                proc.kill()
                break
            time.sleep(1)
        if not os.path.exists(out_json):
            raise RuntimeError("muninn_accuracy produced no output for " + clip["name"])
        hyp = json.load(open(out_json, encoding="utf-8"))
        ref_words = words(" ".join(s["text"] for s in ref["segments"]))
        hyp_words = words(" ".join(s["text"] for s in hyp["segments"]))
        rate, sub, dele, ins = wer(ref_words, hyp_words)
        row = {"clip": clip["name"], "wer": round(rate * 100, 1), "sub": sub, "del": dele, "ins": ins,
               "reference": stats(ref), "muninn": stats(hyp),
               "muninn_seconds": round(hyp["seconds"], 1), "reference_seconds": round(ref["seconds"], 1)}
        report["clips"].append(row)
        print("%-22s WER %5.1f%%  (sub %d, missing %d, extra %d)  words %d vs ref %d  "
              "repeats %d vs %d  short %d vs %d  %.0fs" % (
                  clip["name"], row["wer"], sub, dele, ins, row["muninn"]["words"], row["reference"]["words"],
                  row["muninn"]["repeated_lines"], row["reference"]["repeated_lines"],
                  row["muninn"]["short_lines"], row["reference"]["short_lines"], hyp["seconds"]), flush=True)
    json.dump(report, open(os.path.join(RESULTS, "%s_%s.json" % (time.strftime("%Y-%m-%d"), args.label)), "w"),
              indent=1)


if __name__ == "__main__":
    sys.exit(main())
