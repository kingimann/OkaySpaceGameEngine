#pragma once
// ---------------------------------------------------------------------------
// okay::brotli: the engine's Brotli decoder (RFC 7932, "Brotli Compressed Data Format").
//
// Used by WOFF2 web fonts (okay/Browser/WebFont.hpp). Decoding only; there is no Brotli encoder in the engine.
//
// Provenance: written from the text of RFC 7932 only (sections 1 to 10 for the format; section 12 for the limits). No Brotli
// or WOFF2 source (Google's brotli or woff2, fontTools, browsers, any port) was read, fetched, copied or followed, and the
// sample C fragments printed in the RFC (the inverse move-to-front, the Ferment functions, the CRC-32 of Appendix C) were
// not pasted: those steps are written in our own words. The one thing taken from the RFC as it is, is data: the static
// dictionary (Appendix A) and the list of 121 word transforms (Appendix B), turned into engine/src/Core/BrotliDictionary.inc
// by scripts/gen_brotli_dictionary.py from the RFC's text (licence: engine/src/Core/BrotliDictionary-LICENSE.txt). Every
// other table (insert, copy and block-count lengths, the literal context lookups, the word counts per length) is worked
// out from the rule the RFC states, and tests/test_brotli.cpp checks it against the RFC's printed tables and CRC-32 values.
//
// Hostile input is the normal case:
//  - the output is capped (Options::maxOutput) and checked before each meta-block's memory is taken;
//  - every prefix code must be complete (RFC 7932 sections 3.4 and 3.5), every count and every repeat is bounds checked,
//    every back-reference stays inside the output, every dictionary reference inside the dictionary;
//  - no recursion; the input is read through a bounds-checked bit reader (reading past the end is an error, never a read
//    of memory past the input); a stream that does no work per command cannot loop for ever (a command budget);
//  - the large-window extension (window bits above 24) is not part of RFC 7932 and is refused.
// Memory: the output (at most maxOutput) and per meta-block prefix-code tables (a few megabytes in the worst case).
// Standard library only.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::brotli {

struct Options {
    std::size_t maxOutput = std::size_t(1) << 31;   ///< output cap; a stream that would decode to more is refused
    bool allowTrailing = false;                     ///< bytes after the end of the stream are ignored instead of refused
};

/// Decodes a whole Brotli stream. On success `out` holds exactly the decoded bytes and `consumed` the input bytes the stream
/// used. On any problem `out` is cleared, `why` says what and the result is false. Input may be any pointer when n == 0.
bool Decode(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out, const Options& op, std::size_t& consumed,
            std::string& why);
inline bool Decode(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out, const Options& op, std::string& why) {
    std::size_t used = 0;
    return Decode(in, n, out, op, used, why);
}

// ---- the tables, for tests (tests/test_brotli.cpp checks each against the RFC) -------------------------------------------
namespace detail {
/// RFC 7932 Appendix A: the static dictionary (122,784 bytes).
const std::uint8_t* Dictionary();
std::size_t DictionarySize();
/// Appendix A: NDBITS[length] (0..24), and DOFFSET[length] worked out from it (section 8).
int WordBits(int length);
std::uint32_t WordOffset(int length);
/// Appendix B: transform `id` (0..120): prefix, elementary transform (0 Identity, 1 FermentFirst, 2 FermentAll, 3..11
/// OmitFirst1..9, 12..20 OmitLast1..9) and suffix.
struct TransformInfo { const char* prefix; int kind; const char* suffix; };
TransformInfo Transform(int id);
/// Section 8: word transformed by transform `id` into `dst` (room for 64 bytes); returns the length written.
int TransformWord(const std::uint8_t* word, int length, int id, std::uint8_t* dst);
/// Section 7.1: the lookup tables Lut0, Lut1 and Lut2 (table 0..2, i 0..255).
std::uint8_t ContextLut(int table, int i);
/// Section 5: insert length codes 0..23, copy length codes 0..23; section 6: block count codes 0..25. Base and extra bits.
std::uint32_t InsertBase(int code);
int InsertExtra(int code);
std::uint32_t CopyBase(int code);
int CopyExtra(int code);
std::uint32_t BlockCountBase(int code);
int BlockCountExtra(int code);
} // namespace detail

} // namespace okay::brotli
