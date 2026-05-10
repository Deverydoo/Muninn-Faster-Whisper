# Word Styling & Intensity-Based Highlighting

Muninn's advanced word styling system provides dynamic visual feedback based on audio characteristics and transcription confidence.

## Features

✅ **Intensity-Based Coloring** - Words change color based on volume/emphasis
✅ **Confidence Visualization** - Show transcription certainty with colors
✅ **Multiple Presets** - Built-in color schemes for different use cases
✅ **HTML & ANSI Formatters** - Ready for OBS and terminal applications
✅ **Customizable Styling** - Full control over colors, fonts, and effects

## Word Metadata

Each word contains rich metadata for styling:

```cpp
struct Word {
    std::string word;        // Word text
    float start;             // Start time (seconds)
    float end;               // End time (seconds)
    float probability;       // Confidence (0.0-1.0)
    float intensity;         // Audio volume/RMS (0.0-1.0)
    EmphasisLevel emphasis;  // Categorized emphasis level
};
```

### Emphasis Levels

Words are automatically categorized into 5 emphasis levels:

| Level | Intensity Range | Description | Example |
|-------|----------------|-------------|---------|
| **VeryLow** | < 20% | Whispered, very quiet | Background speech |
| **Low** | 20-40% | Quiet speech | Calm narration |
| **Normal** | 40-70% | Normal conversation | Standard speech |
| **High** | 70-90% | Emphasized, louder | Excited speech |
| **VeryHigh** | > 90% | Shouted, very loud | Yelling, music |

## Color Schemes

### 1. Intensity-Based (Recommended)

Colors reflect the speaker's volume and emphasis:

```cpp
#include <muninn/word_styling.h>

muninn::WordStyle style = muninn::StylingPresets::intensity_color_scheme(
    word.emphasis,
    is_current  // true if this is the current word
);
```

**Color Map:**
- **VeryLow** (whispered): `#888888` (dim gray)
- **Low** (quiet): `#AAAAAA` (light gray)
- **Normal**: `#FFFFFF` (white)
- **High** (emphasized): `#FFFF00` (yellow) + larger size
- **VeryHigh** (shouted): `#FF3333` (red) + largest size

**Use Cases:**
- Live streaming with dynamic energy
- Music/karaoke visualization
- Accessibility (visual volume indicator)
- Sports commentary

### 2. Confidence-Based

Colors show transcription certainty:

```cpp
muninn::WordStyle style = muninn::StylingPresets::confidence_color_scheme(
    word.probability,
    is_current
);
```

**Color Map:**
- **Low confidence** (< 60%): `#FF3333` (red) + italic
- **Medium confidence** (60-80%): `#FFFF00` (yellow)
- **High confidence** (> 80%): `#00FF00` (green)

**Use Cases:**
- Quality monitoring
- Proofreading transcripts
- Identifying uncertain sections
- Technical/medical transcription

### 3. Combined Intensity + Confidence

Best of both worlds:

```cpp
muninn::WordStyle style = muninn::StylingPresets::combined_scheme(
    word.emphasis,
    word.probability,
    is_current
);
```

**Features:**
- Uses intensity for color (shouted = red)
- Adds italic for low confidence
- Background highlight for high-emphasis + high-confidence words

**Use Cases:**
- Professional live captions
- High-quality content production
- Detailed analysis

### 4. Classic Karaoke (Position-Based)

Simple 3-color scheme based on timing:

```cpp
using Pos = muninn::StylingPresets::Position;

muninn::WordStyle style = muninn::StylingPresets::karaoke_scheme(
    is_past ? Pos::Past : (is_current ? Pos::Current : Pos::Future)
);
```

**Color Map:**
- **Past**: `#888888` (dim gray)
- **Current**: `#FFFF00` (yellow, bold)
- **Future**: `#FFFFFF` (white)

**Use Cases:**
- Simple karaoke
- Reading assistance
- Language learning

## HTML Formatting (OBS)

For OBS text sources (GDI+/FreeType):

```cpp
#include <muninn/word_styling.h>

// Option 1: Built-in intensity-based formatter
std::string html = muninn::HTMLFormatter::build_karaoke_html(segment, current_time);

// Option 2: Confidence-based formatter
std::string html = muninn::HTMLFormatter::build_confidence_html(segment, current_time);

// Option 3: Combined formatter
std::string html = muninn::HTMLFormatter::build_combined_html(segment, current_time);

// Update OBS text source
obs_data_t* settings = obs_data_create();
obs_data_set_string(settings, "text", html.c_str());
obs_source_update(text_source, settings);
obs_data_release(settings);
```

### HTML Output Examples

**Intensity-Based:**
```html
<font color='#888888'>I</font>
<font color='#AAAAAA'>am</font>
<font color='#FFFF00'><b>VERY</b></font>
<font color='#FF3333' size='+2'><b>EXCITED!</b></font>
```

**Confidence-Based:**
```html
<font color='#00FF00'>clear</font>
<font color='#FFFF00'>moderate</font>
<font color='#FF3333'><i>uncertain</i></font>
```

## ANSI Terminal Formatting

For console applications:

```cpp
#include <muninn/word_styling.h>

// Option 1: Intensity-based ANSI
std::string output = muninn::ANSIFormatter::build_karaoke_ansi(segment, current_time);
std::cout << output << std::endl;

// Option 2: Confidence-based ANSI
std::string output = muninn::ANSIFormatter::build_confidence_ansi(segment, current_time);
std::cout << output << std::endl;
```

### ANSI Output Examples

**Intensity-Based:**
```
\033[2m\033[90mwhispered\033[0m \033[97mnormal\033[0m \033[1m\033[93mEMPHASIZED\033[0m \033[1m\033[91mSHOUTED!\033[0m
```

Renders as: <span style="color:#666">whispered</span> normal **<span style="color:#fc3">EMPHASIZED</span>** **<span style="color:#f33">SHOUTED!</span>**

## Custom Styling

For complete control, build custom styles:

```cpp
muninn::WordStyle create_custom_style(const muninn::Word& word, bool is_current) {
    muninn::WordStyle style;

    // Custom color logic
    if (word.emphasis == muninn::EmphasisLevel::VeryHigh) {
        style.color = "#FF0000";        // Custom red
        style.bold = true;
        style.size_offset = 2;
        style.background = "#330000";   // Dark red background
    } else if (word.probability < 0.5f) {
        style.color = "#FFA500";        // Orange for uncertain
        style.italic = true;
    } else {
        style.color = "#FFFFFF";        // White default
        style.bold = is_current;
    }

    return style;
}

// Use with formatter
std::string html;
for (const auto& word : segment.words) {
    bool is_current = (time >= word.start && time < word.end);
    auto style = create_custom_style(word, is_current);
    html += muninn::HTMLFormatter::format_word(word.word, style) + " ";
}
```

## WordStyle API

```cpp
struct WordStyle {
    std::string color;          // Hex color (#RRGGBB)
    bool bold;                  // Bold text
    bool italic;                // Italic text
    int size_offset;            // Font size offset (-2 to +2)
    std::string background;     // Background color (empty = none)
};
```

### Manual Formatting

```cpp
muninn::WordStyle style;
style.color = "#FFFF00";
style.bold = true;
style.italic = false;
style.size_offset = 1;
style.background = "";

// HTML
std::string html = muninn::HTMLFormatter::format_word("WORD", style);
// Output: <font color='#FFFF00' size='+1'><b>WORD</b></font>
```

## Configuration Options

Enable word timestamps and intensity analysis in streaming options:

```cpp
muninn::StreamingOptions options;
options.word_timestamps = true;  // Required for word-level styling
```

Intensity is automatically calculated from audio RMS during transcription.

## Performance Notes

- **Intensity calculation**: ~1-2% overhead (negligible)
- **HTML formatting**: < 1ms per segment
- **ANSI formatting**: < 1ms per segment
- **Memory**: +8 bytes per word (intensity + emphasis)

## Visual Examples

### Podcast Transcript (Intensity-Based)

```
Welcome to the show. Today we're discussing EXCITING topics!
(shouted word "EXCITING" appears in bright red)
```

### Technical Presentation (Confidence-Based)

```
The algorithm uses tensor decomposition...
(uncertain technical term appears in yellow/red with italics)
```

### Live Gaming Commentary (Combined)

```
He's going for it... HE SCORES!!!
(loud exclamation in red with high confidence = no italic)
```

## Best Practices

1. **Choose the right scheme:**
   - Intensity: Dynamic content (gaming, music, sports)
   - Confidence: Professional transcription (meetings, lectures)
   - Combined: High-quality productions

2. **Test with your content:**
   - Adjust intensity thresholds if needed
   - Verify colors are readable on your background

3. **OBS Settings:**
   - Use GDI+ or FreeType2 text source
   - Enable "Read from file" for live updates
   - Set contrasting background color

4. **Accessibility:**
   - Ensure sufficient color contrast
   - Test with colorblind-friendly palettes
   - Provide alternative highlighting (size, bold)

## API Summary

| Class | Purpose | Output |
|-------|---------|--------|
| `StylingPresets` | Pre-built color schemes | `WordStyle` |
| `HTMLFormatter` | OBS text source formatting | HTML string |
| `ANSIFormatter` | Terminal output formatting | ANSI string |

## See Also

- [Streaming API Guide](STREAMING_API.md)
- [OBS Integration Guide](OBS_KARAOKE_CAPTIONS.md)
- [Integration Guide](INTEGRATION.md)

---

**Ready to create visually dynamic captions!** Use `word_styling.h` for instant intensity-based highlighting.
