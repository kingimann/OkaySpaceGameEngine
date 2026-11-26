#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): the reader for an OkaySpace web game's data pack, so the browser
// can run a web game natively with the player (step 2 of docs/platform/browser_plan.md). The pack is the one the web export
// writes (scripts/web_package.py) and the web player reads (platform/web/okay_web.js); this is its native reader, written
// from that format as the script describes it:
//
//     8 bytes "OKWPACK1", u32 little-endian manifest length, the manifest (UTF-8 JSON
//     {"files": [{"path": "/a/b.png", "offset": N, "size": N}, ...]}), then the file bytes; each offset counts from the
//     end of the manifest. The web export gzips the whole pack (game.okaypack.gz); a plain pack is read too.
//
// A pack comes from a web site, so everything in it is untrusted:
//   * sizes are capped before anything is allocated (the download, the unpacked pack, the manifest, the file count);
//   * gzip is read by the engine's one inflater (okay/Core/Inflate.hpp), which checks the CRC-32 and length of the gzip
//     trailer (a damaged download is refused) and stops at the size cap (a "zip bomb" cannot fill memory);
//   * every file must lie inside the pack (offset + size, no overflow);
//   * every path must start with '/' and the rest must pass okay::pathsafety's Portable rules (no "..", no drive letter,
//     no device names, no absolute or UNC path, no control characters ...), and two files may not have the same name, also
//     not in a different letter case (Windows would write one over the other);
//   * unpacking writes into "<folder>.part" first and renames it to <folder> only when every file was written, and checks
//     with pathsafety::StaysInside that no link inside the folder leads out of it.
// What the format does NOT have: a hash per file or a signature, so a pack is only checked for being intact (the gzip
// CRC-32), not for who made it. Same as the web player, which trusts the site it came from.
// Standard library and the engine's header-only Core/Inflate.hpp, Core/Json.hpp and Core/PathSafety.hpp.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::browser {

struct GamePackLimits {
    std::size_t maxPackedBytes = 256u << 20;     ///< the pack as downloaded (gzip or plain)
    std::size_t maxUnpackedBytes = 512u << 20;   ///< the pack after gzip
    std::size_t maxManifestBytes = 8u << 20;
    std::size_t maxFiles = 20000;
};

struct GamePackFile {
    std::string path;        ///< relative, '/'-separated, as checked (the pack's "/a/b.png" is "a/b.png")
    std::size_t offset = 0;  ///< into GamePack::bytes
    std::size_t size = 0;
};

struct GamePack {
    std::vector<std::uint8_t> bytes;   ///< the whole pack after gzip
    std::vector<GamePackFile> files;   ///< in manifest order; offsets already include the header and manifest
    std::size_t TotalFileBytes() const { std::size_t n = 0; for (const auto& f : files) n += f.size; return n; }
};

/// Read a pack (gzip or plain) of `size` bytes. False with a plain-words reason in `why` when it is refused.
bool ReadGamePack(const std::uint8_t* data, std::size_t size, GamePack& out, std::string& why,
                  const GamePackLimits& limits = GamePackLimits());

/// Write the pack's files into `folder` (made; must not exist yet, or is replaced when `replace`). Written into
/// "<folder>.part" and renamed when complete, so a half-written game is never run. False with `why` on any error, and
/// nothing is left behind.
bool UnpackGamePack(const GamePack& pack, const std::string& folder, std::string& why, bool replace = true);

} // namespace okay::browser
