// Turning Whisper's output tokens back into text. See text_decode.h.
//
// GPT2ByteDecoder, get_gpt2_decoder(), clean_token(), clean_token_raw(),
// extract_text() and compression_ratio_of() are copies of the functions of the
// same names in transcriber.cpp (extract_text was Transcriber::Impl's member),
// so the offline path can switch to this file without any change in output.
// The word splitting and punctuation merging are ports of faster-whisper's
// split_to_word_tokens and merge_punctuations.

#include "text_decode.h"

#include <algorithm>
#include <cstring>
#include <set>

#if __has_include(<zlib.h>)
#include <zlib.h>   // compression ratio, as faster-whisper measures it
#define MUNINN_TEXT_HAVE_ZLIB 1
#else
#define MUNINN_TEXT_HAVE_ZLIB 0
#endif

namespace muninn {
namespace text {

// Builds the same table as Python's bytes_to_unicode(), reversed: printable
// ASCII and most of Latin-1 stand for themselves; every other byte is stored
// as U+0100 onwards.
GPT2ByteDecoder::GPT2ByteDecoder() {
    std::vector<int> bs;
    std::vector<int> cs;

    for (int b = '!'; b <= '~'; b++) bs.push_back(b);
    for (int b = 0xA1; b <= 0xAC; b++) bs.push_back(b);
    for (int b = 0xAE; b <= 0xFF; b++) bs.push_back(b);

    cs = bs;

    int n = 0;
    for (int b = 0; b < 256; b++) {
        if (std::find(bs.begin(), bs.end(), b) == bs.end()) {
            bs.push_back(b);
            cs.push_back(256 + n);
            n++;
        }
    }

    for (size_t i = 0; i < bs.size(); i++) {
        decoder_[cs[i]] = static_cast<uint8_t>(bs[i]);
    }
}

std::string GPT2ByteDecoder::decode(const std::string& token) const {
    std::vector<uint8_t> bytes;
    bytes.reserve(token.size());

    size_t i = 0;
    while (i < token.size()) {
        uint32_t codepoint = 0;
        uint8_t c = static_cast<uint8_t>(token[i]);

        if ((c & 0x80) == 0) {
            codepoint = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            codepoint = (c & 0x1F) << 6;
            if (i + 1 < token.size()) {
                codepoint |= (static_cast<uint8_t>(token[i + 1]) & 0x3F);
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            codepoint = (c & 0x0F) << 12;
            if (i + 1 < token.size()) {
                codepoint |= (static_cast<uint8_t>(token[i + 1]) & 0x3F) << 6;
            }
            if (i + 2 < token.size()) {
                codepoint |= (static_cast<uint8_t>(token[i + 2]) & 0x3F);
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            codepoint = (c & 0x07) << 18;
            if (i + 1 < token.size()) {
                codepoint |= (static_cast<uint8_t>(token[i + 1]) & 0x3F) << 12;
            }
            if (i + 2 < token.size()) {
                codepoint |= (static_cast<uint8_t>(token[i + 2]) & 0x3F) << 6;
            }
            if (i + 3 < token.size()) {
                codepoint |= (static_cast<uint8_t>(token[i + 3]) & 0x3F);
            }
            i += 4;
        } else {
            // Not valid UTF-8: skip the byte.
            i += 1;
            continue;
        }

        auto it = decoder_.find(codepoint);
        if (it != decoder_.end()) {
            bytes.push_back(it->second);
        } else {
            // Not one of the byte characters (shouldn't happen for Whisper
            // output): keep the character as it is.
            if (codepoint < 0x80) {
                bytes.push_back(static_cast<uint8_t>(codepoint));
            } else if (codepoint < 0x800) {
                bytes.push_back(static_cast<uint8_t>(0xC0 | (codepoint >> 6)));
                bytes.push_back(static_cast<uint8_t>(0x80 | (codepoint & 0x3F)));
            } else if (codepoint < 0x10000) {
                bytes.push_back(static_cast<uint8_t>(0xE0 | (codepoint >> 12)));
                bytes.push_back(static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F)));
                bytes.push_back(static_cast<uint8_t>(0x80 | (codepoint & 0x3F)));
            } else {
                bytes.push_back(static_cast<uint8_t>(0xF0 | (codepoint >> 18)));
                bytes.push_back(static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 0x3F)));
                bytes.push_back(static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F)));
                bytes.push_back(static_cast<uint8_t>(0x80 | (codepoint & 0x3F)));
            }
        }
    }

    return std::string(bytes.begin(), bytes.end());
}

const GPT2ByteDecoder& get_gpt2_decoder() {
    static GPT2ByteDecoder decoder;
    return decoder;
}

std::string clean_token(const std::string& token) {
    return get_gpt2_decoder().decode(token);
}

std::string clean_token_raw(const std::string& token) {
    std::string decoded = get_gpt2_decoder().decode(token);
    if (!decoded.empty() && decoded[0] == ' ') {
        decoded.erase(0, 1);
    }
    return decoded;
}

bool is_bracket_token(const std::string& token) {
    return token.size() >= 4 && token.compare(0, 2, "<|") == 0 &&
           token.compare(token.size() - 2, 2, "|>") == 0;
}

std::string extract_text(const std::vector<std::string>& tokens) {
    std::string text;
    for (const auto& token : tokens) {
        if (is_bracket_token(token)) continue;
        text += clean_token(token);
    }
    const size_t first = text.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\n\r");
    return text.substr(first, last - first + 1);
}

float compression_ratio_of(const std::string& text) {
    if (text.empty()) return 0.0f;
#if MUNINN_TEXT_HAVE_ZLIB
    uLongf size = compressBound(static_cast<uLong>(text.size()));
    std::vector<Bytef> packed(size);
    if (compress2(packed.data(), &size, reinterpret_cast<const Bytef*>(text.data()),
                  static_cast<uLong>(text.size()), Z_DEFAULT_COMPRESSION) != Z_OK || size == 0) {
        return 0.0f;
    }
    return static_cast<float>(text.size()) / static_cast<float>(size);
#else
    if (text.size() < 8) return 1.0f;
    std::set<std::string> grams;
    for (size_t i = 0; i + 4 <= text.size(); ++i) grams.insert(text.substr(i, 4));
    return static_cast<float>(text.size() - 3) / static_cast<float>(std::max<size_t>(1, grams.size()));
#endif
}

bool is_incomplete_utf8(const std::string& bytes) {
    // Walk back over continuation bytes to the last lead byte and check that
    // the character it starts is complete.
    if (bytes.empty()) return false;
    size_t i = bytes.size();
    size_t continuation = 0;
    while (i > 0 && (static_cast<uint8_t>(bytes[i - 1]) & 0xC0) == 0x80 && continuation < 4) {
        --i;
        ++continuation;
    }
    if (i == 0) return true;  // nothing but continuation bytes
    const uint8_t lead = static_cast<uint8_t>(bytes[i - 1]);
    size_t needed;
    if ((lead & 0x80) == 0) needed = 0;
    else if ((lead & 0xE0) == 0xC0) needed = 1;
    else if ((lead & 0xF0) == 0xE0) needed = 2;
    else if ((lead & 0xF8) == 0xF0) needed = 3;
    else return false;  // a broken byte: waiting for more tokens won't fix it
    if (needed == 0) return continuation > 0;  // stray continuation bytes after ASCII
    return continuation < needed;
}

bool is_spaceless_language(const std::string& language) {
    static const char* const kSpaceless[] = {"zh", "ja", "th", "lo", "my", "yue"};
    for (const char* l : kSpaceless) {
        if (language == l) return true;
    }
    return false;
}

namespace {

// faster-whisper tests `subword.strip() in string.punctuation`: a substring
// test against Python's ASCII punctuation string.
bool is_ascii_punctuation_run(const std::string& subword) {
    static const std::string kPunctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    const size_t first = subword.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return true;  // only whitespace: "" is in any string
    const size_t last = subword.find_last_not_of(" \t\n\r");
    return kPunctuation.find(subword.substr(first, last - first + 1)) != std::string::npos;
}

std::string trim_copy(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

// split_tokens_on_unicode: a unit ends wherever the bytes so far are whole
// characters.
std::vector<WordTokens> split_on_unicode(const std::vector<std::string>& tokens,
                                         const std::vector<size_t>& ids,
                                         size_t eot_id) {
    std::vector<WordTokens> units;
    WordTokens current;
    for (size_t i = 0; i < tokens.size(); ++i) {
        // Specials have no bytes of their own; keep them as separate units.
        if (ids[i] >= eot_id) {
            if (!current.token_indices.empty()) {
                units.push_back(std::move(current));
                current = {};
            }
            units.push_back({std::string(), {i}});
            continue;
        }
        current.text += clean_token(tokens[i]);
        current.token_indices.push_back(i);
        if (!is_incomplete_utf8(current.text)) {
            units.push_back(std::move(current));
            current = {};
        }
    }
    if (!current.token_indices.empty()) units.push_back(std::move(current));
    return units;
}

}  // namespace

std::vector<WordTokens> split_to_word_tokens(const std::vector<std::string>& tokens,
                                             const std::vector<size_t>& ids,
                                             size_t eot_id,
                                             const std::string& language) {
    if (tokens.size() != ids.size()) return {};
    std::vector<WordTokens> units = split_on_unicode(tokens, ids, eot_id);
    if (is_spaceless_language(language)) return units;

    std::vector<WordTokens> words;
    for (auto& unit : units) {
        const bool special = ids[unit.token_indices.front()] >= eot_id;
        const bool with_space = !unit.text.empty() && unit.text[0] == ' ';
        const bool punctuation = !special && is_ascii_punctuation_run(unit.text);
        if (special || with_space || punctuation || words.empty()) {
            words.push_back(std::move(unit));
        } else {
            words.back().text += unit.text;
            words.back().token_indices.insert(words.back().token_indices.end(),
                                              unit.token_indices.begin(),
                                              unit.token_indices.end());
        }
    }
    return words;
}

void merge_punctuations(std::vector<WordTokens>& words) {
    // faster-whisper's defaults.
    static const std::string kPrepended = "\"'\xE2\x80\x9C\xC2\xBF([{-";  // "'“¿([{-
    static const std::string kAppended =
        "\"'.\xE3\x80\x82,\xEF\xBC\x8C!\xEF\xBC\x81?\xEF\xBC\x9F:\xEF\xBC\x9A\xE2\x80\x9D)]}\xE3\x80\x81";
        // "'.。,，!！?？:：”)]}、

    if (words.size() >= 2) {
        // Opening punctuation joins the word after it.
        size_t j = words.size() - 1;
        for (size_t i = words.size() - 1; i-- > 0;) {
            WordTokens& previous = words[i];
            WordTokens& following = words[j];
            const std::string stripped = trim_copy(previous.text);
            if (!previous.text.empty() && previous.text[0] == ' ' && !stripped.empty() &&
                kPrepended.find(stripped) != std::string::npos) {
                following.text = previous.text + following.text;
                following.token_indices.insert(following.token_indices.begin(),
                                               previous.token_indices.begin(),
                                               previous.token_indices.end());
                previous.text.clear();
                previous.token_indices.clear();
            } else {
                j = i;
            }
        }

        // Closing punctuation joins the word before it.
        size_t i = 0;
        for (size_t k = 1; k < words.size(); ++k) {
            WordTokens& previous = words[i];
            WordTokens& following = words[k];
            const bool previous_ends_with_space =
                !previous.text.empty() && previous.text.back() == ' ';
            if (!previous_ends_with_space && !following.text.empty() &&
                kAppended.find(following.text) != std::string::npos) {
                previous.text += following.text;
                previous.token_indices.insert(previous.token_indices.end(),
                                              following.token_indices.begin(),
                                              following.token_indices.end());
                following.text.clear();
                following.token_indices.clear();
            } else {
                i = k;
            }
        }
    }

    words.erase(std::remove_if(words.begin(), words.end(),
                               [](const WordTokens& w) { return w.text.empty(); }),
                words.end());
}

}  // namespace text
}  // namespace muninn
