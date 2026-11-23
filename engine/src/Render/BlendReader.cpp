// Our own Blender (.blend) reader. A .blend file is a stream of blocks (a 4 byte code, a size, the
// writer's memory address of the block, the index of the struct it holds and an element count) plus
// one "DNA1" block that describes every struct the writer knew (names, types, field order). The
// reader is driven by that description: every field is found by name and offset in the file's own
// layout, so one reader covers Blender 2.4x to 4.x, 32 and 64 bit pointers, both endians. Pointers in
// the file are the writer's old addresses; they are resolved through a table of block start addresses.
// Written from the public description of the file layout (the "DNA" self description), with no code
// from any other importer.
//
// Reads: objects (transforms, parenting incl. parent inverse, bone parents), meshes in both the old
// (MVert / MFace / MPoly + MLoop) and the new attribute-based storage, UVs, materials (legacy colours
// and image textures, node based Principled colours and image textures), armatures and vertex group
// skin weights, and action F-Curves (location, rotation, scale of objects and pose bones).
// Mirror and Subdivision Surface modifiers are applied in stack order (BlendModifiers.cpp); other modifiers are named.
// Everything it did not read is listed in ImportedModel::notes. See docs/content/model_readers.md.
#include "FileOpen.hpp"
#include "okay/Render/ModelData.hpp"
#include "BlendReaderInternal.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "BlendModifiers.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {
namespace blnd {

// ---- public entry ----------------------------------------------------------------------------

static bool Gunzip(const std::string& in, std::vector<u8>& out, std::string& err) {
    const u8* d = (const u8*)in.data();
    const std::size_t n = in.size();
    if (n < 18 || d[2] != 8) { err = "Blend: bad gzip header"; return false; }
    const u8 flg = d[3];
    std::size_t p = 10;
    if (flg & 4) { if (p + 2 > n) { err = "Blend: truncated gzip header"; return false; } p += 2 + (std::size_t)(d[p] | (d[p + 1] << 8)); }
    if (flg & 8) { while (p < n && d[p]) ++p; ++p; }
    if (flg & 16) { while (p < n && d[p]) ++p; ++p; }
    if (flg & 2) p += 2;
    if (p + 8 > n) { err = "Blend: truncated gzip stream"; return false; }
    if (!okay::Inflate(d + p, n - p - 8, false, out, kMaxInflated)) { err = "Blend: the gzip data is damaged or larger than " + std::to_string(kMaxInflated >> 20) + " MiB"; return false; }
    const std::size_t t = n - 4;
    const std::uint32_t isize = (std::uint32_t)d[t] | ((std::uint32_t)d[t + 1] << 8) | ((std::uint32_t)d[t + 2] << 16) | ((std::uint32_t)d[t + 3] << 24);
    if (isize != (std::uint32_t)(out.size() & 0xffffffffu)) { err = "Blend: the gzip data does not match its stored size"; return false; }
    return true;
}

} // namespace blnd

bool ReadBlendBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    auto fail = [&](const std::string& m) { out = ImportedModel{}; if (err) *err = m; return false; };
    if (bytes.size() > blnd::kMaxFileBytes) return fail("Blend: file too large");
    blnd::Reader r;
    r.baseDir = baseDir;
    r.flatten = flatten;
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0x1f && (unsigned char)bytes[1] == 0x8b) {
        std::string e;
        if (!blnd::Gunzip(bytes, r.f.buf, e)) return fail(e);
    } else if (bytes.size() >= 4 && (unsigned char)bytes[0] == 0x28 && (unsigned char)bytes[1] == 0xb5 && (unsigned char)bytes[2] == 0x2f && (unsigned char)bytes[3] == 0xfd) {
        return fail("Blend: this file is zstd compressed (Blender 3.0+ with 'Compress File' on), which is not supported yet; save it uncompressed or as gzip");
    } else {
        r.f.buf.assign(bytes.begin(), bytes.end());
    }
    if (!r.Run(out)) {
        const std::string m = !r.err.empty() ? r.err : (!r.f.err.empty() ? r.f.err : std::string("Blend: could not read file"));
        return fail(m);
    }
    out.backend = "own";
    return true;
}

bool ReadBlendFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    std::string bytes;
    detail::ReserveForFile(fp, bytes, blnd::kMaxFileBytes);
    char buf[65536];
    for (;;) {
        const std::size_t k = std::fread(buf, 1, sizeof(buf), fp);
        if (k == 0) break;
        if (bytes.size() + k > blnd::kMaxFileBytes) { std::fclose(fp); if (err) *err = "Blend: file too large"; return false; }
        bytes.append(buf, k);
    }
    std::fclose(fp);
    const std::size_t sl = path.find_last_of("/\\");
    return ReadBlendBuffer(bytes, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err, flatten);
}

} // namespace okay
