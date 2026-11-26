#pragma once
// Browser (page engine): web font files. A WOFF 1.0 file (W3C "WOFF File Format 1.0", 2012) turned back into the TrueType /
// OpenType (sfnt) file it was made from, so the font rasterizer can read it; a TrueType / OpenType file is passed through.
// WOFF tables are zlib streams: they go through the engine's one inflate (okay/Core/Inflate.hpp). WOFF2 is not read (it needs
// Brotli with the RFC 7932 static dictionary, a data table we have not added: see docs/platform/browser_plan.md).
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

/// A web font's bytes as an sfnt file the rasterizer reads: a TrueType / OpenType file as it is, a WOFF file decoded.
/// False (and `why`) for WOFF2, unknown files and broken or hostile WOFF files (bad offsets, sizes that do not add up, a table
/// that does not inflate to its stated size, too big).
bool DecodeWebFont(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& sfnt, std::string& why,
                   const WebFontLimits& limits = WebFontLimits());

} // namespace okay::browser
