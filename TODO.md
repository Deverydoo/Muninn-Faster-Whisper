# Muninn Faster-Whisper - Feature Parity with faster-whisper

## Reference
- faster-whisper source: https://github.com/SYSTRAN/faster-whisper/blob/master/faster_whisper/transcribe.py

---

## ✅ COMPLETED FEATURES

| Feature | Status | Notes |
|---------|--------|-------|
| Silero VAD | ✅ Done | Neural VAD with ONNX Runtime, CPU default (faster than GPU for small batches) |
| Beam Search | ✅ Done | Default beam_size=5, matching faster-whisper |
| Hallucination Detection | ✅ Done | Phrase repetition, compression ratio, no-speech prob, cross-chunk detection |
| Batch Processing | ✅ Done | GPU parallel chunks (batch_size=4) |
| Multi-track Audio | ✅ Done | Via Heimdall library |
| Timestamp Remapping | ✅ Done | VAD-filtered → original timeline mapping |
| Basic Thresholds | ✅ Done | compression_ratio_threshold=2.4, log_prob_threshold=-1.0, no_speech_threshold=0.4 |
| Energy VAD Fallback | ✅ Done | Falls back to energy-based VAD if Silero unavailable |

---

## 🔴 HIGH PRIORITY - Quality Impact

### [x] 1. Temperature Fallback Strategy
**Impact: HIGH | Complexity: Medium | ✅ DONE**

Implemented:
- Added `std::vector<float> temperature_fallback = {0.0, 0.2, 0.4, 0.6, 0.8, 1.0}` to TranscribeOptions
- Modified `transcribe_chunk()` to retry with higher temperatures when:
  - `compression_ratio > compression_ratio_threshold` (default 2.4), OR
  - `avg_logprob < log_prob_threshold` (default -1.0)
- Segment.temperature now tracks which temperature produced the result
- Log messages show temperature fallback decisions

---

### [x] 2. Condition on Previous Text
**Impact: HIGH | Complexity: Low | ✅ DONE**

Implemented:
- Added `condition_on_previous` option (default true)
- Added `prompt_reset_on_temperature` option (default 0.5)
- Added `initial_prompt` support for domain-specific vocabulary
- Previous text appended to prompt using `<|startofprev|>` token format
- Context trimmed to ~1000 chars to avoid context overflow

Note: Full cross-chunk context conditioning requires sequential processing.
Currently implemented for single-chunk mode and initial_prompt support.

---

### [x] 3. Language Auto-Detection
**Impact: HIGH | Complexity: Medium | ✅ DONE**

Implemented:
- When `language="auto"`, use CTranslate2's `detect_language()` method
- Process first 30s of mel-spectrogram features for detection
- Set `result.language` and `result.language_probability` from detection
- Falls back to "en" if detection fails
- Works only for multilingual models (checked via `is_multilingual()`)

---

## 🟡 MEDIUM PRIORITY - Edge Cases

### [x] 4. Hallucination Silence Threshold
**Impact: Medium | Complexity: Low | ✅ DONE**

Implemented:
- Added `hallucination_silence_threshold` option (default 0.0 = disabled)
- Cross-references VAD speech segments with transcription timestamps
- Calculates overlap fraction between segment and speech regions
- Filters segments where overlap < threshold (e.g., 0.5 = 50% minimum overlap)
- Applied after timestamp remapping to original timeline

---

### [x] 5. Word-Level Timestamps (Timestamp Token Parsing)
**Impact: Medium | Complexity: Medium | ✅ DONE**

Implemented approach:
- Parse Whisper's timestamp tokens (`<|0.00|>`, `<|2.50|>`, etc.) from output
- Create sub-segments with accurate timing from Whisper's internal timestamps
- Word-level timing estimated by proportional distribution within segments
- Segment struct `words` vector populated when `word_timestamps=true`

Note: Cross-attention-based alignment would give even more precise word timing,
but timestamp token parsing provides good accuracy with much simpler implementation.

---

## 🟢 LOW PRIORITY - Nice to Have

### [x] 6. Prompt Reset on High Temperature
**Impact: Low | Complexity: Low | ✅ DONE**

Implemented:
- Added previous_temperature parameter to transcribe_chunk()
- Checks if previous segment temperature >= prompt_reset_on_temperature threshold (default 0.5)
- If so, resets prompt context to prevent error propagation from difficult segments
- Logs when prompt reset is triggered

---

### [ ] 7. Hotwords / Prefix Support
**Impact: Low | Complexity: Medium**

faster-whisper behavior:
- `hotwords` parameter boosts recognition of specific words
- `prefix` parameter prepends text to generation

Implementation:
- Already have `hotwords` in TranscribeOptions
- Need to implement actual boosting via logit bias or prompt engineering

---

### [x] 8. Initial Prompt Support
**Impact: Low | Complexity: Low | ✅ DONE**

Implemented:
- `initial_prompt` is passed to CTranslate2 generation via `<|startofprev|>` token format
- Used for domain-specific vocabulary, technical content, proper nouns
- Combined with condition_on_previous for continuous context conditioning

---

### [x] 9. Suppress Tokens
**Impact: Low | Complexity: Low | ✅ DONE**

Implemented:
- `suppress_blank` option exposed in TranscribeOptions (default true)
- `suppress_tokens` option exposed in TranscribeOptions (default {-1} for model defaults)
- Both options now passed to CTranslate2 WhisperOptions instead of hardcoded values

---

### [x] 10. Clip Timestamps
**Impact: Low | Complexity: Low | ✅ DONE**

Implemented:
- Added `clip_start` (default 0.0) and `clip_end` (default -1.0 = full audio) to TranscribeOptions
- Audio samples sliced before VAD processing based on clip range
- Final segment timestamps adjusted by clip_offset to reflect original timeline
- Word timestamps also adjusted when present

---

## Implementation Order (Recommended)

1. **Temperature Fallback** - Biggest quality win, retry failed segments
2. **Condition on Previous** - Better consistency, easy to implement
3. **Language Detection** - Essential for multilingual, uses existing CTranslate2 API
4. **Hallucination Silence** - Catches edge cases cheaply
5. **Initial Prompt** - Quick win, already have option defined
6. **Word Timestamps** - Complex but valuable for some use cases

---

## Notes

- CTranslate2 API docs: https://opennmt.net/CTranslate2/
- faster-whisper uses `WhisperModel.generate()` which maps to our `model->generate()`
- Cross-attention for word timestamps may require CTranslate2 modifications or alternative approach
