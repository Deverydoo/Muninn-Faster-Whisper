#include "muninn/caption_engine.h"
#include <algorithm>
#include <limits>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// ICaptionEngine - High-Level API Implementation
// ═══════════════════════════════════════════════════════════════════════════

std::vector<Word> ICaptionEngine::extractWords(const std::vector<Segment>& segments) const {
    // Segments in time order (multi-track results come track by track).
    std::vector<const Segment*> ordered;
    ordered.reserve(segments.size());
    for (const auto& seg : segments) ordered.push_back(&seg);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Segment* a, const Segment* b) { return a->start < b->start; });

    std::vector<Word> words;
    for (const Segment* seg : ordered) {
        words.insert(words.end(), seg->words.begin(), seg->words.end());
    }

    return words;
}

std::vector<PresentationEvent> ICaptionEngine::planAllPresentation(
    const std::vector<CaptionCue>& cues,
    const PresentationOptions& options) const {

    std::vector<std::vector<PresentationEvent>> perCue;
    perCue.reserve(cues.size());
    for (const auto& cue : cues) {
        perCue.push_back(planPresentation(cue, options));
    }

    auto eventTime = [](const std::vector<PresentationEvent>& events, PresentationEventType type,
                        float fallback) {
        for (const auto& e : events) {
            if (e.type == type) return e.time;
        }
        return fallback;
    };

    // One cue on screen at a time. The lead-in of a cue may not cover the
    // previous cue's last word, and a cue's hold, fade and clear end when the
    // next cue appears.
    for (size_t i = 1; i < perCue.size(); ++i) {
        if (cues[i - 1].tokens.empty() || cues[i].tokens.empty()) continue;
        const float previousSpeechEnd = cues[i - 1].tokens.back().end;
        const float firstToken = cues[i].tokens.front().start;
        const float earliestShow = std::min(previousSpeechEnd, firstToken);
        for (auto& e : perCue[i]) {
            if ((e.type == PresentationEventType::CueShow ||
                 e.type == PresentationEventType::TokenReveal) && e.time < earliestShow) {
                e.time = earliestShow;
            }
        }
    }
    for (size_t i = 0; i + 1 < perCue.size(); ++i) {
        const float nextShow = eventTime(perCue[i + 1], PresentationEventType::CueShow,
                                         std::numeric_limits<float>::infinity());
        const float show = eventTime(perCue[i], PresentationEventType::CueShow, 0.0f);
        const float clear = eventTime(perCue[i], PresentationEventType::CueClear, nextShow);
        if (clear <= nextShow) continue;

        const float newClear = std::max(show, nextShow);
        for (auto& e : perCue[i]) {
            if (e.type == PresentationEventType::CueClear) {
                e.time = newClear;
            } else if (e.type == PresentationEventType::CueFadeOut) {
                e.time = std::max(show, std::min(e.time, newClear - e.duration));
                e.duration = newClear - e.time;
            }
        }
        const float fade = eventTime(perCue[i], PresentationEventType::CueFadeOut, newClear);
        for (auto& e : perCue[i]) {
            if (e.type == PresentationEventType::CueHold && e.time > fade) e.time = fade;
        }
    }

    std::vector<PresentationEvent> allEvents;
    for (const auto& events : perCue) {
        allEvents.insert(allEvents.end(), events.begin(), events.end());
    }

    // Sort all events by time
    std::stable_sort(allEvents.begin(), allEvents.end());

    return allEvents;
}

CaptionDocument ICaptionEngine::process(
    const std::vector<Segment>& segments,
    const CaptionEngineOptions& options) const {

    CaptionDocument doc;

    // Set metadata
    doc.metadata.language = options.languageHint;
    doc.metadata.script = options.scriptHint;
    doc.metadata.engineFamily = engineFamily();
    doc.metadata.engineVersion = engineVersion();
    doc.metadata.confidence = 1.0f;

    // Extract all words
    std::vector<Word> words = extractWords(segments);

    if (words.empty()) {
        return doc;
    }

    // Calculate total duration
    doc.totalDuration = words.back().end;
    doc.lastSpeechEnd = words.back().end;

    // Tokenize
    doc.tokens = tokenize(words, options);

    // Build cues
    doc.cues = buildCues(doc.tokens, options);

    // Plan presentation (if enabled)
    if (options.generatePresentationEvents) {
        doc.events = planAllPresentation(doc.cues, options.presentation);
    }

    return doc;
}

} // namespace muninn
