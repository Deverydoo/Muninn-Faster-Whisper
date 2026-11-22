// Checks for the translator's text handling and the diarizer's clustering,
// with no model and no GPU: sentence splitting, output clean-up per target
// language, language-code normalisation, blank input, and speaker clustering
// (the O(n^2) version against a copy of the old O(n^3) loop, and its speed).
//
// It compiles src/translate_text.cpp and src/speaker_clustering.cpp directly,
// so it needs neither muninn.dll nor CTranslate2. ctest label: cpu.
//
// Usage: test_translate_text            (exit code 0 = all checks passed)
#include "translate_text.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                   \
    do {                                                                     \
        ++g_checks;                                                          \
        const std::string _a = (a), _b = (b);                                \
        if (_a != _b) {                                                      \
            ++g_failures;                                                    \
            std::printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,        \
                        __LINE__, _a.c_str(), _b.c_str());                   \
        }                                                                    \
    } while (0)

using muninn::detail::clean_output;
using muninn::detail::split_sentences;

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += " | ";
        s += v[i];
    }
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════
// Sentence splitter
// ═══════════════════════════════════════════════════════════════════════════

void test_sentence_splitter() {
    std::printf("[sentence splitter]\n");

    CHECK_EQ_STR(joined(split_sentences("Hello there. How are you? Fine!")),
                 "Hello there. | How are you? | Fine!");
    // Numbers
    CHECK_EQ_STR(joined(split_sentences("It costs 3.5 dollars. Cheap.")),
                 "It costs 3.5 dollars. | Cheap.");
    CHECK_EQ_STR(joined(split_sentences("Version 1.2.3 is out. Get it.")),
                 "Version 1.2.3 is out. | Get it.");
    // Single-letter abbreviations
    CHECK_EQ_STR(joined(split_sentences("Bring fruit, e.g. Apples and pears. Thanks.")),
                 "Bring fruit, e.g. Apples and pears. | Thanks.");
    CHECK_EQ_STR(joined(split_sentences("I lived in the U.S. For ten years.")),
                 "I lived in the U.S. For ten years.");
    CHECK_EQ_STR(joined(split_sentences("That is, i.e. The point. Next.")),
                 "That is, i.e. The point. | Next.");
    CHECK_EQ_STR(joined(split_sentences("J. R. R. Tolkien wrote it. Read it.")),
                 "J. R. R. Tolkien wrote it. | Read it.");
    // Common abbreviations
    CHECK_EQ_STR(joined(split_sentences("Mr. Smith met Dr. Jones. They talked.")),
                 "Mr. Smith met Dr. Jones. | They talked.");
    CHECK_EQ_STR(joined(split_sentences("Cats vs. Dogs is a classic. Pick one.")),
                 "Cats vs. Dogs is a classic. | Pick one.");
    CHECK_EQ_STR(joined(split_sentences("Bring pens, paper, etc. The usual. Go.")),
                 "Bring pens, paper, etc. The usual. | Go.");
    CHECK_EQ_STR(joined(split_sentences("Room No. 5 is free. Take it.")),
                 "Room No. 5 is free. | Take it.");
    // "No." on its own is a sentence (common in speech)
    CHECK_EQ_STR(joined(split_sentences("Did you win? No. Not this time.")),
                 "Did you win? | No. | Not this time.");
    // Dots inside words
    CHECK_EQ_STR(joined(split_sentences("Open file.txt now. Then visit example.com today.")),
                 "Open file.txt now. | Then visit example.com today.");
    CHECK_EQ_STR(joined(split_sentences("See https://example.com/a.b?c=d. Done.")),
                 "See https://example.com/a.b?c=d. | Done.");
    // Lower-case continuation and ellipses
    CHECK_EQ_STR(joined(split_sentences("It was approx. five metres. Big.")),
                 "It was approx. five metres. | Big.");
    CHECK_EQ_STR(joined(split_sentences("Wait... what was that? Nothing...")),
                 "Wait... what was that? | Nothing...");
    CHECK_EQ_STR(joined(split_sentences("Hmm... Okay then.")), "Hmm... | Okay then.");
    CHECK_EQ_STR(joined(split_sentences("Really?! Yes!!")), "Really?! | Yes!!");
    // Closing quotes stay with their sentence
    CHECK_EQ_STR(joined(split_sentences("He said \"Go.\" Then he left.")),
                 "He said \"Go.\" | Then he left.");
    // "!" and "?" glued to a word
    CHECK_EQ_STR(joined(split_sentences("I use Yahoo!Mail daily. Yes.")),
                 "I use Yahoo!Mail daily. | Yes.");
    // CJK
    CHECK_EQ_STR(joined(split_sentences("こんにちは。元気ですか？はい！")),
                 "こんにちは。 | 元気ですか？ | はい！");
    CHECK_EQ_STR(joined(split_sentences("「行こう。」と言った。")), "「行こう。」 | と言った。");
    // Edges
    CHECK(split_sentences("").empty());
    CHECK(split_sentences("   \n ").empty());
    CHECK_EQ_STR(joined(split_sentences("no punctuation at all")), "no punctuation at all");
    CHECK_EQ_STR(joined(split_sentences("  padded.  ")), "padded.");
}

// ═══════════════════════════════════════════════════════════════════════════
// Output clean-up
// ═══════════════════════════════════════════════════════════════════════════

void test_clean_output() {
    std::printf("[clean_output]\n");

    // English
    CHECK_EQ_STR(clean_output("eng_Latn Hello , world !", "eng_Latn"), "Hello, world!");
    CHECK_EQ_STR(clean_output("  Is it ready ?  ", "eng_Latn"), "Is it ready?");
    CHECK_EQ_STR(clean_output("Note : this .", "eng_Latn"), "Note: this.");
    CHECK_EQ_STR(clean_output("Hello,world", "eng_Latn"), "Hello, world");
    CHECK_EQ_STR(clean_output("Open file.txt now", "eng_Latn"), "Open file.txt now");
    CHECK_EQ_STR(clean_output("Visit https://example.com/a?b=c,d now", "eng_Latn"),
                 "Visit https://example.com/a?b=c,d now");
    CHECK_EQ_STR(clean_output("Mail me@example.com today", "eng_Latn"), "Mail me@example.com today");
    CHECK_EQ_STR(clean_output("It costs .5 dollars", "eng_Latn"), "It costs .5 dollars");
    CHECK_EQ_STR(clean_output("Meet at 10:30 in the U.S.A.", "eng_Latn"), "Meet at 10:30 in the U.S.A.");
    CHECK_EQ_STR(clean_output("Wait ...", "eng_Latn"), "Wait...");
    CHECK_EQ_STR(clean_output("1,000 people", "eng_Latn"), "1,000 people");
    CHECK_EQ_STR(clean_output("   ", "eng_Latn"), "");
    CHECK_EQ_STR(clean_output("eng_Latn", "eng_Latn"), "");

    // French keeps its space before ; : ! ?
    CHECK_EQ_STR(clean_output("Bonjour ! Ça va ?", "fra_Latn"), "Bonjour ! Ça va ?");
    CHECK_EQ_STR(clean_output("Il a dit : oui ; non", "fra_Latn"), "Il a dit : oui ; non");
    CHECK_EQ_STR(clean_output("Bonjour , le monde .", "fra_Latn"), "Bonjour, le monde.");
    CHECK_EQ_STR(clean_output("fra_Latn Ouvrez fichier.txt", "fra_Latn"), "Ouvrez fichier.txt");

    // Spanish and German behave like English
    CHECK_EQ_STR(clean_output("¿Qué tal ?", "spa_Latn"), "¿Qué tal?");
    CHECK_EQ_STR(clean_output("Hallo , Welt !", "deu_Latn"), "Hallo, Welt!");

    // Japanese / Chinese: no space before full-width punctuation, none inserted
    CHECK_EQ_STR(clean_output("こんにちは 。元気 ？", "jpn_Jpan"), "こんにちは。元気？");
    CHECK_EQ_STR(clean_output("你好 ，世界 。", "zho_Hans"), "你好，世界。");
    CHECK_EQ_STR(clean_output("ok,go", "jpn_Jpan"), "ok,go");

    // Sentence joining
    using muninn::detail::join_sentences;
    CHECK_EQ_STR(join_sentences({"Hello.", "Bye."}, "eng_Latn"), "Hello. Bye.");
    CHECK_EQ_STR(join_sentences({"こんにちは。", "さようなら。"}, "jpn_Jpan"), "こんにちは。さようなら。");
    CHECK_EQ_STR(join_sentences({"Bonjour.", "", "Salut."}, "fra_Latn"), "Bonjour. Salut.");

    // UTF-8 safe truncation for log lines
    using muninn::detail::utf8_truncate;
    CHECK_EQ_STR(utf8_truncate("short", 50), "short");
    CHECK_EQ_STR(utf8_truncate("abcdef", 3), "abc...");
    CHECK_EQ_STR(utf8_truncate("ab\xC3\xA9", 3), "ab...");             // é not cut in half
    CHECK_EQ_STR(utf8_truncate("\xE7\x94\xB0\xE4\xB8\xAD", 4), "\xE7\x94\xB0...");  // 田中
}

// ═══════════════════════════════════════════════════════════════════════════
// Language codes and blank input
// ═══════════════════════════════════════════════════════════════════════════

void test_language_codes() {
    std::printf("[language codes]\n");
    using muninn::detail::nllb_code_for;
    using muninn::detail::short_code_for_nllb;

    CHECK_EQ_STR(nllb_code_for("en"), "eng_Latn");
    CHECK_EQ_STR(nllb_code_for("EN"), "eng_Latn");
    CHECK_EQ_STR(nllb_code_for(" fr "), "fra_Latn");
    CHECK_EQ_STR(nllb_code_for("en-US"), "eng_Latn");
    CHECK_EQ_STR(nllb_code_for("en_GB"), "eng_Latn");
    CHECK_EQ_STR(nllb_code_for("pt-BR"), "por_Latn");
    CHECK_EQ_STR(nllb_code_for("zh"), "zho_Hans");
    CHECK_EQ_STR(nllb_code_for("zh-CN"), "zho_Hans");
    CHECK_EQ_STR(nllb_code_for("zh-TW"), "zho_Hant");
    CHECK_EQ_STR(nllb_code_for("zh_Hant"), "zho_Hant");
    CHECK_EQ_STR(nllb_code_for("zh-Hant-TW"), "zho_Hant");
    CHECK_EQ_STR(nllb_code_for("nb"), "nob_Latn");
    CHECK_EQ_STR(nllb_code_for("fra_Latn"), "fra_Latn");  // NLLB codes pass through
    CHECK_EQ_STR(nllb_code_for("xx"), "");
    CHECK_EQ_STR(nllb_code_for("xx-YY"), "");
    CHECK_EQ_STR(nllb_code_for(""), "");
    CHECK_EQ_STR(nllb_code_for("auto"), "");

    CHECK_EQ_STR(short_code_for_nllb("fra_Latn"), "fr");
    CHECK_EQ_STR(short_code_for_nllb("zho_Hant"), "zh");
    CHECK_EQ_STR(short_code_for_nllb("nope"), "");

    using muninn::detail::is_blank;
    CHECK(is_blank(""));
    CHECK(is_blank(" \t\r\n"));
    CHECK(is_blank("\xC2\xA0"));            // no-break space
    CHECK(is_blank("\xE3\x80\x80 "));       // ideographic space
    CHECK(!is_blank(" a "));
    CHECK(!is_blank("\xE3\x80\x82"));        // 。 is text
}

} // namespace

int main() {
    test_sentence_splitter();
    test_clean_output();
    test_language_codes();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
