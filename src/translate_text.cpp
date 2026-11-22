#include "translate_text.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace muninn {
namespace detail {

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Language tables
// ═══════════════════════════════════════════════════════════════════════════

// Whisper/short code -> NLLB code. One entry per language; the reverse
// mapping (NLLB -> short) is built from this table.
const std::unordered_map<std::string, std::string>& base_codes() {
    static const std::unordered_map<std::string, std::string> table = {
        // Latin-script European languages
        {"en", "eng_Latn"},      // English
        {"es", "spa_Latn"},      // Spanish
        {"fr", "fra_Latn"},      // French
        {"de", "deu_Latn"},      // German
        {"it", "ita_Latn"},      // Italian
        {"pt", "por_Latn"},      // Portuguese
        {"nl", "nld_Latn"},      // Dutch
        {"pl", "pol_Latn"},      // Polish
        {"ro", "ron_Latn"},      // Romanian
        {"sv", "swe_Latn"},      // Swedish
        {"da", "dan_Latn"},      // Danish
        {"no", "nob_Latn"},      // Norwegian (Bokmal)
        {"fi", "fin_Latn"},      // Finnish
        {"cs", "ces_Latn"},      // Czech
        {"hu", "hun_Latn"},      // Hungarian
        {"el", "ell_Grek"},      // Greek
        {"tr", "tur_Latn"},      // Turkish

        // Cyrillic languages
        {"ru", "rus_Cyrl"},      // Russian
        {"uk", "ukr_Cyrl"},      // Ukrainian
        {"bg", "bul_Cyrl"},      // Bulgarian
        {"be", "bel_Cyrl"},      // Belarusian

        // Asian languages
        {"zh", "zho_Hans"},      // Chinese (Simplified)
        {"ja", "jpn_Jpan"},      // Japanese
        {"ko", "kor_Hang"},      // Korean
        {"vi", "vie_Latn"},      // Vietnamese
        {"th", "tha_Thai"},      // Thai
        {"id", "ind_Latn"},      // Indonesian
        {"ms", "zsm_Latn"},      // Malay
        {"tl", "tgl_Latn"},      // Filipino/Tagalog

        // Middle Eastern / South Asian
        {"ar", "arb_Arab"},      // Arabic
        {"fa", "pes_Arab"},      // Persian (Farsi)
        {"he", "heb_Hebr"},      // Hebrew
        {"hi", "hin_Deva"},      // Hindi
        {"bn", "ben_Beng"},      // Bengali
        {"ta", "tam_Taml"},      // Tamil
        {"ur", "urd_Arab"},      // Urdu
    };
    return table;
}

// Codes that mean something the base language doesn't: regional scripts and
// older or alternative spellings of a code. Checked before the base table.
const std::unordered_map<std::string, std::string>& special_codes() {
    static const std::unordered_map<std::string, std::string> table = {
        {"zh-tw", "zho_Hant"},    // Chinese as written in Taiwan (Traditional)
        {"zh-hk", "zho_Hant"},    // Hong Kong
        {"zh-mo", "zho_Hant"},    // Macau
        {"zh-hant", "zho_Hant"},  // Traditional script
        {"nb", "nob_Latn"},       // Norwegian Bokmal
        {"iw", "heb_Hebr"},       // old code for Hebrew
        {"in", "ind_Latn"},       // old code for Indonesian
        {"fil", "tgl_Latn"},      // Filipino
    };
    return table;
}

const std::unordered_map<std::string, std::string>& reverse_codes() {
    static const std::unordered_map<std::string, std::string> table = [] {
        std::unordered_map<std::string, std::string> r;
        for (const auto& kv : base_codes()) r[kv.second] = kv.first;
        return r;
    }();
    return table;
}

bool is_ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool is_ascii_alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool is_ascii_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }

bool is_ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool starts_with_at(const std::string& s, size_t i, const char* seq) {
    const size_t len = std::strlen(seq);
    return i + len <= s.size() && s.compare(i, len, seq) == 0;
}

// Length in bytes of a whitespace character at i (ASCII, U+00A0, U+3000), or 0.
size_t space_len_at(const std::string& s, size_t i) {
    if (i >= s.size()) return 0;
    if (is_ascii_space(static_cast<unsigned char>(s[i]))) return 1;
    if (starts_with_at(s, i, "\xC2\xA0")) return 2;      // no-break space
    if (starts_with_at(s, i, "\xE3\x80\x80")) return 3;  // ideographic space
    return 0;
}

// Sentence enders that need no space after them: 。！？ and the full-width
// full stop ．.
size_t cjk_terminator_len_at(const std::string& s, size_t i) {
    if (starts_with_at(s, i, "\xE3\x80\x82") ||  // 。
        starts_with_at(s, i, "\xEF\xBC\x81") ||  // ！
        starts_with_at(s, i, "\xEF\xBC\x9F") ||  // ？
        starts_with_at(s, i, "\xEF\xBC\x8E")) {  // ．
        return 3;
    }
    return 0;
}

// Closing quotes and brackets that belong to the sentence before them.
size_t closer_len_at(const std::string& s, size_t i) {
    if (i >= s.size()) return 0;
    const char c = s[i];
    if (c == '"' || c == '\'' || c == ')' || c == ']' || c == '}') return 1;
    if (starts_with_at(s, i, "\xE2\x80\x9D") ||  // ”
        starts_with_at(s, i, "\xE2\x80\x99") ||  // ’
        starts_with_at(s, i, "\xE3\x80\x8D") ||  // 」
        starts_with_at(s, i, "\xE3\x80\x8F") ||  // 』
        starts_with_at(s, i, "\xEF\xBC\x89")) {  // ）
        return 3;
    }
    if (starts_with_at(s, i, "\xC2\xBB")) return 2;  // »
    return 0;
}

std::string trim(const std::string& s, size_t begin, size_t end) {
    while (begin < end) {
        const size_t len = space_len_at(s, begin);
        if (len == 0) break;
        begin += len;
    }
    while (end > begin) {
        const unsigned char c = static_cast<unsigned char>(s[end - 1]);
        if (is_ascii_space(c)) { --end; continue; }
        if (end - begin >= 2 && s.compare(end - 2, 2, "\xC2\xA0") == 0) { end -= 2; continue; }
        if (end - begin >= 3 && s.compare(end - 3, 3, "\xE3\x80\x80") == 0) { end -= 3; continue; }
        break;
    }
    return s.substr(begin, end - begin);
}

// Words that end in a dot without ending the sentence. Lower case, without
// the dot. English first, then a few common ones from the other languages
// Whisper hears most. "no" is handled separately ("No." is a sentence).
const std::unordered_set<std::string>& abbreviations() {
    static const std::unordered_set<std::string> words = {
        "mr", "mrs", "ms", "dr", "prof", "sr", "jr", "st", "vs", "etc",
        "approx", "inc", "ltd", "corp", "co", "mt", "ft", "fig", "vol",
        "dept", "gen", "gov", "lt", "col", "sgt", "capt", "rev", "cf", "al",
        "ca", "jan", "feb", "mar", "apr", "jun", "jul", "aug", "sep", "sept",
        "oct", "nov", "dec",
        // German, Spanish, French
        "bzw", "usw", "ggf", "nr", "hr", "sra", "srta", "dra", "ud", "uds",
        "mme", "mlle",
    };
    return words;
}

// "e.g", "U.S", "i.e", "J": single letters separated by dots.
bool is_single_letter_abbreviation(const std::string& word) {
    if (word.empty()) return false;
    for (size_t i = 0; i < word.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(word[i]);
        if (i % 2 == 0) {
            if (!is_ascii_alpha(c)) return false;
        } else if (c != '.') {
            return false;
        }
    }
    return word.size() % 2 == 1;  // ends in a letter
}

// The word just before position i (back to whitespace), without opening
// quotes or brackets.
std::string word_before(const std::string& s, size_t i) {
    size_t b = i;
    while (b > 0 && !is_ascii_space(static_cast<unsigned char>(s[b - 1]))) --b;
    while (b < i && (s[b] == '"' || s[b] == '\'' || s[b] == '(' || s[b] == '[')) ++b;
    return s.substr(b, i - b);
}

// Decides whether the run of terminators s[i, run_end) ends a sentence.
bool is_sentence_boundary(const std::string& s, size_t i, size_t run_end) {
    // Something glued on ("3.5", "file.txt", "example.com", "Yahoo!Mail",
    // "?q=1"): not a boundary.
    if (run_end < s.size() && space_len_at(s, run_end) == 0) return false;

    bool only_dots = true;
    for (size_t k = i; k < run_end;) {
        if (s[k] == '.') { ++k; continue; }
        if (starts_with_at(s, k, "\xE2\x80\xA6")) { k += 3; continue; }  // …
        if (s[k] == '!' || s[k] == '?') { only_dots = false; ++k; continue; }
        ++k;  // closing quote/bracket bytes
    }
    if (!only_dots) return true;

    // The next word after the run.
    size_t j = run_end;
    while (j < s.size()) {
        const size_t len = space_len_at(s, j);
        if (len == 0) break;
        j += len;
    }
    if (j >= s.size()) return true;  // end of text

    // "approx. five", "Wait... what": a lower-case word continues the sentence.
    if (is_ascii_lower(static_cast<unsigned char>(s[j]))) return false;

    // Abbreviations: only a single dot right after the word.
    const bool single_dot = s[i] == '.' &&
        (i + 1 == run_end || (s[i + 1] != '.' && !starts_with_at(s, i + 1, "\xE2\x80\xA6")));
    if (single_dot) {
        const std::string word = word_before(s, i);
        if (is_single_letter_abbreviation(word)) return false;
        std::string lower;
        lower.reserve(word.size());
        for (char c : word) lower += ascii_lower(c);
        if (abbreviations().count(lower)) return false;
        if (lower == "no" && is_ascii_digit(static_cast<unsigned char>(s[j]))) return false;  // "No. 5"
    }
    return true;
}

// The language part of an NLLB code ("fra_Latn" -> "fra").
std::string nllb_language(const std::string& target_nllb) {
    const size_t us = target_nllb.find('_');
    return us == std::string::npos ? target_nllb : target_nllb.substr(0, us);
}

// Targets written without spaces between words or sentences.
bool writes_without_spaces(const std::string& lang3) {
    return lang3 == "jpn" || lang3 == "zho" || lang3 == "yue";
}

// The whitespace-delimited word around position i looks like a URL, a path,
// an e-mail address or a query string: leave it exactly as it is.
bool in_urlish_word(const std::string& s, size_t i) {
    size_t b = i, e = i;
    while (b > 0 && !is_ascii_space(static_cast<unsigned char>(s[b - 1]))) --b;
    while (e < s.size() && !is_ascii_space(static_cast<unsigned char>(s[e]))) ++e;
    const std::string w = s.substr(b, e - b);
    return w.find("://") != std::string::npos || w.find("www.") != std::string::npos ||
           w.find_first_of("/\\@=&") != std::string::npos;
}

// Characters after which a removed space can't have been meaningful: the end,
// whitespace, or more punctuation / closing quotes.
bool punct_is_free_standing(const std::string& s, size_t j) {
    if (j >= s.size()) return true;
    if (space_len_at(s, j) > 0) return true;
    const char c = s[j];
    return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' ||
           c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' || closer_len_at(s, j) > 0;
}

// Full-width punctuation that never takes a space before it in CJK text.
size_t fullwidth_punct_len_at(const std::string& s, size_t i) {
    if (cjk_terminator_len_at(s, i)) return 3;
    if (starts_with_at(s, i, "\xE3\x80\x81") ||  // 、
        starts_with_at(s, i, "\xEF\xBC\x8C") ||  // ，
        starts_with_at(s, i, "\xEF\xBC\x9A") ||  // ：
        starts_with_at(s, i, "\xEF\xBC\x9B")) {  // ；
        return 3;
    }
    return 0;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Language codes
// ═══════════════════════════════════════════════════════════════════════════

std::string canonical_language_code(const std::string& code) {
    std::string out = trim(code, 0, code.size());
    for (char& c : out) {
        c = ascii_lower(c);
        if (c == '_') c = '-';
    }
    return out;
}

std::string nllb_code_for(const std::string& code) {
    // Already an NLLB code ("fra_Latn")?
    if (reverse_codes().count(code)) return code;

    const std::string canon = canonical_language_code(code);
    if (canon.empty()) return "";

    // The longest known prefix: "zh-hant-tw" -> "zh-hant", "en-us" -> "en".
    std::string key = canon;
    while (!key.empty()) {
        auto special = special_codes().find(key);
        if (special != special_codes().end()) return special->second;
        auto base = base_codes().find(key);
        if (base != base_codes().end()) return base->second;
        const size_t dash = key.rfind('-');
        if (dash == std::string::npos) break;
        key.resize(dash);
    }
    return "";
}

std::string short_code_for_nllb(const std::string& nllb_code) {
    auto it = reverse_codes().find(nllb_code);
    if (it != reverse_codes().end()) return it->second;
    if (nllb_code == "zho_Hant") return "zh";
    return "";
}

// ═══════════════════════════════════════════════════════════════════════════
// Text
// ═══════════════════════════════════════════════════════════════════════════

bool is_blank(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        const size_t len = space_len_at(text, i);
        if (len == 0) return false;
        i += len;
    }
    return true;
}

std::vector<std::string> split_sentences(const std::string& text) {
    std::vector<std::string> sentences;
    const size_t n = text.size();
    size_t start = 0;
    size_t i = 0;

    auto push = [&](size_t end) {
        std::string s = trim(text, start, end);
        if (!s.empty()) sentences.push_back(std::move(s));
    };
    auto skip_space = [&](size_t j) {
        while (j < n) {
            const size_t len = space_len_at(text, j);
            if (len == 0) break;
            j += len;
        }
        return j;
    };
    auto consume_closers = [&](size_t j) {
        while (j < n) {
            const size_t len = closer_len_at(text, j);
            if (len == 0) break;
            j += len;
        }
        return j;
    };

    while (i < n) {
        // 。！？ end a sentence with or without a space after them.
        if (size_t len = cjk_terminator_len_at(text, i)) {
            size_t j = i + len;
            while (size_t more = cjk_terminator_len_at(text, j)) j += more;
            j = consume_closers(j);
            push(j);
            start = i = skip_space(j);
            continue;
        }

        const char c = text[i];
        const bool ellipsis = starts_with_at(text, i, "\xE2\x80\xA6");
        if (c == '.' || c == '!' || c == '?' || ellipsis) {
            // The whole run: "...", "?!", "!!!", "…", then closing quotes.
            size_t j = i;
            while (j < n) {
                if (text[j] == '.' || text[j] == '!' || text[j] == '?') { ++j; continue; }
                if (starts_with_at(text, j, "\xE2\x80\xA6")) { j += 3; continue; }
                break;
            }
            j = consume_closers(j);
            if (is_sentence_boundary(text, i, j)) {
                push(j);
                start = i = skip_space(j);
            } else {
                i = j;
            }
            continue;
        }
        ++i;
    }
    push(n);
    return sentences;
}

std::string join_sentences(const std::vector<std::string>& sentences,
                           const std::string& target_nllb) {
    const bool no_spaces = writes_without_spaces(nllb_language(target_nllb));
    std::string result;
    for (size_t i = 0; i < sentences.size(); ++i) {
        if (sentences[i].empty()) continue;
        if (!result.empty() && !no_spaces) result += ' ';
        result += sentences[i];
    }
    return result;
}

std::string clean_output(const std::string& text, const std::string& target_nllb) {
    std::string result = text;

    // A leading target-language token ("fra_Latn Bonjour").
    if (!target_nllb.empty() && result.compare(0, target_nllb.size(), target_nllb) == 0) {
        result.erase(0, target_nllb.size());
    }

    result = trim(result, 0, result.size());
    if (result.empty()) return "";

    const std::string lang = nllb_language(target_nllb);
    const bool french = (lang == "fra");
    const bool no_spaces = writes_without_spaces(lang);

    std::string out;
    out.reserve(result.size() + 8);
    const size_t n = result.size();

    for (size_t i = 0; i < n; ++i) {
        const char c = result[i];

        if (c == ' ' && i + 1 < n) {
            const char p = result[i + 1];
            const bool free_standing = punct_is_free_standing(result, i + 2);
            // " ." and " ," as sentence punctuation; not " .5" or " .txt".
            if ((p == '.' || p == ',') && free_standing) continue;
            // " !", " ?", " ;", " :" — except in French, where that space is correct.
            if (!french && (p == '!' || p == '?' || p == ';' || p == ':') && free_standing) continue;
            // CJK text: no space before 。、，！？：；
            if (no_spaces && fullwidth_punct_len_at(result, i + 1)) continue;
        }

        out += c;

        // A missing space after , ; ! ? between two letters ("Hello,world").
        // Never after '.' or ':' (file.txt, U.S., 10:30, e.g.), never in a
        // URL, path or address, and not for languages written without spaces.
        if (!no_spaces && (c == ',' || c == ';' || c == '!' || c == '?') &&
            i > 0 && i + 1 < n &&
            is_ascii_alpha(static_cast<unsigned char>(result[i - 1])) &&
            is_ascii_alpha(static_cast<unsigned char>(result[i + 1])) &&
            !in_urlish_word(result, i)) {
            out += ' ';
        }
    }
    return out;
}

std::string utf8_truncate(const std::string& text, size_t max_bytes) {
    if (text.size() <= max_bytes) return text;
    size_t cut = max_bytes;
    // Step back over continuation bytes so the cut lands on a character start.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "...";
}

} // namespace detail
} // namespace muninn
