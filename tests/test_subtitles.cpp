// Subtitle export and caption engine checks. No model, no GPU.
// CI runs this with the other cpu tests (ctest -L cpu).
//
// Usage: test_subtitles            (exit code 0 = all checks passed)

#include "muninn/caption_options.h"
#include "muninn/caption_router.h"
#include "muninn/subtitle_export.h"
#include "muninn/word_styling.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
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

using muninn::Segment;
using muninn::SubtitleEntry;
using muninn::SubtitleExporter;
using muninn::SubtitleExportOptions;
using muninn::Word;

// UTF-8 code points, not counting line breaks.
size_t code_points(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80 && c != '\n' && c != '\r') ++n;
    }
    return n;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> words_of(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

// Text with every whitespace character removed.
std::string squeeze(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') out += c;
    }
    return out;
}

std::string all_text(const std::vector<SubtitleEntry>& entries) {
    std::string out;
    for (const auto& e : entries) {
        if (!out.empty()) out += " ";
        out += e.text;
    }
    return out;
}

Segment make_segment(float start, float end, const std::string& text, int track = 0) {
    Segment s;
    s.start = start;
    s.end = end;
    s.text = text;
    s.track_id = track;
    return s;
}

// A segment whose words are spread evenly over [start, end].
Segment segment_with_words(float start, float end, const std::vector<std::string>& words,
                           int track = 0, const char* joiner = " ") {
    Segment s;
    s.start = start;
    s.end = end;
    s.track_id = track;
    const float step = (end - start) / static_cast<float>(words.size());
    for (size_t i = 0; i < words.size(); ++i) {
        Word w;
        w.word = words[i];
        w.start = start + step * static_cast<float>(i);
        w.end = w.start + step * 0.9f;
        w.probability = 0.9f;
        s.words.push_back(w);
        if (i) s.text += joiner;
        s.text += words[i];
    }
    return s;
}

bool no_overlaps(const std::vector<SubtitleEntry>& entries) {
    for (size_t i = 0; i + 1 < entries.size(); ++i) {
        if (entries[i].end > entries[i + 1].start + 1e-6f) {
            std::printf("    overlap: #%d ends %.3f, #%d starts %.3f\n", entries[i].index,
                        entries[i].end, entries[i + 1].index, entries[i + 1].start);
            return false;
        }
    }
    return true;
}

bool sorted_and_numbered(const std::vector<SubtitleEntry>& entries) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].index != static_cast<int>(i + 1)) return false;
        if (i && entries[i].start < entries[i - 1].start) return false;
    }
    return true;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::filesystem::path u8(const std::string& s) { return std::filesystem::u8path(s); }

// ─────────────────────────────────────────────────────────────

void test_split_without_loss() {
    std::printf("[long text split without loss]\n");

    std::string text;
    for (int i = 0; i < 60; ++i) text += (i ? " word" : "word") + std::to_string(i);

    SubtitleExportOptions opt;  // 42 chars x 2 lines
    auto entries = SubtitleExporter::segments_to_entries({make_segment(0.0f, 30.0f, text)}, opt);

    CHECK(entries.size() > 1);  // too much for one cue: more cues, not lost words
    CHECK(words_of(all_text(entries)) == words_of(text));
    for (const auto& e : entries) {
        const auto lines = split(e.text, '\n');
        CHECK(lines.size() <= 2);
        for (const auto& line : lines) CHECK(code_points(line) <= 42);
    }
    // The pieces share the segment's time, in order, without gaps or overlap.
    CHECK(std::fabs(entries.front().start - 0.0f) < 1e-4f);
    CHECK(no_overlaps(entries));
    CHECK(sorted_and_numbered(entries));

    // split_text itself keeps every word, however many lines it takes.
    const std::string split_all = SubtitleExporter::split_text(text, 42, 2);
    CHECK(words_of(split_all) == words_of(text));
    CHECK(split(split_all, '\n').size() > 2);

    // With word timings, each piece takes the time of its words.
    std::vector<std::string> ws;
    for (int i = 0; i < 40; ++i) ws.push_back("token" + std::to_string(i));
    auto timed = SubtitleExporter::segments_to_entries({segment_with_words(10.0f, 30.0f, ws)}, opt);
    CHECK(timed.size() > 1);
    CHECK(words_of(all_text(timed)) == ws);
    size_t word_total = 0;
    for (const auto& e : timed) word_total += e.words.size();
    CHECK(word_total == ws.size());
    for (const auto& e : timed) {
        if (!e.words.empty() && &e != &timed.front()) CHECK(std::fabs(e.start - e.words.front().start) < 1e-4f);
    }
}

void test_line_lengths_non_ascii() {
    std::printf("[CJK and Cyrillic line lengths]\n");

    // Cyrillic: 6-letter words are 12 bytes each. Counting bytes would give
    // one word per line; counting characters fits three on a 20-char line.
    const std::string privet = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";  // привет
    std::string cyr;
    for (int i = 0; i < 6; ++i) cyr += (i ? " " : "") + privet;
    const std::string cyr_split = SubtitleExporter::split_text(cyr, 20, 2);
    const auto cyr_lines = split(cyr_split, '\n');
    CHECK(cyr_lines.size() == 2);
    for (const auto& line : cyr_lines) CHECK(code_points(line) <= 20);
    CHECK(words_of(cyr_split) == words_of(cyr));

    // Russian splitter: a single-letter preposition moved to the next line
    // must not be lost.
    const std::string v = "\xD0\xB2";  // в
    std::string ru = privet + " " + privet + " " + v + " " + privet + " " + privet;
    const std::string ru_split = SubtitleExporter::split_russian_text(ru, 16, 2, true);
    CHECK(words_of(ru_split) == words_of(ru));

    // Spanish splitter: same for a short word.
    const std::string es = "esta es una frase bastante larga de prueba para el subtitulo final";
    const std::string es_split = SubtitleExporter::split_spanish_text(es, 20, 2, true);
    CHECK(words_of(es_split) == words_of(es));

    // Japanese, Chinese, Korean, Thai: every character kept, lines within the limit.
    std::string ja;
    for (int i = 0; i < 50; ++i) ja += "\xE3\x81\x82";  // あ
    const std::string ja_split = SubtitleExporter::split_japanese_text(ja, 22, 2);
    CHECK(squeeze(ja_split) == ja);
    for (const auto& line : split(ja_split, '\n')) CHECK(code_points(line) <= 22);

    std::string zh;
    for (int i = 0; i < 45; ++i) zh += "\xE4\xB8\xAD";  // 中
    const std::string zh_split = SubtitleExporter::split_chinese_text(zh, 18, 2, true);
    CHECK(squeeze(zh_split) == zh);
    CHECK(split(zh_split, '\n').size() == 3);
    for (const auto& line : split(zh_split, '\n')) CHECK(code_points(line) <= 18);

    const std::string hangul_word = "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94";  // 안녕하세요
    std::string ko;
    for (int i = 0; i < 12; ++i) ko += (i ? " " : "") + hangul_word;
    const std::string ko_split = SubtitleExporter::split_korean_text(ko, 23, 2, true);
    CHECK(squeeze(ko_split) == squeeze(ko));

    std::string th;
    for (int i = 0; i < 100; ++i) th += "\xE0\xB8\x81";  // ก
    const std::string th_split = SubtitleExporter::split_thai_text(th, 40, 2);
    CHECK(squeeze(th_split) == th);
}

void test_no_merge_across_tracks() {
    std::printf("[no merge across tracks, no minutes-long cues]\n");

    SubtitleExportOptions opt;
    CHECK(opt.gap_threshold == 0.0f);  // merging is opt-in now

    // Touching segments on two tracks, merging on: never merged.
    opt.gap_threshold = 0.5f;
    auto two_tracks = SubtitleExporter::segments_to_entries(
        {make_segment(0.0f, 1.0f, "mic says hi", 0), make_segment(1.05f, 2.0f, "game says boom", 1)}, opt);
    CHECK(two_tracks.size() == 2);
    for (const auto& e : two_tracks) {
        CHECK(e.text == "mic says hi" || e.text == "game says boom");
    }

    // Same track merges.
    auto one_track = SubtitleExporter::segments_to_entries(
        {make_segment(0.0f, 1.0f, "hello", 0), make_segment(1.05f, 2.0f, "there", 0)}, opt);
    CHECK(one_track.size() == 1);
    if (!one_track.empty()) CHECK_EQ_STR(one_track[0].text, "hello there");

    // 200 touching one-second segments: no cue longer than max_duration.
    std::vector<Segment> chain;
    for (int i = 0; i < 200; ++i) chain.push_back(make_segment(float(i), float(i) + 0.99f, "ok"));
    auto chained = SubtitleExporter::segments_to_entries(chain, opt);
    CHECK(chained.size() > 20);
    for (const auto& e : chained) CHECK(e.end - e.start <= opt.max_duration + 1e-4f);
    CHECK(words_of(all_text(chained)).size() == 200);

    // Default (no merging): one cue per segment.
    SubtitleExportOptions plain;
    CHECK(SubtitleExporter::segments_to_entries(chain, plain).size() == 200);

    // Out-of-order input comes out sorted and numbered.
    auto shuffled = SubtitleExporter::segments_to_entries(
        {make_segment(5.0f, 6.0f, "third"), make_segment(0.0f, 1.0f, "first"), make_segment(2.0f, 3.0f, "second")},
        plain);
    CHECK(sorted_and_numbered(shuffled));
    if (shuffled.size() == 3) CHECK_EQ_STR(shuffled[0].text, "first");

    // Word-based segmenters never put two tracks' words in one cue.
    SubtitleExportOptions ru;
    ru.enable_russian_mode = true;
    auto mixed = SubtitleExporter::build_entries(
        {segment_with_words(0.0f, 2.0f, {"aaa", "bbb", "ccc"}, 0),
         segment_with_words(0.1f, 2.1f, {"xxx", "yyy", "zzz"}, 1)}, ru);
    for (const auto& e : mixed) {
        const std::string t = e.text;
        const bool has_mic = t.find("aaa") != std::string::npos || t.find("bbb") != std::string::npos ||
                             t.find("ccc") != std::string::npos;
        const bool has_game = t.find("xxx") != std::string::npos || t.find("yyy") != std::string::npos ||
                              t.find("zzz") != std::string::npos;
        CHECK(!(has_mic && has_game) || t.find('\n') != std::string::npos);  // only a same-start merge
    }
    CHECK(words_of(all_text(mixed)).size() == 6);
}

void test_no_overlaps() {
    std::printf("[no overlapping cues]\n");

    // Karaoke holds each phrase 1.4 s after its last word.
    std::vector<std::string> words;
    for (int i = 0; i < 30; ++i) words.push_back("w" + std::to_string(i) + (i % 5 == 4 ? "." : ""));
    SubtitleExportOptions karaoke;
    karaoke.enable_karaoke_mode = true;
    auto k = SubtitleExporter::build_entries({segment_with_words(0.0f, 9.0f, words)}, karaoke);
    CHECK(k.size() > 1);
    CHECK(no_overlaps(k));
    CHECK(words_of(all_text(k)) == words);

    // Korean min-duration padding (1 s) with quick short phrases.
    SubtitleExportOptions ko;
    ko.enable_korean_mode = true;
    std::vector<Segment> quick;
    for (int i = 0; i < 6; ++i) {
        quick.push_back(segment_with_words(i * 0.5f, i * 0.5f + 0.2f, {"\xEB\x84\xA4."}));  // 네.
    }
    auto q = SubtitleExporter::build_entries(quick, ko);
    CHECK(q.size() == 6);
    CHECK(no_overlaps(q));

    // Overlapping segments on two tracks.
    SubtitleExportOptions plain;
    auto t = SubtitleExporter::segments_to_entries(
        {make_segment(0.0f, 3.0f, "track zero", 0), make_segment(1.0f, 4.0f, "track one", 1),
         make_segment(1.0f, 2.0f, "same start", 2)}, plain);
    CHECK(no_overlaps(t));
    CHECK(all_text(t).find("track zero") != std::string::npos);
    CHECK(all_text(t).find("track one") != std::string::npos);
    CHECK(all_text(t).find("same start") != std::string::npos);
    for (const auto& e : t) CHECK(e.end > e.start);
}

void test_escaping() {
    std::printf("[escaping]\n");

    SubtitleEntry e;
    e.index = 1;
    e.start = 1.0f;
    e.end = 2.0f;
    e.text = "I <3 AT&T --> ok";

    SubtitleExportOptions opt;
    const std::string vtt = SubtitleExporter::format_vtt_entry(e, opt);
    CHECK(vtt.find("I &lt;3 AT&amp;T --&gt; ok") != std::string::npos);
    CHECK(vtt.find("<3") == std::string::npos);
    // The only "-->" left is the timing line's.
    CHECK(vtt.find("-->") == vtt.rfind("-->"));

    const std::string srt = SubtitleExporter::format_srt_entry(e);
    CHECK(srt.find("I <3 AT&T -> ok") != std::string::npos);
    CHECK(srt.find("-->") == srt.rfind("-->"));

    e.text = "a ---> b";
    const std::string srt2 = SubtitleExporter::format_srt_entry(e);
    CHECK(srt2.find("-->") == srt2.rfind("-->"));

    // Word-by-word VTT escapes each word.
    e.text = "x<y & z";
    Word w1; w1.word = "x<y"; w1.start = 1.0f; w1.end = 1.4f;
    Word w2; w2.word = "&"; w2.start = 1.4f; w2.end = 1.6f;
    Word w3; w3.word = "z"; w3.start = 1.6f; w3.end = 2.0f;
    e.words = {w1, w2, w3};
    opt.vtt_include_word_timestamps = true;
    const std::string vtt_words = SubtitleExporter::format_vtt_entry(e, opt);
    CHECK(vtt_words.find("x&lt;y") != std::string::npos);
    CHECK(vtt_words.find(">&amp;") != std::string::npos);
    CHECK(vtt_words.find("x<y") == std::string::npos);

    // HTML formatter for OBS.
    muninn::WordStyle style{};
    style.color = "#FFFFFF";
    const std::string html = muninn::HTMLFormatter::format_word("<b>Tom & \"Jerry\"</b>", style);
    CHECK(html.find("&lt;b&gt;Tom &amp; &quot;Jerry&quot;&lt;/b&gt;") != std::string::npos);
}

void test_vtt_word_timestamps() {
    std::printf("[VTT word timestamps]\n");

    SubtitleEntry e;
    e.index = 3;
    e.start = 1.0f;
    e.end = 3.0f;
    e.text = "one two\nthree";
    e.speaker_id = 2;
    const char* texts[] = {"one", "two", "three"};
    const float starts[] = {0.8f, 1.5f, 2.25f};  // the first starts before the cue (lead-in)
    for (int i = 0; i < 3; ++i) {
        Word w;
        w.word = texts[i];
        w.start = starts[i];
        w.end = starts[i] + 0.4f;
        w.probability = (i == 1) ? 0.3f : 0.9f;
        e.words.push_back(w);
    }

    SubtitleExportOptions opt;
    opt.vtt_include_word_timestamps = true;
    opt.vtt_include_confidence_styling = true;
    const std::string vtt = SubtitleExporter::format_vtt_entry(e, opt);
    CHECK(vtt.find("<v Speaker2>one <00:00:01.500><c.uncertain>two</c>\n<00:00:02.250>three</v>") !=
          std::string::npos);
    if (vtt.find("<v Speaker2>") == std::string::npos) std::printf("    got: %s\n", vtt.c_str());
}

void test_timestamps() {
    std::printf("[timestamp rounding and negatives]\n");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(1.9996f), "00:00:02,000");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(1.2346f), "00:00:01,235");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(3599.9996f), "01:00:00,000");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(61.25f), "00:01:01,250");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(-5.0f), "00:00:00,000");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(-0.0004f), "00:00:00,000");
    CHECK_EQ_STR(SubtitleExporter::format_srt_timestamp(std::numeric_limits<float>::quiet_NaN()),
                 "00:00:00,000");
    CHECK_EQ_STR(SubtitleExporter::format_vtt_timestamp(7322.5f), "02:02:02.500");
    CHECK_EQ_STR(SubtitleExporter::format_ass_timestamp(3725.456f), "1:02:05.46");
    CHECK_EQ_STR(SubtitleExporter::format_ass_timestamp(-1.0f), "0:00:00.00");

    // A negative segment start is clamped in the entries too.
    SubtitleExportOptions opt;
    auto entries = SubtitleExporter::segments_to_entries({make_segment(-0.5f, 1.0f, "early")}, opt);
    CHECK(entries.size() == 1);
    if (!entries.empty()) CHECK(entries[0].start >= 0.0f);
}

void test_ass_shape(const std::filesystem::path& dir) {
    std::printf("[ASS output]\n");

    Segment a = make_segment(1.0f, 2.5f, "Hello there, this line is long enough to wrap onto two lines");
    a.speaker_id = 0;
    a.speaker_label = "Alice, the host";
    Segment b = make_segment(3.0f, 4.0f, "Braces {\\b1} and C:\\path");

    SubtitleExportOptions opt;
    opt.format = muninn::SubtitleFormat::ASS;
    opt.output_path = muninn::SubtitleExporter::generate_output_path(
        (dir / "clip.mp4").u8string(), muninn::SubtitleFormat::ASS);
    CHECK(opt.output_path.size() > 4 && opt.output_path.substr(opt.output_path.size() - 4) == ".ass");

    SubtitleExporter exporter;
    const std::string path = exporter.export_subtitles({a, b}, "unused.mp4", opt);
    const std::string ass = read_file(u8(path));

    CHECK(ass.rfind("[Script Info]", 0) == 0);
    CHECK(ass.find("ScriptType: v4.00+") != std::string::npos);
    CHECK(ass.find("[V4+ Styles]") != std::string::npos);
    CHECK(ass.find("\nStyle: Default,") != std::string::npos);
    CHECK(ass.find("[Events]") != std::string::npos);
    CHECK(ass.find("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text") !=
          std::string::npos);
    CHECK(ass.find("Dialogue: 0,0:00:01.00,0:00:02.50,Default,Alice  the host,0,0,0,,Hello there, this "
                   "line is long enough to\\Nwrap onto two lines") != std::string::npos);
    CHECK(ass.find("Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Braces \\{\\\\b1\\} and C:\\\\path") !=
          std::string::npos);

    // Every Dialogue line has the 9 commas before the text.
    std::istringstream lines(ass);
    std::string line;
    int dialogues = 0;
    while (std::getline(lines, line)) {
        if (line.rfind("Dialogue: ", 0) != 0) continue;
        ++dialogues;
        CHECK(std::count(line.begin(), line.end(), ',') >= 9);
    }
    CHECK(dialogues == 2);
}

void test_korean_karaoke_spacing() {
    std::printf("[Korean syllable karaoke spacing]\n");

    const std::string annyeong = "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94";  // 안녕하세요
    const std::string yeoreobun = "\xEC\x97\xAC\xEB\x9F\xAC\xEB\xB6\x84";                     // 여러분

    SubtitleExportOptions opt;
    opt.enable_korean_mode = true;
    opt.korean_syllable_karaoke = true;
    auto entries = SubtitleExporter::build_entries(
        {segment_with_words(0.0f, 2.0f, {annyeong, " " + yeoreobun})}, opt);
    CHECK(entries.size() == 1);
    if (!entries.empty()) {
        CHECK_EQ_STR(entries[0].text, annyeong + " " + yeoreobun);
        CHECK(entries[0].words.size() == 8);  // one per syllable, for highlighting
    }
}

void test_non_ascii_paths(const std::filesystem::path& base) {
    std::printf("[non-ASCII output paths]\n");

    // "José_田中_Привет"
    const std::string name = "Jos\xC3\xA9_\xE7\x94\xB0\xE4\xB8\xAD_\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    const std::filesystem::path dir = base / u8(name);
    std::filesystem::create_directories(dir);

    const std::string video = (dir / u8("vid\xC3\xA9o.mp4")).u8string();
    const std::string expected_srt = (dir / u8("vid\xC3\xA9o.srt")).u8string();
    CHECK_EQ_STR(SubtitleExporter::generate_output_path(video, muninn::SubtitleFormat::SRT), expected_srt);

    std::vector<Segment> segs = {make_segment(0.0f, 1.0f, "Caf\xC3\xA9 au lait")};
    SubtitleExporter exporter;
    const std::string srt = exporter.export_srt(segs, video);
    CHECK_EQ_STR(srt, expected_srt);
    CHECK(std::filesystem::exists(u8(srt)));
    CHECK(read_file(u8(srt)).find("Caf\xC3\xA9 au lait") != std::string::npos);

    const std::string vtt = exporter.export_vtt(segs, video);
    CHECK(std::filesystem::exists(u8(vtt)));
    CHECK(read_file(u8(vtt)).rfind("WEBVTT", 0) == 0);

    // overwrite_existing = false refuses to replace the file.
    SubtitleExportOptions keep;
    keep.overwrite_existing = false;
    bool threw = false;
    try {
        exporter.export_srt(segs, video, keep);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    // Metadata JSON round trip, two tracks, millisecond times, the device.
    Segment s0 = segment_with_words(1.2345f, 2.5f, {"Caf\xC3\xA9", "\"quoted\""}, 0);
    s0.speaker_id = 1;
    s0.speaker_label = "Speaker 1";
    Segment s1 = make_segment(3.001f, 4.0f, "line\nbreak", 1);
    const std::string meta = muninn::SubtitleMetadata::generate_metadata_json(
        {s0, s1}, video, "large-v3-turbo", "fr", 10.5f, "cpu");
    CHECK(std::filesystem::exists(u8(meta)));
    const std::string json = read_file(u8(meta));
    CHECK(json.find("\"device\": \"cpu\"") != std::string::npos);
    CHECK(json.find("\"start\": 1.234") != std::string::npos || json.find("\"start\": 1.235") != std::string::npos);
    CHECK(json.find("\"channel_2\"") != std::string::npos);

    const auto loaded = muninn::SubtitleMetadata::load_from_metadata_json(meta);
    CHECK(loaded.size() == 2);
    if (loaded.size() == 2) {
        CHECK(std::fabs(loaded[0].start - 1.2345f) < 0.001f);
        CHECK(loaded[0].track_id == 0);
        CHECK(loaded[0].words.size() == 2);
        if (loaded[0].words.size() == 2) CHECK_EQ_STR(loaded[0].words[1].word, "\"quoted\"");
        CHECK(loaded[0].speaker_id == 1);
        CHECK_EQ_STR(loaded[0].speaker_label, "Speaker 1");
        CHECK_EQ_STR(loaded[0].language, "fr");
        CHECK(loaded[1].track_id == 1);
        CHECK_EQ_STR(loaded[1].text, "line\nbreak");
    }

    bool bad_threw = false;
    const std::filesystem::path bad = dir / "bad.json";
    { std::ofstream(bad) << "{ \"transcriptions\": [1, 2"; }
    try {
        muninn::SubtitleMetadata::load_from_metadata_json(bad.u8string());
    } catch (const std::exception&) {
        bad_threw = true;
    }
    CHECK(bad_threw);
}

// ─────────────────────────────────────────────────────────────
// Caption engine

void test_caption_engine() {
    std::printf("[caption engine]\n");

    auto& router = muninn::CaptionRouter::instance();
    CHECK(router.engineCount() > 0);  // registered without being asked
    const size_t before = router.engineCount();
    router.registerDefaultEngines();  // safe, and does nothing the second time
    CHECK(router.engineCount() == before);
    CHECK(router.getEngine("xx-unknown") != nullptr);

    // Long text: every word ends up in a cue, cues never overlap, lines fit.
    std::vector<std::string> words;
    for (int i = 0; i < 80; ++i) words.push_back("caption" + std::to_string(i));
    Segment seg = segment_with_words(0.0f, 20.0f, words);
    seg.language = "en";

    muninn::CaptionEngineOverrides overrides;
    overrides.breakOnSentence = false;
    overrides.maxCps = 1000.0f;  // leave it to the line limit
    const auto doc = router.process({seg}, overrides);
    std::string joined;
    for (const auto& cue : doc.cues) {
        joined += " " + cue.text;
        CHECK(cue.lineCount <= 2);
        for (const auto& line : cue.lines) CHECK(line.size() <= 42);
    }
    CHECK(words_of(joined) == words);
    for (size_t i = 0; i + 1 < doc.cues.size(); ++i) {
        CHECK(doc.cues[i].end <= doc.cues[i + 1].start + 1e-6f);
        CHECK(doc.cues[i].index == static_cast<int>(i + 1));
    }

    // Presentation: a cue is cleared by the time the next one shows.
    std::vector<float> shows(doc.cues.size(), -1.0f), clears(doc.cues.size(), -1.0f);
    for (const auto& e : doc.events) {
        if (e.cueIndex < 0 || e.cueIndex >= static_cast<int>(doc.cues.size())) continue;
        if (e.type == muninn::PresentationEventType::CueShow) shows[e.cueIndex] = e.time;
        if (e.type == muninn::PresentationEventType::CueClear) clears[e.cueIndex] = e.time;
    }
    CHECK(!doc.events.empty());
    for (size_t i = 0; i + 1 < doc.cues.size(); ++i) {
        CHECK(clears[i] >= 0.0f && shows[i + 1] >= 0.0f);
        CHECK(clears[i] <= shows[i + 1] + 1e-4f);
        CHECK(shows[i] <= clears[i]);
    }

    // Japanese never loses characters either.
    Segment ja;
    ja.language = "ja";
    for (int i = 0; i < 70; ++i) {
        Word w;
        w.word = "\xE3\x81\x82";  // あ
        w.start = 0.1f * i;
        w.end = w.start + 0.09f;
        ja.words.push_back(w);
    }
    muninn::CaptionEngineOverrides ja_over;
    ja_over.maxCps = 1000.0f;
    const auto ja_doc = router.process({ja}, ja_over);
    std::string ja_text;
    for (const auto& cue : ja_doc.cues) ja_text += squeeze(cue.text);
    CHECK(code_points(ja_text) == 70);

    // Korean keeps the spaces between words.
    Segment ko = segment_with_words(0.0f, 1.0f,
        {"\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94", "\xEC\x97\xAC\xEB\x9F\xAC\xEB\xB6\x84"});
    ko.language = "ko";
    const auto ko_doc = router.process({ko});
    CHECK(ko_doc.cues.size() == 1);
    if (!ko_doc.cues.empty()) {
        CHECK_EQ_STR(ko_doc.cues[0].text,
                     "\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94 \xEC\x97\xAC\xEB\x9F\xAC\xEB\xB6\x84");
    }

    // Overrides: a value equal to the default still counts.
    muninn::CaptionEngineOptions japanese = muninn::CaptionPresets::japanese();
    CHECK(japanese.maxCharsPerLine == 22);
    muninn::CaptionEngineOverrides o;
    o.maxCharsPerLine = 42;
    o.balanceLines = false;
    japanese.merge(o);
    CHECK(japanese.maxCharsPerLine == 42);
    CHECK(japanese.breakOnSentence == true);  // unset: preset kept

    muninn::CaptionEngineOptions english = muninn::CaptionPresets::english();
    muninn::CaptionEngineOverrides off;
    off.breakOnSentence = false;
    english.merge(off);
    CHECK(english.breakOnSentence == false);
    CHECK(english.avoidOrphanWords == true);  // unset: preset kept
}

void test_line_layout() {
    std::printf("[balanceLines and avoidOrphanWords]\n");

    auto& router = muninn::CaptionRouter::instance();

    // 43 characters: the greedy split leaves "dog" alone on line two.
    const std::vector<std::string> words = {"the", "quick", "brown", "fox", "jumps",
                                            "over", "the", "lazy", "dog"};
    Segment seg = segment_with_words(0.0f, 4.0f, words);
    seg.language = "en";

    muninn::CaptionEngineOverrides base;
    base.breakOnSentence = false;
    base.maxCps = 1000.0f;
    base.maxCharsPerLine = 40;
    base.avoidOrphanWords = false;
    base.balanceLines = false;
    const auto greedy = router.process({seg}, base);
    CHECK(greedy.cues.size() == 1);
    if (!greedy.cues.empty()) {
        CHECK(greedy.cues[0].lines.size() == 2);
        if (greedy.cues[0].lines.size() == 2) CHECK_EQ_STR(greedy.cues[0].lines[1], "dog");
    }

    muninn::CaptionEngineOverrides orphan = base;
    orphan.avoidOrphanWords = true;
    const auto no_orphan = router.process({seg}, orphan);
    if (!no_orphan.cues.empty() && no_orphan.cues[0].lines.size() == 2) {
        CHECK_EQ_STR(no_orphan.cues[0].lines[1], "lazy dog");
        CHECK(words_of(no_orphan.cues[0].text) == words);
    } else {
        CHECK(false);
    }

    muninn::CaptionEngineOverrides balance = base;
    balance.balanceLines = true;
    const auto balanced = router.process({seg}, balance);
    if (!balanced.cues.empty() && balanced.cues[0].lines.size() == 2) {
        const auto& l = balanced.cues[0].lines;
        const long diff = static_cast<long>(l[0].size()) - static_cast<long>(l[1].size());
        CHECK(std::labs(diff) <= 6);
        CHECK(words_of(balanced.cues[0].text) == words);
    } else {
        CHECK(false);
    }
}

}  // namespace

int main() {
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / "muninn_test_subtitles";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base);

    test_split_without_loss();
    test_line_lengths_non_ascii();
    test_no_merge_across_tracks();
    test_no_overlaps();
    test_escaping();
    test_vtt_word_timestamps();
    test_timestamps();
    test_ass_shape(base);
    test_korean_karaoke_spacing();
    test_non_ascii_paths(base);
    test_caption_engine();
    test_line_layout();

    std::filesystem::remove_all(base, ec);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
