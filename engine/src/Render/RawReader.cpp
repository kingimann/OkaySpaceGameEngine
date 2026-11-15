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

    // Lines are read in place (no copy of a line, no token strings): a 256 MiB line of one-letter words must not become
    // 128 million strings. At most kMaxLineTokens tokens of a line are looked at; one more refuses the file.
    using detail::rc::Ws;
    std::size_t pos = 0, lineNo = 0;
    while (pos < text.size()) {
        std::size_t e = pos;
        while (e < text.size() && text[e] != '\n' && text[e] != '\r') ++e;
        const std::size_t b0 = pos;
        pos = e;
        if (pos < text.size() && text[pos] == '\r') { ++pos; if (pos < text.size() && text[pos] == '\n') ++pos; }
        else if (pos < text.size()) ++pos;
        ++lineNo;
        std::size_t b = b0;
        while (b < e && Ws(text[b])) ++b;
        if (b >= e || text[b] == '#') continue;
        // first token
        std::size_t q = b;
        while (q < e && !Ws(text[q])) ++q;
        double probe;
        if (!detail::rc::ParseNumber(text.data() + b, text.data() + q, probe)) {   // a group name: the whole line, trimmed
            std::size_t te = e;
            while (te > b && Ws(text[te - 1])) --te;
            if (te - b > 256) te = b + 256;
            if (!groupFor(text.substr(b, te - b))) return Fail(err, "more than " + std::to_string(kMaxGroups) + " groups");
            continue;
        }
        // tokens of a number line: pointers into the text
        const char* tb[kMaxLineTokens + 1];
        const char* te_[kMaxLineTokens + 1];
        std::size_t nt = 0;
        for (std::size_t t = b; t < e;) {
            while (t < e && Ws(text[t])) ++t;
            if (t >= e) break;
            std::size_t u = t;
            while (u < e && !Ws(text[u])) ++u;
            if (nt >= kMaxLineTokens) return Fail(err, "line " + std::to_string(lineNo) + " has more than " + std::to_string(kMaxLineTokens) + " values");
            tb[nt] = text.data() + t; te_[nt] = text.data() + u; ++nt;
            t = u;
        }
        double v[12];
        std::size_t k = 0;
        while (k < nt && k < 12 && detail::rc::ParseNumber(tb[k], te_[k], v[k])) ++k;
        std::string tex;
        std::size_t first = 0;
        if (k == 9 && nt == 9) first = 0;
        else if (k == 9 && nt == 10) { tex.assign(tb[9], (std::size_t)(te_[9] - tb[9])); if (tex.size() > 256) tex.resize(256); first = 0; }
        else if (k == 12 && nt == 12) { first = 3; ++colourLines; }
        else return Fail(err, "line " + std::to_string(lineNo) + " is not a triangle: nine numbers (optionally a colour before or a texture name after) are needed, " + std::to_string(k) + " found");
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
