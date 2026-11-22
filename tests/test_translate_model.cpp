// Translator checks that need an NLLB model (a CTranslate2 conversion with
// sentencepiece.bpe.model beside it). Registered with ctest (labels
// model;gpu) only when MUNINN_TEST_NLLB_MODEL is set at configure time.
//
// Usage:
//   test_translate_model <nllb_model_dir> [device]   device: cuda (default) or cpu
//   test_translate_model <nllb_model_dir> --files-only
//
// --files-only runs just the path checks: it copies the tokenizer into
// temporary folders and never loads the model, so it needs no GPU.
//
// Exit code 0 = all checks passed.

#include "muninn/translator.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

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

bool contains(const std::string& s, const char* what) {
    return s.find(what) != std::string::npos;
}

// X1: the tokenizer must load from a folder with a non-ASCII name, and a
// missing tokenizer must be an error, never a silent fallback.
void test_paths(const std::string& model_dir) {
    std::printf("[paths]\n");
    std::error_code ec;
    const fs::path base = fs::temp_directory_path() / "muninn_translate_test";
    fs::remove_all(base, ec);

    // A folder without sentencepiece.bpe.model.
    const fs::path empty_dir = base / "no_tokenizer";
    fs::create_directories(empty_dir, ec);
    std::string message;
    try {
        muninn::Translator t(empty_dir.u8string(), "cpu");
        message = "(no exception)";
    } catch (const std::exception& e) {
        message = e.what();
    }
    std::printf("  missing tokenizer: %s\n", message.c_str());
    CHECK(contains(message, "SentencePiece"));

    // A non-ASCII folder holding only the tokenizer: loading must get past
    // the tokenizer and fail on the missing model.bin instead.
    const fs::path utf8_dir = base / fs::u8path(u8"José_田中");
    fs::create_directories(utf8_dir, ec);
    fs::copy_file(fs::u8path(model_dir) / "sentencepiece.bpe.model",
                  utf8_dir / "sentencepiece.bpe.model", fs::copy_options::overwrite_existing, ec);
    CHECK(!ec);
    message.clear();
    try {
        muninn::Translator t(utf8_dir.u8string(), "cpu");
        message = "(no exception)";
    } catch (const std::exception& e) {
        message = e.what();
    }
    std::printf("  non-ASCII folder, tokenizer only: %s\n", message.c_str());
    CHECK(!contains(message, "SentencePiece"));
    CHECK(message != "(no exception)");

    fs::remove_all(base, ec);
}

void test_translation(const std::string& model_dir, const std::string& device) {
    std::printf("[translation on %s]\n", device.c_str());
    muninn::Translator t(model_dir, device);
    CHECK(t.is_loaded());

    // X2/X3: blanks pass through as "", real text is translated.
    const std::vector<std::string> in = {
        "", "   ", "Hello, how are you?", "It costs 3.5 dollars. Dr. Smith agreed."};
    auto r = t.translate_batch_result(in, "en", "fr");
    CHECK(r.texts.size() == in.size());
    CHECK(r.texts[0].empty());
    CHECK(r.texts[1].empty());
    CHECK(!r.texts[2].empty() && r.texts[2] != in[2]);
    CHECK(!r.texts[3].empty() && r.texts[3] != in[3]);
    CHECK(r.failed_count == 0);
    CHECK(!r.cancelled);
    std::printf("  \"%s\" -> \"%s\"\n", in[3].c_str(), r.texts[3].c_str());

    // Codes are normalised.
    CHECK(!t.translate("Good morning.", "EN-us", "fr_FR").empty());

    // Unsupported language: "" and an error, never the source.
    auto bad = t.translate_batch_result({"Hello.", "", "Bye."}, "en", "xx");
    CHECK(bad.texts[0].empty() && bad.texts[1].empty() && bad.texts[2].empty());
    CHECK(bad.failed_count == 2);
    CHECK(!bad.error.empty());

    // Same language: the source is the answer.
    CHECK(t.translate("Hello.", "en", "en-GB") == "Hello.");

    // Cancelled: "" and the flag.
    t.cancel();
    auto c = t.translate_batch_result({"Hello."}, "en", "fr");
    CHECK(c.cancelled);
    CHECK(c.texts[0].empty());
    CHECK(c.failed_count == 1);
    t.reset_cancel();
    CHECK(!t.translate("Hello.", "en", "fr").empty());

    // Unloaded: "" with an error.
    CHECK(t.unload_model());
    auto u = t.translate_batch_result({"Hello."}, "en", "fr");
    CHECK(u.texts[0].empty());
    CHECK(!u.error.empty());
}

// X5: float16 asked for on the CPU falls back to float32 instead of throwing.
void test_cpu_float16(const std::string& model_dir) {
    std::printf("[float16 on cpu]\n");
    bool ok = false;
    try {
        muninn::Translator t(model_dir, "cpu", "float16");
        ok = t.is_loaded() && !t.translate("Thank you.", "en", "de").empty();
    } catch (const std::exception& e) {
        std::printf("  threw: %s\n", e.what());
    }
    CHECK(ok);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: test_translate_model <nllb_model_dir> [cuda|cpu|--files-only]\n");
        return 2;
    }
    const std::string model_dir = argv[1];
    const std::string mode = argc > 2 ? argv[2] : "cuda";

    test_paths(model_dir);
    if (mode != "--files-only") {
        test_translation(model_dir, mode);
        test_cpu_float16(model_dir);
    }

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
