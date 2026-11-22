// Checks for the translator's text handling and the diarizer's clustering,
// with no model and no GPU: sentence splitting, output clean-up per target
// language, language-code normalisation, blank input, and speaker clustering
// (the O(n^2) version against a copy of the old O(n^3) loop, and its speed).
//
// It compiles src/translate_text.cpp and src/speaker_clustering.cpp directly,
// so it needs neither muninn.dll nor CTranslate2. ctest label: cpu.
//
// Usage: test_translate_text            (exit code 0 = all checks passed)

// A user of diarization.h has no ONNX Runtime headers (this target has none
// on its include path) and no Muninn build defines; make sure the header
// compiles that way.
#ifdef MUNINN_USE_SILERO_VAD
#undef MUNINN_USE_SILERO_VAD
#endif
#include "muninn/diarization.h"

#include "speaker_clustering.h"
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

// ═══════════════════════════════════════════════════════════════════════════
// Speaker clustering
// ═══════════════════════════════════════════════════════════════════════════

// The clustering loop as it was before the November 2026 audit (D1), copied
// from diarization.cpp: O(n^3), recomputing every cluster pair's average
// similarity on every merge. Returns the cluster index of each embedding
// (clusters are kept in order of their first embedding).
float old_cosine_similarity(const muninn::SpeakerEmbedding& emb1,
                            const muninn::SpeakerEmbedding& emb2) {
    float dot_product = 0.0f, norm1 = 0.0f, norm2 = 0.0f;
    for (size_t i = 0; i < emb1.features.size(); ++i) {
        dot_product += emb1.features[i] * emb2.features[i];
        norm1 += emb1.features[i] * emb1.features[i];
        norm2 += emb2.features[i] * emb2.features[i];
    }
    if (norm1 == 0.0f || norm2 == 0.0f) return 0.0f;
    return dot_product / (std::sqrt(norm1) * std::sqrt(norm2));
}

std::vector<int> old_cluster_labels(const std::vector<muninn::SpeakerEmbedding>& embeddings,
                                    float merge_threshold, int max_speakers) {
    std::vector<int> cluster_labels(embeddings.size(), -1);
    std::vector<std::vector<size_t>> clusters;
    for (size_t i = 0; i < embeddings.size(); ++i) {
        cluster_labels[i] = static_cast<int>(i);
        clusters.push_back({i});
    }

    bool merged = true;
    while (merged && clusters.size() > 1) {
        merged = false;
        float best_similarity = -1.0f;
        size_t best_i = 0, best_j = 0;
        for (size_t i = 0; i < clusters.size(); ++i) {
            for (size_t j = i + 1; j < clusters.size(); ++j) {
                float total_similarity = 0.0f;
                size_t pair_count = 0;
                for (size_t idx_i : clusters[i]) {
                    for (size_t idx_j : clusters[j]) {
                        total_similarity += old_cosine_similarity(embeddings[idx_i], embeddings[idx_j]);
                        pair_count++;
                    }
                }
                float avg_similarity = total_similarity / pair_count;
                if (avg_similarity > best_similarity && avg_similarity >= merge_threshold) {
                    best_similarity = avg_similarity;
                    best_i = i;
                    best_j = j;
                    merged = true;
                }
            }
        }
        if (merged) {
            clusters[best_i].insert(clusters[best_i].end(),
                                    clusters[best_j].begin(), clusters[best_j].end());
            clusters.erase(clusters.begin() + best_j);
            for (size_t i = 0; i < clusters.size(); ++i)
                for (size_t idx : clusters[i]) cluster_labels[idx] = static_cast<int>(i);
        }
    }

    if (max_speakers > 0 && static_cast<int>(clusters.size()) > max_speakers) {
        while (static_cast<int>(clusters.size()) > max_speakers) {
            float best_similarity = -1.0f;
            size_t best_i = 0, best_j = 0;
            for (size_t i = 0; i < clusters.size(); ++i) {
                for (size_t j = i + 1; j < clusters.size(); ++j) {
                    float total_similarity = 0.0f;
                    size_t pair_count = 0;
                    for (size_t idx_i : clusters[i]) {
                        for (size_t idx_j : clusters[j]) {
                            total_similarity += old_cosine_similarity(embeddings[idx_i], embeddings[idx_j]);
                            pair_count++;
                        }
                    }
                    float avg_similarity = total_similarity / pair_count;
                    if (avg_similarity > best_similarity) {
                        best_similarity = avg_similarity;
                        best_i = i;
                        best_j = j;
                    }
                }
            }
            clusters[best_i].insert(clusters[best_i].end(),
                                    clusters[best_j].begin(), clusters[best_j].end());
            clusters.erase(clusters.begin() + best_j);
            for (size_t i = 0; i < clusters.size(); ++i)
                for (size_t idx : clusters[i]) cluster_labels[idx] = static_cast<int>(i);
        }
    }
    return cluster_labels;
}

// n embeddings around `speakers` random centres; `spread` sets how far each
// point strays from its centre (bigger = clusters overlap more).
std::vector<muninn::SpeakerEmbedding> random_embeddings(std::mt19937& rng, size_t n, size_t dim,
                                                        int speakers, float spread) {
    std::normal_distribution<float> normal(0.0f, 1.0f);
    std::uniform_int_distribution<int> pick(0, std::max(0, speakers - 1));
    std::vector<std::vector<float>> centres(static_cast<size_t>(std::max(1, speakers)),
                                            std::vector<float>(dim));
    for (auto& c : centres)
        for (auto& v : c) v = normal(rng);

    std::vector<muninn::SpeakerEmbedding> out(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& c = centres[static_cast<size_t>(pick(rng))];
        out[i].features.resize(dim);
        for (size_t d = 0; d < dim; ++d) out[i].features[d] = c[d] + spread * normal(rng);
        out[i].start = 0.5f * static_cast<float>(i);
        out[i].end = out[i].start + 1.0f;
    }
    return out;
}

void test_clustering_matches_old() {
    std::printf("[clustering: new vs old]\n");
    std::mt19937 rng(20261122);

    const size_t sizes[] = {1, 2, 3, 5, 8, 13, 25, 40, 60};
    const float thresholds[] = {0.2f, 0.5f, 0.7f, 0.9f};
    const int max_speakers[] = {0, 1, 2, 3, 10};
    const float spreads[] = {0.3f, 0.8f, 1.5f};

    int cases = 0, mismatches = 0;
    int nontrivial = 0;  // neither one cluster nor every point on its own
    for (size_t n : sizes) {
        for (float spread : spreads) {
            const int speakers = 1 + static_cast<int>(rng() % 4);
            const auto emb = random_embeddings(rng, n, 16, speakers, spread);
            for (float th : thresholds) {
                for (int ms : max_speakers) {
                    const auto expected = old_cluster_labels(emb, th, ms);
                    const auto got = muninn::detail::cluster_embeddings(emb, th, ms);
                    ++cases;
                    const int k = expected.empty() ? 0 : *std::max_element(expected.begin(), expected.end()) + 1;
                    if (k > 1 && k < static_cast<int>(n)) ++nontrivial;
                    if (expected != got) {
                        ++mismatches;
                        if (mismatches <= 5) {
                            std::printf("  mismatch: n=%zu spread=%.1f threshold=%.1f max_speakers=%d\n",
                                        n, spread, th, ms);
                        }
                    }
                }
            }
        }
    }
    std::printf("  %d cases (%d with some but not all points merged), %d mismatches\n",
                cases, nontrivial, mismatches);
    CHECK(mismatches == 0);
    CHECK(nontrivial > cases / 3);

    // A zero vector is similar to nothing (0), as before.
    std::vector<muninn::SpeakerEmbedding> with_zero = random_embeddings(rng, 6, 8, 2, 0.2f);
    std::fill(with_zero[3].features.begin(), with_zero[3].features.end(), 0.0f);
    CHECK(old_cluster_labels(with_zero, 0.5f, 0) == muninn::detail::cluster_embeddings(with_zero, 0.5f, 0));

    // Edges
    CHECK(muninn::detail::cluster_embeddings({}, 0.7f, 10).empty());
    CHECK(muninn::detail::cluster_embeddings(random_embeddings(rng, 1, 8, 1, 0.1f), 0.7f, 10) ==
          std::vector<int>{0});
}

void test_clustering_speed() {
    std::printf("[clustering: speed]\n");
    std::mt19937 rng(7);
    // n = 2000 is about 17 minutes at the default 0.5 s step; 512 = pyannote's size.
    const auto emb = random_embeddings(rng, 2000, 512, 4, 0.4f);

    const auto t0 = std::chrono::steady_clock::now();
    const auto labels = muninn::detail::cluster_embeddings(emb, 0.7f, 10);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    const int found = labels.empty() ? 0 : *std::max_element(labels.begin(), labels.end()) + 1;
    std::printf("  n=2000, dim=512: %.2f s, %d clusters\n", seconds, found);
    CHECK(labels.size() == 2000);
    CHECK(seconds < 5.0);
    CHECK(found == 4);  // four well-separated speakers
}

} // namespace

int main() {
    test_sentence_splitter();
    test_clean_output();
    test_language_codes();
    test_clustering_matches_old();
    test_clustering_speed();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
