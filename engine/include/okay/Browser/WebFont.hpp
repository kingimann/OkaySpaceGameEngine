#pragma once
// Browser (page engine): web font files. A WOFF 1.0 file (W3C "WOFF File Format 1.0", 2012) turned back into the TrueType /
// OpenType (sfnt) file it was made from, so the font rasterizer can read it; a TrueType / OpenType file is passed through.
// WOFF tables are zlib streams: they go through the engine's one inflate (okay/Core/Inflate.hpp). A WOFF2 file (W3C "WOFF File
// Format 2.0") is one Brotli stream (okay/Core/Brotli.hpp) with the transformed glyf / loca and hmtx tables rebuilt; a WOFF2
// collection becomes a TrueType collection ("ttcf").
// Standard library only.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::browser {

enum class FontFileKind { Unknown, Sfnt, Woff, Woff2 };
/// What the first bytes say the file is: TrueType (0x00010000, "true"), OpenType ("OTTO"), WOFF ("wOFF"), WOFF2 ("wOF2").
FontFileKind FontFileKindOf(const std::uint8_t* data, std::size_t size);

struct WebFontLimits {
    std::size_t maxSfntBytes = 32u << 20;   ///< the rebuilt font may be at most this big
    std::size_t maxTables = 512;
};

/// A web font's bytes as an sfnt file the rasterizer reads: a TrueType / OpenType file as it is, a WOFF or WOFF2 file decoded
/// (table checksums and head.checkSumAdjustment are worked out again for WOFF2, which does not store them). False (and `why`)
/// for unknown files and broken or hostile WOFF / WOFF2 files (bad offsets, sizes that do not add up, a table that does not
/// decompress to its stated size, a glyph stream that runs out, too big).
bool DecodeWebFont(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& sfnt, std::string& why,
                   const WebFontLimits& limits = WebFontLimits());

} // namespace okay::browser
