// Our own RAW reader: the plain "raw triangle" text format (one triangle per line, nine numbers: x y z of the three corners).
// Written ONLY from the public description (Paul Bourke's "POV-Ray RAW triangle format" page: nine numbers per triangle,
// the twelve-number variant that starts with a red green blue colour, the nine-number variant that ends with a texture name,
// and non-numeric identifiers that separate groups of triangles). No importer source of any other project was read. The
// binary "raw" files that some tools write are not read (no description of one was found). Triangles with equal corner
// positions are welded, smooth normals are made, one part per group. Capped, no recursion.
// See docs/own_raw_reader.md.
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace raw {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxTris = 500000, kMaxGroups = 100000, kMaxOutVerts = 4000000, kMaxLineTokens = 64;

struct Key { std::uint32_t a, b, c; bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c; } };
struct KeyHash { std::size_t operator()(const Key& k) const { std::uint64_t h = k.a * 0x9E3779B97F4A7C15ull; h ^= (k.b + 0x7F4A7C15ull + (h << 6) + (h >> 2)); h ^= (k.c + 0x9E3779B9ull + (h << 6) + (h >> 2)); return (std::size_t)h; } };

struct Group {
    std::string name;
    detail::rc::Soup soup;
    std::unordered_map<Key, std::uint32_t, KeyHash> weld;
};

std::uint32_t Bits(float f) { if (f == 0.0f) f = 0.0f; std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

bool Fail(std::string* err, const std::string& m) { if (err) *err = "RAW: " + m; return false; }

} // namespace raw
} // namespace

bool ReadRawBuffer(const std::string& input, ImportedModel& out, std::string* err) {
    using namespace raw;
    out = ImportedModel{};
    // a copy only when the text is UTF-16 or has a UTF-8 mark (the rest of the text is read in place)
    const bool needFix = input.size() >= 2 && (((unsigned char)input[0] == 0xFF && (unsigned char)input[1] == 0xFE) || ((unsigned char)input[0] == 0xFE && (unsigned char)input[1] == 0xFF) || ((unsigned char)input[0] == 0xEF && input.size() >= 3 && (unsigned char)input[1] == 0xBB && (unsigned char)input[2] == 0xBF));
    std::string fixed;
    if (needFix) fixed = input;
    bool converted = false;
    std::string why;
    if (needFix && !detail::txt::NormalizeEncoding(fixed, converted, why, kMaxFileBytes)) return Fail(err, why);
    const std::string& text = needFix ? fixed : input;
    for (char ch : text) if (ch == '\0') return Fail(err, "the file holds a NUL byte: it is not a text triangle file (binary raw files are not read)");

    std::vector<Group> groups;
    std::unordered_map<std::string, std::size_t> byName;
    std::size_t cur = (std::size_t)-1, tris = 0;
    int colourLines = 0;
    auto groupFor = [&](const std::string& name) -> bool {
        auto it = byName.find(name);
        if (it == byName.end()) {
            if (groups.size() >= kMaxGroups) return false;
            it = byName.emplace(name, groups.size()).first;
            groups.emplace_back();
            groups.back().name = name;
        }
        cur = it->second;
        return true;
    };

    detail::txt::Lines lines(text);
    std::string line;
    while (lines.Next(line)) {
        const std::vector<std::string> tok = detail::txt::Split(line);
        if (tok.empty() || tok[0][0] == '#') continue;
        double probe;
        if (!detail::txt::ToDouble(tok[0], probe)) {   // a group name: the whole line
            std::size_t b = 0, e = line.size();
            while (b < e && detail::txt::IsWs(line[b])) ++b;
            while (e > b && detail::txt::IsWs(line[e - 1])) --e;
            if (e - b > 256) e = b + 256;
            if (!groupFor(line.substr(b, e - b))) return Fail(err, "more than " + std::to_string(kMaxGroups) + " groups");
            continue;
        }
        if (tok.size() > kMaxLineTokens) return Fail(err, "line " + std::to_string(lines.number) + " has too many values");
        double v[12];
        std::size_t k = 0;
        while (k < tok.size() && k < 12 && detail::txt::ToDouble(tok[k], v[k])) ++k;
        std::string tex;
        std::size_t first = 0;
        if (k == 9 && tok.size() == 9) first = 0;
        else if (k == 9 && tok.size() == 10) { tex = tok[9]; first = 0; }
        else if (k == 12 && tok.size() == 12) { first = 3; ++colourLines; }
        else return Fail(err, "line " + std::to_string(lines.number) + " is not a triangle: nine numbers (optionally a colour before or a texture name after) are needed, " + std::to_string(k) + " found");
        if (++tris > kMaxTris) return Fail(err, "over the budget of " + std::to_string(kMaxTris) + " triangles for one file");
        if (!tex.empty()) { if (!groupFor(tex)) return Fail(err, "more than " + std::to_string(kMaxGroups) + " groups"); }
        else if (cur == (std::size_t)-1 && !groupFor("RAW")) return Fail(err, "no group");
        Group& g = groups[cur];
        for (int c = 0; c < 3; ++c) {
            const Vec3 p{(float)v[first + (std::size_t)c * 3], (float)v[first + (std::size_t)c * 3 + 1], (float)v[first + (std::size_t)c * 3 + 2]};
            const Key key{Bits(p.x), Bits(p.y), Bits(p.z)};
            auto it = g.weld.find(key);
            if (it == g.weld.end()) { it = g.weld.emplace(key, (std::uint32_t)g.soup.pos.size()).first; g.soup.pos.push_back(p); }
            g.soup.ci.push_back(it->second);
        }
    }
    if (tris == 0) return Fail(err, "the file holds no triangles");

    ImportedNode root;
    root.name = "RAW";
    out.nodes.push_back(root);
    std::size_t outVerts = 0;
    int degenerate = 0;
    for (Group& g : groups) {
        if (g.soup.ci.empty()) continue;
        ImportedPart part;
        detail::rc::BuildStats st;
        if (!detail::rc::BuildMesh(g.soup, 60.0, part.mesh, &st, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) return Fail(err, "over the budget of " + std::to_string(kMaxOutVerts) + " vertices for one file");
        degenerate += st.degenerate;
        g.soup = detail::rc::Soup{};
        g.weld.clear();
        if (part.mesh.triangles.empty()) continue;
        outVerts += part.mesh.vertices.size();
        if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
        part.mesh.name = g.name;
        part.node = 0;
        out.parts.push_back(std::move(part));
    }
    if (out.parts.empty()) return Fail(err, "the file has no usable triangles (all are degenerate)");
    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("RAW: " + s); };
    if (colourLines) note(std::to_string(colourLines) + " triangle(s) carry a colour: the colour is not imported");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    note("one triangle per line is required; a texture name after nine numbers names the group and is not loaded as a texture");
    return true;
}

bool ReadRawFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, raw::kMaxFileBytes, bytes, err)) return false;
    return ReadRawBuffer(bytes, out, err);
}

} // namespace okay
