#pragma once

#include "types.h"
#include <string>
#include <sstream>

namespace muninn {

/**
 * @brief Color styling for word highlighting
 */
struct WordStyle {
    std::string color;          // Hex color (#RRGGBB)
    bool bold;                  // Bold text
    bool italic;                // Italic text
    int size_offset;            // Font size offset (-2 to +2)
    std::string background;     // Background color (empty = none)
};

/**
 * @brief Predefined color schemes for different emphasis levels
 */
class StylingPresets {
public:
    /**
     * @brief Intensity-based color scheme (volume/loudness)
     *
     * VeryLow (whispered):  Dim gray (#888888)
     * Low (quiet):          Light gray (#AAAAAA)
     * Normal:               White (#FFFFFF)
     * High (emphasized):    Yellow (#FFFF00)
     * VeryHigh (shouted):   Red (#FF3333)
     */
    static WordStyle intensity_color_scheme(EmphasisLevel emphasis, bool is_current = false) {
        WordStyle style;
        style.bold = is_current;
        style.italic = false;
        style.size_offset = 0;

        switch (emphasis) {
            case EmphasisLevel::VeryLow:
                style.color = "#888888";  // Dim gray (whispered)
                break;
            case EmphasisLevel::Low:
                style.color = "#AAAAAA";  // Light gray (quiet)
                break;
            case EmphasisLevel::Normal:
                style.color = "#FFFFFF";  // White (normal)
                break;
            case EmphasisLevel::High:
                style.color = "#FFFF00";  // Yellow (emphasized)
                if (is_current) style.size_offset = 1;
                break;
            case EmphasisLevel::VeryHigh:
                style.color = "#FF3333";  // Red (shouted)
                if (is_current) style.size_offset = 2;
                break;
        }

        return style;
    }

    /**
     * @brief Confidence-based color scheme (transcription accuracy)
     *
     * Low confidence (< 0.6):  Red (#FF3333)
     * Medium confidence (0.6-0.8): Yellow (#FFFF00)
     * High confidence (> 0.8):     Green (#00FF00)
     */
    static WordStyle confidence_color_scheme(float probability, bool is_current = false) {
        WordStyle style;
        style.bold = is_current;
        style.italic = probability < 0.6f;  // Italicize uncertain words
        style.size_offset = 0;

        if (probability < 0.6f) {
            style.color = "#FF3333";  // Red (uncertain)
        } else if (probability < 0.8f) {
            style.color = "#FFFF00";  // Yellow (medium confidence)
        } else {
            style.color = "#00FF00";  // Green (high confidence)
        }

        return style;
    }

    /**
     * @brief Combined intensity + confidence scheme
     *
     * Uses intensity for color, confidence for styling effects
     */
    static WordStyle combined_scheme(EmphasisLevel emphasis, float probability, bool is_current = false) {
        WordStyle style = intensity_color_scheme(emphasis, is_current);

        // Add italic for low confidence
        if (probability < 0.6f) {
            style.italic = true;
        }

        // Add background highlight for very high emphasis + high confidence
        if (emphasis == EmphasisLevel::VeryHigh && probability > 0.8f && is_current) {
            style.background = "#330000";  // Dark red background
        }

        return style;
    }

    /**
     * @brief Classic karaoke scheme (position-based)
     *
     * Past words:    Dim gray
     * Current word:  Yellow/bold
     * Future words:  White
     */
    enum class Position { Past, Current, Future };

    static WordStyle karaoke_scheme(Position pos) {
        WordStyle style;
        style.italic = false;
        style.size_offset = 0;

        switch (pos) {
            case Position::Past:
                style.color = "#888888";
                style.bold = false;
                break;
            case Position::Current:
                style.color = "#FFFF00";
                style.bold = true;
                break;
            case Position::Future:
                style.color = "#FFFFFF";
                style.bold = false;
                break;
        }

        return style;
    }
};

/**
 * @brief HTML formatting helper for OBS text sources
 */
class HTMLFormatter {
public:
    /**
     * @brief Format a word with HTML styling
     */
    static std::string format_word(const std::string& word, const WordStyle& style) {
        std::ostringstream html;

        // Background
        if (!style.background.empty()) {
            html << "<span style='background-color: " << style.background << "; padding: 2px;'>";
        }

        // Color
        html << "<font color='" << style.color << "'";

        // Size
        if (style.size_offset != 0) {
            html << " size='" << (style.size_offset > 0 ? "+" : "") << style.size_offset << "'";
        }

        html << ">";

        // Bold
        if (style.bold) html << "<b>";

        // Italic
        if (style.italic) html << "<i>";

        // Word text
        html << word;

        // Close tags
        if (style.italic) html << "</i>";
        if (style.bold) html << "</b>";
        html << "</font>";

        if (!style.background.empty()) {
            html << "</span>";
        }

        return html.str();
    }

    /**
     * @brief Build karaoke HTML with intensity-based coloring
     */
    static std::string build_karaoke_html(const Segment& seg, float current_time) {
        std::ostringstream html;

        for (const auto& word : seg.words) {
            bool is_current = (current_time >= word.start && current_time < word.end);
            bool is_past = (current_time > word.end);

            WordStyle style;

            if (is_past) {
                // Past words - dimmed
                style = StylingPresets::karaoke_scheme(StylingPresets::Position::Past);
            } else if (is_current) {
                // Current word - intensity-based color
                style = StylingPresets::intensity_color_scheme(word.emphasis, true);
            } else {
                // Future words - normal
                style = StylingPresets::karaoke_scheme(StylingPresets::Position::Future);
            }

            html << format_word(word.word, style) << " ";
        }

        return html.str();
    }

    /**
     * @brief Build HTML with confidence-based coloring
     */
    static std::string build_confidence_html(const Segment& seg, float current_time) {
        std::ostringstream html;

        for (const auto& word : seg.words) {
            bool is_current = (current_time >= word.start && current_time < word.end);
            WordStyle style = StylingPresets::confidence_color_scheme(word.probability, is_current);
            html << format_word(word.word, style) << " ";
        }

        return html.str();
    }

    /**
     * @brief Build HTML with combined intensity + confidence
     */
    static std::string build_combined_html(const Segment& seg, float current_time) {
        std::ostringstream html;

        for (const auto& word : seg.words) {
            bool is_current = (current_time >= word.start && current_time < word.end);

            if (current_time > word.end) {
                // Past words - always dimmed
                WordStyle style = StylingPresets::karaoke_scheme(StylingPresets::Position::Past);
                html << format_word(word.word, style) << " ";
            } else {
                // Current/future - use combined scheme
                WordStyle style = StylingPresets::combined_scheme(word.emphasis, word.probability, is_current);
                html << format_word(word.word, style) << " ";
            }
        }

        return html.str();
    }
};

/**
 * @brief ANSI terminal formatting helper for console apps
 */
class ANSIFormatter {
public:
    // ANSI color codes
    static constexpr const char* RESET = "\033[0m";
    static constexpr const char* BOLD = "\033[1m";
    static constexpr const char* DIM = "\033[2m";
    static constexpr const char* ITALIC = "\033[3m";

    // Colors
    static constexpr const char* GRAY = "\033[90m";
    static constexpr const char* WHITE = "\033[97m";
    static constexpr const char* YELLOW = "\033[93m";
    static constexpr const char* RED = "\033[91m";
    static constexpr const char* GREEN = "\033[92m";

    /**
     * @brief Build karaoke output with ANSI colors (intensity-based)
     */
    static std::string build_karaoke_ansi(const Segment& seg, float current_time) {
        std::ostringstream out;

        for (const auto& word : seg.words) {
            bool is_current = (current_time >= word.start && current_time < word.end);
            bool is_past = (current_time > word.end);

            if (is_past) {
                // Past - dimmed
                out << DIM << word.word << RESET << " ";
            } else if (is_current) {
                // Current - intensity-based color
                switch (word.emphasis) {
                    case EmphasisLevel::VeryLow:
                        out << DIM << GRAY << word.word << RESET << " ";
                        break;
                    case EmphasisLevel::Low:
                        out << WHITE << word.word << RESET << " ";
                        break;
                    case EmphasisLevel::Normal:
                        out << BOLD << WHITE << word.word << RESET << " ";
                        break;
                    case EmphasisLevel::High:
                        out << BOLD << YELLOW << word.word << RESET << " ";
                        break;
                    case EmphasisLevel::VeryHigh:
                        out << BOLD << RED << word.word << RESET << " ";
                        break;
                }
            } else {
                // Future - normal
                out << word.word << " ";
            }
        }

        return out.str();
    }

    /**
     * @brief Build output with confidence-based ANSI colors
     */
    static std::string build_confidence_ansi(const Segment& seg, float current_time) {
        std::ostringstream out;

        for (const auto& word : seg.words) {
            bool is_current = (current_time >= word.start && current_time < word.end);

            // Color by confidence
            const char* color;
            if (word.probability < 0.6f) {
                color = RED;
            } else if (word.probability < 0.8f) {
                color = YELLOW;
            } else {
                color = GREEN;
            }

            if (is_current) {
                out << BOLD << color << word.word << RESET << " ";
            } else {
                out << color << word.word << RESET << " ";
            }
        }

        return out.str();
    }
};

} // namespace muninn
