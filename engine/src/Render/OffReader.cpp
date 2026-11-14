// Our own OFF (Object File Format, text mesh) reader. Written from the public description of the format (Geomview's
// "off" manual page). Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/own_off_reader.md.
#include "PolygonTriangulate.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace off {

using namespace detail::txt;

constexpr std::size_t kMaxFileBytes   = 512u * 1024u * 1024u;
constexpr std::size_t kMaxVertices    = 10000000;
constexpr std::size_t kMaxFaces       = 10000000;
constexpr std::size_t kMaxTriangles   = 30000000;
constexpr std::size_t kMaxFaceCorners = 65536;
constexpr double      kMaxCoord       = 1e15;

struct Reader {
    std::string err;
    bool Fail(const std::string& m) { if (err.empty()) err = "OFF: " + m; return false; }

    bool Run(std::string text, ImportedModel& out) {
        bool wasUtf16 = false;
        std::string why;
        if (!NormalizeEncoding(text, wasUtf16, why, kMaxFileBytes)) return Fail(why);
        Lines lines(text);
        std::string line;
        auto nextData = [&](std::vector<std::string>& tk) -> bool {   // next non-blank line, '#' comments removed
            while (lines.Next(line)) {
                const std::size_t h = line.find('#');
                if (h != std::string::npos) line.resize(h);
                tk = Split(line);
                if (!tk.empty()) return true;
            }
            return false;
        };

        std::vector<std::string> tk;
        if (!nextData(tk)) return Fail("empty file");
        // Header: [ST][C][N][4][n]OFF, then (maybe on the same line) the counts.
        std::string kw = tk[0];
        const std::string up = [&] { std::string u = kw; for (char& c : u) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); return u; }();
        if (up.size() < 3 || up.compare(up.size() - 3, 3, "OFF") != 0) return Fail("not an OFF file (the first word must end in OFF)");
        std::string prefix = up.substr(0, up.size() - 3);
        bool hasST = false, hasC = false, hasN = false;
        std::size_t dim = 3;
        {
            std::size_t i = 0;
            if (prefix.compare(0, 2, "ST") == 0) { hasST = true; i = 2; }
            if (i < prefix.size() && prefix[i] == 'C') { hasC = true; ++i; }
            if (i < prefix.size() && prefix[i] == 'N') { hasN = true; ++i; }
            if (i < prefix.size() && prefix[i] == '4') { dim = 4; ++i; }
            if (i < prefix.size() && prefix[i] == 'N') { return Fail("nOFF (any number of dimensions) is not supported"); }
            if (i != prefix.size()) return Fail("unknown OFF header word '" + kw + "'");
        }
        if (dim == 4) return Fail("4OFF (homogeneous coordinates) is not supported");
        std::vector<std::string> counts(tk.begin() + 1, tk.end());
        if (counts.empty()) { if (!nextData(counts)) return Fail("missing the vertex and face counts"); }
        if (counts.size() < 2) return Fail("the counts line needs a vertex count and a face count");
        long long nv = 0, nf = 0;
        if (!ToInt(counts[0], nv) || !ToInt(counts[1], nf) || nv < 0 || nf < 0) return Fail("bad vertex or face count");
        if ((std::size_t)nv > kMaxVertices) return Fail("too many vertices");
        if ((std::size_t)nf > kMaxFaces) return Fail("too many faces");
        // Every vertex and face line is at least 6 bytes ("0 0 0\n", "3 0 1 2"), so the counts cannot exceed what the text can hold.
        const std::size_t left = text.size() - std::min(text.size(), lines.pos);
        if ((std::size_t)nv * 5 > left || (std::size_t)nf * 5 > left) return Fail("the counts are larger than the file");

        Mesh mesh;
        mesh.name = "OFF";
        mesh.vertices.reserve((std::size_t)nv);
        if (hasN) mesh.normals.reserve((std::size_t)nv);
        if (hasST) mesh.uvs.reserve((std::size_t)nv);
        std::size_t extraValues = 0, colorVerts = 0;
        for (long long i = 0; i < nv; ++i) {
            if (!nextData(tk)) return Fail("the file ends inside the vertex list (" + std::to_string(i) + " of " + std::to_string(nv) + " vertices)");
            const std::size_t want = 3 + (hasN ? 3u : 0u) + (hasST ? 2u : 0u);
            if (tk.size() < want) return Fail("vertex " + std::to_string(i) + " has too few values");
            float v[8];
            const std::size_t use = std::min<std::size_t>(tk.size(), 3 + (hasN ? 3u : 0u));
            for (std::size_t k = 0; k < use; ++k) if (!ToFloat(tk[k], v[k])) return Fail("bad number in vertex " + std::to_string(i));
            mesh.vertices.push_back(Vec3{v[0], v[1], v[2]});
            std::size_t at = 3;
            if (hasN) { mesh.normals.push_back(Vec3{v[3], v[4], v[5]}); at = 6; }
            // Colour (3 or 4 values) sits between the normal and the texture coordinates; its width is what is left over.
            std::size_t rest = tk.size() - at - (hasST ? 2u : 0u);
            if (hasC) { colorVerts++; at += rest >= 4 ? 4 : rest; rest = tk.size() - at - (hasST ? 2u : 0u); }
            if (hasST) {
                float s, t;
                if (!ToFloat(tk[at], s) || !ToFloat(tk[at + 1], t)) return Fail("bad texture coordinate in vertex " + std::to_string(i));
                mesh.uvs.push_back(Vec2{s, t});
                at += 2;
            }
            if (at < tk.size()) ++extraValues;
        }

        std::size_t badFaces = 0, shortFaces = 0, faceColors = 0, degenerate = 0;
        std::vector<Vec3> ring;
        std::vector<std::array<int, 3>> tris;
        std::vector<int> idx;
        for (long long f = 0; f < nf; ++f) {
            if (!nextData(tk)) return Fail("the file ends inside the face list (" + std::to_string(f) + " of " + std::to_string(nf) + " faces)");
            long long n = 0;
            if (!ToInt(tk[0], n) || n < 0) return Fail("bad corner count in face " + std::to_string(f));
            if ((std::size_t)n > kMaxFaceCorners) return Fail("a face has too many corners");
            if (tk.size() < 1 + (std::size_t)n) return Fail("face " + std::to_string(f) + " has fewer indices than its corner count");
            if (tk.size() > 1 + (std::size_t)n) ++faceColors;   // r g b [a] after the indices
            idx.clear();
            bool bad = false;
            for (long long k = 0; k < n; ++k) {
                long long vi = 0;
                if (!ToInt(tk[1 + (std::size_t)k], vi)) return Fail("bad index in face " + std::to_string(f));
                if (vi < 0 || vi >= nv) bad = true;
                idx.push_back((int)vi);
            }
            if (n < 3) { ++shortFaces; continue; }
            if (bad) { ++badFaces; continue; }
            ring.clear();
            for (int vi : idx) ring.push_back(mesh.vertices[(std::size_t)vi]);
            detail::TriangulatePolygon(ring, tris);
            for (const auto& t : tris) {
                const int a = idx[(std::size_t)t[0]], b = idx[(std::size_t)t[1]], c = idx[(std::size_t)t[2]];
                if (a == b || b == c || a == c) { ++degenerate; continue; }
                if (mesh.triangles.size() / 3 >= kMaxTriangles) return Fail("too many triangles");
                mesh.triangles.push_back(a); mesh.triangles.push_back(b); mesh.triangles.push_back(c);
            }
        }
        if (mesh.triangles.empty()) return Fail("the file has no faces");
        if (!hasN) mesh.normals.clear();
        else {
            for (Vec3& n : mesh.normals) {
                const float m = n.Magnitude();
                if (m > 1e-12f && std::isfinite(m)) n = n * (1.0f / m); else { mesh.normals.clear(); break; }
            }
        }
        if (mesh.normals.empty()) mesh.ComputeSmoothNormals();
        for (const Vec3& v : mesh.vertices) if (std::fabs(v.x) > kMaxCoord || std::fabs(v.y) > kMaxCoord || std::fabs(v.z) > kMaxCoord) return Fail("coordinates out of range");

        ImportedNode root;
        root.name = "OFF";
        out.nodes.push_back(root);
        ImportedPart part;
        part.mesh = std::move(mesh);
        part.node = 0;
        out.parts.push_back(std::move(part));
        out.backend = "own";
        auto note = [&](const std::string& s) { out.notes.push_back("OFF: " + s); };
        if (wasUtf16) note("the file is UTF-16 text and was converted");
        if (hasC && colorVerts) note("per-vertex colors not imported");
        if (faceColors) note(std::to_string(faceColors) + " face(s) carry colors: not imported");
        if (extraValues) note(std::to_string(extraValues) + " vertex line(s) with more values than the header announces: the extra values were ignored");
        if (shortFaces) note(std::to_string(shortFaces) + " face(s) with fewer than 3 corners skipped");
        if (badFaces) note(std::to_string(badFaces) + " face(s) with an out-of-range vertex index skipped");
        if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        if (hasST) note("texture coordinates are kept as written (no v flip)");
        return true;
    }
};

} // namespace off
} // namespace

bool ReadOffBuffer(const std::string& text, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    off::Reader r;
    if (!r.Run(text, out)) {
        const std::string m = r.err.empty() ? "OFF: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    return true;
}

bool ReadOffFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, off::kMaxFileBytes, bytes, err)) return false;
    return ReadOffBuffer(bytes, out, err);
}

} // namespace okay
