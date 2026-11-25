// Browser: the web game pack reader (see okay/Browser/GamePack.hpp for the format and the rules).
#include "okay/Browser/GamePack.hpp"

#include "okay/Core/Inflate.hpp"
#include "okay/Core/Json.hpp"
#include "okay/Core/PathSafety.hpp"
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

namespace okay::browser {

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[8] = {'O', 'K', 'W', 'P', 'A', 'C', 'K', '1'};
constexpr std::size_t kHeader = 12;   // magic + u32 manifest length

std::string Mb(std::size_t n) { return std::to_string((n + (1u << 20) - 1) >> 20) + " MB"; }

} // namespace

bool ReadGamePack(const std::uint8_t* data, std::size_t size, GamePack& out, std::string& why, const GamePackLimits& limits) {
    out = GamePack();
    why.clear();
    if (!data || size == 0) { why = "The game file is empty."; return false; }
    if (size > limits.maxPackedBytes) { why = "The game is larger than " + Mb(limits.maxPackedBytes) + "."; return false; }
    if (size >= 2 && data[0] == 0x1f && data[1] == 0x8b) {
        inflate::Options op;
        op.maxOutput = limits.maxUnpackedBytes;
        std::string zwhy;
        if (!inflate::Gzip(data, size, out.bytes, op, zwhy)) {
            out.bytes.clear();
            why = "The game file is damaged (" + zwhy + "). Reload the page and try again.";
            return false;
        }
    } else {
        if (size > limits.maxUnpackedBytes) { why = "The game is larger than " + Mb(limits.maxUnpackedBytes) + "."; return false; }
        out.bytes.assign(data, data + size);
    }
    const std::vector<std::uint8_t>& b = out.bytes;
    auto fail = [&](const std::string& w) { out = GamePack(); why = w; return false; };
    if (b.size() < kHeader || std::memcmp(b.data(), kMagic, sizeof kMagic) != 0)
        return fail("This is not an OkaySpace game pack.");
    const std::size_t mlen = (std::size_t)b[8] | ((std::size_t)b[9] << 8) | ((std::size_t)b[10] << 16) | ((std::size_t)b[11] << 24);
    if (mlen > limits.maxManifestBytes) return fail("The game pack's file list is too large.");
    if (mlen > b.size() - kHeader) return fail("The game pack is cut off (its file list is incomplete).");
    const std::size_t base = kHeader + mlen;
    const std::size_t dataBytes = b.size() - base;

    json::Value manifest;
    std::string jwhy;
    json::Limits jl;
    jl.maxBytes = limits.maxManifestBytes;
    jl.maxDepth = 8;
    if (!json::Parse(std::string_view((const char*)b.data() + kHeader, mlen), manifest, &jwhy, jl))
        return fail("The game pack's file list cannot be read (" + jwhy + ").");
    const json::Value* files = manifest.Get("files");
    if (!files || !files->IsArray()) return fail("The game pack has no file list.");
    if (files->array.size() > limits.maxFiles) return fail("The game pack has too many files.");

    std::set<std::string> seen;   // lower-cased, so "A.png" and "a.png" cannot both be written on Windows
    std::vector<GamePackFile> list;
    list.reserve(files->array.size());
    for (const json::Value& e : files->array) {
        const json::Value* p = e.Get("path");
        const json::Value* o = e.Get("offset");
        const json::Value* s = e.Get("size");
        std::uint64_t off = 0, sz = 0;
        if (!p || !p->IsString() || !o || !s || !o->AsIndex(off) || !s->AsIndex(sz))
            return fail("The game pack's file list has a broken entry.");
        const std::string& path = p->string;
        if (path.size() < 2 || path[0] != '/')
            return fail("The game pack names a file outside the game (" + path.substr(0, 80) + ").");
        const std::string rel = path.substr(1);
        const pathsafety::Why w = pathsafety::Check(rel, pathsafety::Mode::Portable);
        if (w != pathsafety::Why::Ok)
            return fail("The game pack has a file name that is not allowed (" + rel.substr(0, 80) + ": " + pathsafety::Describe(w) + ").");
        if (rel.back() == '/') return fail("The game pack has a file name that is not allowed (" + rel.substr(0, 80) + ").");
        if (off > dataBytes || sz > dataBytes - off) return fail("The game pack is cut off (" + rel.substr(0, 80) + " is incomplete).");
        if (!seen.insert(str::LowerAscii(rel)).second) return fail("The game pack has the same file twice (" + rel.substr(0, 80) + ").");
        GamePackFile f;
        f.path = rel;
        f.offset = base + (std::size_t)off;
        f.size = (std::size_t)sz;
        list.push_back(std::move(f));
    }
    // A file may not also be a folder of another one ("a" and "a/b"): the second could not be written.
    for (const GamePackFile& f : list)
        for (std::size_t slash = f.path.find('/'); slash != std::string::npos; slash = f.path.find('/', slash + 1))
            if (seen.count(str::LowerAscii(f.path.substr(0, slash)))) return fail("The game pack has a file and a folder with the same name (" + f.path.substr(0, slash) + ").");
    out.files = std::move(list);
    return true;
}

bool UnpackGamePack(const GamePack& pack, const std::string& folder, std::string& why, bool replace) {
    why.clear();
    std::error_code ec;
    const fs::path target = fs::u8path(folder);
    if (target.empty() || !target.has_filename()) { why = "No folder to unpack the game into."; return false; }
    const fs::path part = fs::u8path(folder + ".part");
    fs::remove_all(part, ec);
    ec.clear();
    if (!fs::create_directories(part, ec) || ec) { why = "Could not make the game's folder (" + ec.message() + ")."; return false; }
    auto fail = [&](const std::string& w) { std::error_code e2; fs::remove_all(part, e2); why = w; return false; };
    for (const GamePackFile& f : pack.files) {
        if (f.offset > pack.bytes.size() || f.size > pack.bytes.size() - f.offset) return fail("The game pack is damaged.");
        fs::path dest;
        std::string pwhy;
        // The name rules ran in ReadGamePack; this also catches a link inside the folder that points out of it.
        if (!pathsafety::StaysInside(part, f.path, &dest, &pwhy)) return fail("The game pack has a file name that is not allowed (" + f.path.substr(0, 80) + ": " + pwhy + ").");
        fs::create_directories(dest.parent_path(), ec);
        if (ec) return fail("Could not write the game's files (" + ec.message() + ").");
        std::ofstream o(dest, std::ios::binary | std::ios::trunc);
        if (f.size) o.write((const char*)pack.bytes.data() + f.offset, (std::streamsize)f.size);
        o.close();
        if (!o) return fail("Could not write the game's files (is the disk full?).");
    }
    if (fs::exists(target, ec)) {
        if (!replace) return fail("The game's folder is already there.");
        fs::remove_all(target, ec);
        if (ec) return fail("Could not replace the game's old files (" + ec.message() + ").");
    }
    fs::rename(part, target, ec);
    if (ec) return fail("Could not finish unpacking the game (" + ec.message() + ").");
    return true;
}

} // namespace okay::browser
