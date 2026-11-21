// Our own RAW reader: the plain "raw triangle" text format (one triangle per line, nine numbers: x y z of the three corners).
// Written ONLY from the public description (Paul Bourke's "POV-Ray RAW triangle format" page: nine numbers per triangle,
// the twelve-number variant that starts with a red green blue colour, the nine-number variant that ends with a texture name,
// and non-numeric identifiers that separate groups of triangles). No importer source of any other project was read. The
// binary "raw" files that some tools write are not read (no description of one was found). Triangles with equal corner
// positions are welded, smooth normals are made, one part per group. Capped, no recursion.
// See docs/content/model_readers.md.
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace raw {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxTris = 500000, kMaxGroups = 100000, kMaxOutVerts = 4000000, kMaxLineTokens = 64;


struct Group {
    std::string name;
    detail::rc::Soup soup;
    detail::WeldMap<3> weld;   // position bits (-0 as 0) -> soup position
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
        // first token, read as a number in one pass (ScanNumber: ParseNumber of the token)
        double v[12];
        const char* sp = text.data() + b;
        const char* const se = text.data() + e;
        if (!detail::rc::ScanNumber(sp, se, v[0])) {   // a group name: the whole line, trimmed
            std::size_t te = e;
            while (te > b && Ws(text[te - 1])) --te;
            if (te - b > 256) te = b + 256;
            if (!groupFor(text.substr(b, te - b))) return Fail(err, "more than " + std::to_string(kMaxGroups) + " groups");
            continue;
        }
        // the other tokens: the leading ones read as numbers (at most 12), in the same pass that counts them
        std::size_t nt = 1, k = 1;
        bool numbers = true;
        const char* tb9 = nullptr;
        const char* te9 = nullptr;
        for (;;) {
            while (sp < se && Ws(*sp)) ++sp;
            if (sp >= se) break;
            if (nt >= kMaxLineTokens) return Fail(err, "line " + std::to_string(lineNo) + " has more than " + std::to_string(kMaxLineTokens) + " values");
            const char* const t0 = sp;
            if (numbers && k < 12 && detail::rc::ScanNumber(sp, se, v[k])) ++k;
            else { numbers = false; while (sp < se && !Ws(*sp)) ++sp; }
            if (nt == 9) { tb9 = t0; te9 = sp; }
            ++nt;
        }
        std::string tex;
        std::size_t first = 0;
        if (k == 9 && nt == 9) first = 0;
        else if (k == 9 && nt == 10) { tex.assign(tb9, (std::size_t)(te9 - tb9)); if (tex.size() > 256) tex.resize(256); first = 0; }
        else if (k == 12 && nt == 12) { first = 3; ++colourLines; }
        else return Fail(err, "line " + std::to_string(lineNo) + " is not a triangle: nine numbers (optionally a colour before or a texture name after) are needed, " + std::to_string(k) + " found");
        if (++tris > kMaxTris) return Fail(err, "over the budget of " + std::to_string(kMaxTris) + " triangles for one file");
        if (!tex.empty()) { if (!groupFor(tex)) return Fail(err, "more than " + std::to_string(kMaxGroups) + " groups"); }
        else if (cur == (std::size_t)-1 && !groupFor("RAW")) return Fail(err, "no group");
        Group& g = groups[cur];
        for (int c = 0; c < 3; ++c) {
            const Vec3 p{(float)v[first + (std::size_t)c * 3], (float)v[first + (std::size_t)c * 3 + 1], (float)v[first + (std::size_t)c * 3 + 2]};
            const std::uint32_t key[3] = {Bits(p.x), Bits(p.y), Bits(p.z)};
            bool added = false;
            const int vi = g.weld.Get(key, (int)g.soup.pos.size(), added);
            if (added) g.soup.pos.push_back(p);
            g.soup.ci.push_back((std::uint32_t)vi);
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
        g.weld.clear();   // not needed to build the mesh: freed first, for a lower peak
        if (!detail::rc::BuildMesh(g.soup, 60.0, part.mesh, &st, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) return Fail(err, "over the budget of " + std::to_string(kMaxOutVerts) + " vertices for one file");
        degenerate += st.degenerate;
        g.soup = detail::rc::Soup{};
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

// A guard for the import seam, not part of reading. Assimp 5.4.3's RAW importer builds a broken node list when two or more
// groups hold triangles: the root says it has one child per such group, but only its first child pointer is ever set (each
// group's node is written over the one before) and the others stay null, so whoever walks the nodes crashes (our scene walk
// in ModelImportAssimp.cpp did, on the corpus files WithColor.raw and WithTexture.raw and on the own_dispatch_e2 fixture).
// Found by running it on small files and from the crash's backtrace. The rule that decides "a group" here follows what the
// importer does with lines (a line whose first non-blank character is not a digit, '+' or '-' starts or selects a group; the
// text ends at the first NUL; a line ends at CR, LF or form feed), and it errs on the side of "unsafe": a line that starts
// like a number counts as a triangle even when it would be refused, and a group name is only taken as "the same group again"
// when the whole rest of the line is exactly that name. Also called unsafe: a line of 4,000 characters or more (the importer
// copies a line into a fixed 4,096-byte buffer), UTF-16 text (converted before parsing; not modelled here) and more than 1,000
// group names (to keep this check linear). Our RAW reader reads all of these.
bool RawFileUnsafeForAssimp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;   // nothing to read: Assimp will say so itself
    std::vector<std::string> names{"<default>"};
    std::vector<char> filled{0};
    std::size_t cur = 0, filledCount = 0, lineLen = 0;
    enum class St { Start, Name, Skip } st = St::Start;
    std::string name;   // the whole rest of a name line (leading blanks skipped)
    bool bomChecked = false;
    char buf[65536];
    auto endName = [&]() -> bool {
        const std::size_t sp = name.find_first_of(" \t");
        const std::string token = name.substr(0, sp);
        std::size_t found = names.size();
        for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == name) { found = i; break; }
        if (found == names.size()) { names.push_back(token); filled.push_back(0); }
        cur = found;
        name.clear();
        return names.size() > 1000;   // many group names: say unsafe rather than compare them all (our reader reads such a file)
    };
    for (;;) {
        f.read(buf, sizeof buf);
        const std::size_t n = (std::size_t)f.gcount();
        if (n == 0) break;
        std::size_t i = 0;
        if (!bomChecked) {
            bomChecked = true;
            const unsigned char b0 = (unsigned char)buf[0], b1 = n > 1 ? (unsigned char)buf[1] : 0, b2 = n > 2 ? (unsigned char)buf[2] : 0;
            if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) return true;
            if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) i = 3;
        }
        for (; i < n; ++i) {
            const char c = buf[i];
            if (c == '\0') return (st == St::Name && endName()) || filledCount >= 2;
            const bool eol = c == '\r' || c == '\n' || c == '\f';
            if (eol) {
                if (st == St::Name && endName()) return true;
                st = St::Start; lineLen = 0;
                continue;
            }
            if (++lineLen >= 4000) return true;
            switch (st) {
            case St::Start:
                if (c == ' ' || c == '\t') break;
                if ((c >= '0' && c <= '9') || c == '+' || c == '-') {
                    if (!filled[cur]) { filled[cur] = 1; if (++filledCount >= 2) return true; }
                    st = St::Skip;
                } else { st = St::Name; name.assign(1, c); }
                break;
            case St::Name: name += c; break;
            default: break;
            }
        }
    }
    if (st == St::Name && endName()) return true;
    return filledCount >= 2;
}

} // namespace okay
