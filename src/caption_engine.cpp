#include "muninn/caption_engine.h"
#include <algorithm>

namespace muninn {

// ═══════════════════════════════════════════════════════════════════════════
// ICaptionEngine - High-Level API Implementation
// ═══════════════════════════════════════════════════════════════════════════

std::vector<Word> ICaptionEngine::extractWords(const std::vector<Segment>& segments) const {
    std::vector<Word> words;

    for (const auto& seg : segments) {
        for (const auto& word : seg.words) {
            words.push_back(word);
        }
    }

    return words;
}

std::vector<PresentationEvent> ICaptionEngine::planAllPresentation(
    const std::vector<CaptionCue>& cues,
    const PresentationOptions& options) const {

    std::vector<PresentationEvent> allEvents;

    for (const auto& cue : cues) {
        auto cueEvents = planPresentation(cue, options);
        allEvents.insert(allEvents.end(), cueEvents.begin(), cueEvents.end());
    }

    // Sort all events by time
    std::sort(allEvents.begin(), allEvents.end());

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
