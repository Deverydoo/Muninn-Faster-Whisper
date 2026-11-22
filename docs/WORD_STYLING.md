# Word Styling

`word_styling.h` turns word timestamps into styled text for OBS text sources (HTML) or a terminal (ANSI colours). It's header-only.

What Muninn gives each word:

```cpp
struct Word {
    float start;             // Start time (seconds)
    float end;               // End time (seconds)
    std::string word;        // Word text
    float probability;       // Confidence (0.0-1.0)
};
```

Muninn doesn't measure how loud a word is. Earlier versions had `intensity` and `emphasis` fields on `Word`, but nothing ever filled them in, so they were removed in November 2026. The `EmphasisLevel` enum and the presets that take one are still there for callers who work out emphasis themselves.

## Colour schemes (`StylingPresets`)

### Karaoke (by position)

```cpp
using Pos = muninn::StylingPresets::Position;
muninn::WordStyle style = muninn::StylingPresets::karaoke_scheme(
    is_past ? Pos::Past : (is_current ? Pos::Current : Pos::Future));
```

- Past: `#888888` (dim gray)
- Current: `#FFFF00` (yellow, bold)
- Future: `#FFFFFF` (white)

### Confidence

```cpp
muninn::WordStyle style = muninn::StylingPresets::confidence_color_scheme(word.probability, is_current);
```

- Below 60%: `#FF3333` (red), italic
- 60-80%: `#FFFF00` (yellow)
- Above 80%: `#00FF00` (green)

Useful for proofreading: the words Whisper was unsure of stand out.

### Emphasis (you supply the level)

```cpp
muninn::EmphasisLevel level = my_loudness_to_level(word);  // your own measurement
muninn::WordStyle style = muninn::StylingPresets::intensity_color_scheme(level, is_current);
muninn::WordStyle both  = muninn::StylingPresets::combined_scheme(level, word.probability, is_current);
```

- VeryLow `#888888`, Low `#AAAAAA`, Normal `#FFFFFF`, High `#FFFF00` (larger when current), VeryHigh `#FF3333` (largest when current)
- `combined_scheme` adds italic for low confidence and a dark red background for a VeryHigh, confident, current word

## HTML for OBS (`HTMLFormatter`)

```cpp
#include <muninn/word_styling.h>

// Past words dimmed, the current word highlighted
std::string html = muninn::HTMLFormatter::build_karaoke_html(segment, current_time);

// Coloured by confidence
std::string html = muninn::HTMLFormatter::build_confidence_html(segment, current_time);

// One word with any style
std::string one = muninn::HTMLFormatter::format_word(word.word, style);
```

Word text is escaped (`& < > " '`), so a transcribed "<3" or "AT&T" shows as written instead of breaking the markup. `HTMLFormatter::escape()` is there if you build your own HTML.

`build_combined_html` was removed along with the emphasis fields.

## ANSI for terminals (`ANSIFormatter`)

```cpp
std::cout << muninn::ANSIFormatter::build_karaoke_ansi(segment, current_time) << "\n";
std::cout << muninn::ANSIFormatter::build_confidence_ansi(segment, current_time) << "\n";
```

The karaoke version dims past words and shows the current one in bold yellow.

## Custom styles

```cpp
muninn::WordStyle my_style(const muninn::Word& word, bool is_current) {
    muninn::WordStyle style{};
    if (word.probability < 0.5f) {
        style.color = "#FFA500";   // orange for uncertain
        style.italic = true;
    } else {
        style.color = "#FFFFFF";
        style.bold = is_current;
    }
    return style;
}

std::string html;
for (const auto& word : segment.words) {
    bool is_current = (time >= word.start && time < word.end);
    html += muninn::HTMLFormatter::format_word(word.word, my_style(word, is_current)) + " ";
}
```

## WordStyle

```cpp
struct WordStyle {
    std::string color;          // Hex color (#RRGGBB)
    bool bold;                  // Bold text
    bool italic;                // Italic text
    int size_offset;            // Font size offset (-2 to +2)
    std::string background;     // Background color (empty = none)
};
```

## Word timestamps

Word styling needs word timestamps:

```cpp
muninn::TranscribeOptions options;
options.word_timestamps = true;
```

## See also

- [OBS Integration Guide](OBS_KARAOKE_CAPTIONS.md)
- [Subtitle Export](SUBTITLE_EXPORT.md)
