#pragma once

// Internal text helpers for the translator: language codes, sentence splitting
// and output clean-up. Not part of the public API. They have no dependencies
// (no CTranslate2, no SentencePiece), so tests/test_translate_text.cpp compiles
// translate_text.cpp directly and checks them without a model.

#include <string>
#include <vector>

namespace muninn {
namespace detail {

// Language codes -----------------------------------------------------------

// Lower-cases and trims a code and turns '_' into '-' ("pt_BR" -> "pt-br").
std::string canonical_language_code(const std::string& code);

// The NLLB code for a Whisper/short code, or "" when it isn't mapped.
// Case and region are normalised: "EN", "en-US" and "en_us" all give
// "eng_Latn". A region-specific mapping wins over the base language
// ("zh-TW" -> "zho_Hant", "zh-CN" -> "zho_Hans").
std::string nllb_code_for(const std::string& code);

// The short code for an NLLB code ("fra_Latn" -> "fr"), or "".
std::string short_code_for_nllb(const std::string& nllb_code);

// Text ---------------------------------------------------------------------

// True when the text is empty or only whitespace (ASCII whitespace, no-break
// space U+00A0, ideographic space U+3000).
bool is_blank(const std::string& text);

// Splits text into sentences. Not a boundary: a dot between digits ("3.5"),
// a dot inside a word ("file.txt", "example.com"), single-letter
// abbreviations ("e.g.", "U.S."), common abbreviations ("Mr.", "Dr.", "vs.",
// "etc."), and a dot followed by a lower-case word. "!" and "?" end a sentence
// only when whitespace or the end follows. CJK 。！？ always end one.
std::vector<std::string> split_sentences(const std::string& text);

// Joins translated sentences with one space, or none for targets written
// without spaces (Chinese, Japanese, Thai).
std::string join_sentences(const std::vector<std::string>& sentences,
                           const std::string& target_nllb);

// Tidies NLLB output for the target language (an NLLB code such as
// "fra_Latn"): drops a leading target-language token, trims, removes a stray
// space before sentence punctuation. French keeps its space before ; : ! ?.
// Words that look like URLs, paths, file names or numbers are never changed.
std::string clean_output(const std::string& text, const std::string& target_nllb);

// Cuts text to at most max_bytes without splitting a UTF-8 sequence, adding
// "..." when it cut anything. For log lines.
std::string utf8_truncate(const std::string& text, size_t max_bytes);

} // namespace detail
} // namespace muninn
