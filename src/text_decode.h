#pragma once

// Turning Whisper's output tokens back into text. Internal to Muninn (not in
// include/muninn): streaming uses it now, and the offline transcriber is meant
// to switch to it so both paths decode text the same way.
//
// Whisper uses GPT-2's byte-level BPE: every byte of UTF-8 text is stored as
// one "printable" Unicode character, so a token like "Ã©" is really the two
// bytes of "é", and "Ġ" (U+0120) is a space. Text has to be decoded byte by
// byte before it is shown, or anything outside ASCII comes out garbled. See
// Loki Studio's Docs/GPT2_BPE_ENCODING_FIX.md for the original bug.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace muninn {
namespace text {

// Maps GPT-2's byte characters back to the bytes they stand for.
class GPT2ByteDecoder {
public:
    GPT2ByteDecoder();

    // A token string as stored in the vocabulary, decoded to raw bytes (which
    // are UTF-8 once the tokens of a word are joined).
    std::string decode(const std::string& token) const;

private:
    std::unordered_map<uint32_t, uint8_t> decoder_;
};

// One shared decoder, built on first use (thread-safe).
const GPT2ByteDecoder& get_gpt2_decoder();

// The token decoded to bytes. A leading "Ġ" becomes a real space.
std::string clean_token(const std::string& token);

// The same, without the leading space.
std::string clean_token_raw(const std::string& token);

// True for any "<|...|>" token: special tokens and timestamps alike.
bool is_bracket_token(const std::string& token);

// The text of a token sequence: "<|...|>" tokens skipped, the rest decoded and
// joined, surrounding whitespace trimmed.
std::string extract_text(const std::vector<std::string>& tokens);

// faster-whisper's compression ratio: UTF-8 bytes over zlib-compressed bytes.
// Repetitive text compresses well, so a decoding loop scores high (the usual
// threshold is 2.4). Without zlib in the build, distinct 4-grams stand in.
float compression_ratio_of(const std::string& text);

// True when the bytes end partway through a UTF-8 character (or contain a
// broken sequence), i.e. more tokens are needed before they can be shown.
bool is_incomplete_utf8(const std::string& bytes);

// Languages written without spaces between words. Their words are split
// wherever the bytes so far form whole characters, as faster-whisper does.
bool is_spaceless_language(const std::string& language);

// One word built from consecutive text tokens.
struct WordTokens {
    std::string text;              // decoded bytes, with its leading space if any
    std::vector<size_t> token_indices;  // positions in the token list given
};

// Groups text tokens into words, as faster-whisper's split_to_word_tokens does.
// `tokens` are vocabulary strings and `ids` their ids, same length. A token id
// at or above `eot_id` (end of text, timestamps and the other specials) always
// stands alone. For languages with spaces, a token starting with a space or
// made only of punctuation starts a new word; otherwise it joins the previous
// one. For spaceless languages, a word ends wherever the bytes are whole
// characters.
std::vector<WordTokens> split_to_word_tokens(const std::vector<std::string>& tokens,
                                             const std::vector<size_t>& ids,
                                             size_t eot_id,
                                             const std::string& language);

// Moves opening punctuation onto the next word and closing punctuation onto the
// previous one, as faster-whisper's merge_punctuations does with its default
// sets. Words left with no text (emptied by the merge, or special tokens) are
// removed, so pass text tokens only.
void merge_punctuations(std::vector<WordTokens>& words);

}  // namespace text
}  // namespace muninn
