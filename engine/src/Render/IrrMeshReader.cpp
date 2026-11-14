// Our own Irrlicht mesh (.irrmesh, XML) reader: one part per <buffer>, with its material (diffuse and emissive colour,
// shininess, first texture, normal map). Written from the public description of the format (the Irrlicht engine's
// documentation and the files its tools write). Uses the strict XML reader of the batch-B readers; UTF-16 files are
// converted first. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/own_irrmesh_reader.md.
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "XmlLite.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace irr {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr long kMaxVerts = 4000000, kMaxIndices = 12000000;
constexpr std::size_t kMaxOutVerts = 20000000;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "IRRMESH: " + m; return false; }

// Next white-space separated token of [p, end); false at the end.
bool Tok(const char*& p, const char* end, const char*& b, const char*& e) {
    while (p < end && detail::rc::Ws(*p)) ++p;
    if (p >= end) return false;
    b = p;
    while (p < end && !detail::rc::Ws(*p)) ++p;
    e = p;
    return true;
}

// "aarrggbb" hex colour; false when it is not 1 to 8 hex digits.
bool HexColor(const std::string& s, std::uint32_t& v) {
    std::size_t a = 0, b = s.size();
    while (a < b && detail::rc::Ws(s[a])) ++a;
    while (b > a && detail::rc::Ws(s[b - 1])) --b;
    if (a >= b || b - a > 8) return false;
    std::uint32_t r = 0;
    for (std::size_t i = a; i < b; ++i) {
        const char c = s[i];
        const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        r = (r << 4) | (std::uint32_t)d;
    }
    v = r;
    return true;
}
Color ArgbToColor(std::uint32_t v) {
    return Color{(float)((v >> 16) & 255) / 255.0f, (float)((v >> 8) & 255) / 255.0f, (float)(v & 255) / 255.0f, (float)(v >> 24) / 255.0f};
}

std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; }

} // namespace irr
} // namespace

static bool ReadIrrMeshImpl(const std::string& input, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace irr;
    std::string text = input;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);

    detail::xml::Doc doc;
    std::string xerr;
    if (!detail::xml::Parse(text, doc, xerr)) return Fail(err, xerr);
    if (Lower(doc.Name(0)) != "mesh") return Fail(err, "not an Irrlicht mesh (the root element is <" + doc.Name(0) + ">, not <mesh>)");

    ImportedNode root;
    root.name = "IRRMesh";
    out.nodes.push_back(root);

    int buffers = 0, noVerts = 0, noIdx = 0, badIdx = 0, partialTris = 0, degenerate = 0, lightmaps = 0, secondUv = 0, tangents = 0, vertexColors = 0, extraTex = 0;
    std::size_t outVerts = 0;
    for (int bi : doc.Kids(0)) {
        if (Lower(doc.Name(bi)) != "buffer") continue;
        ++buffers;
        // ---- material ----
        ImportedMaterial mat;
        mat.name = "buffer" + std::to_string(buffers - 1);
        const int mi = doc.First(bi, "material");
        if (mi >= 0) {
            std::string type;
            std::string tex1, tex2;
            for (int k : doc.Kids(mi)) {   // <attributes> may wrap them: Irrlicht writes them as direct children
                const std::string kind = Lower(doc.Name(k));
                const std::string name = doc.AttrOr(k, "name"), value = doc.AttrOr(k, "value");
                if (kind == "color") {
                    std::uint32_t c;
                    if (!HexColor(value, c)) continue;
                    if (name == "Diffuse") { mat.hasDiffuse = true; mat.diffuse = ArgbToColor(c); }
                    else if (name == "Emissive") { const Color e = ArgbToColor(c); if (e.r + e.g + e.b > 0.0f) { mat.hasEmissive = true; mat.emissive = e; } }
                } else if (kind == "float") {
                    double v;
                    if (name == "Shininess" && detail::rc::ParseNumber(value, v) && v >= 0) mat.shininess = (float)v;
                } else if (kind == "enum") {
                    if (name == "Type") type = Lower(value);
                } else if (kind == "texture") {
                    if (name == "Texture1") tex1 = value;
                    else if (name == "Texture2") tex2 = value;
                    else if ((name == "Texture3" || name == "Texture4") && !value.empty()) ++extraTex;
                }
            }
            auto fix = [&](std::string t) {
                for (char& c : t) if (c == '\\') c = '/';
                if (t.compare(0, 2, "./") == 0) t.erase(0, 2);
                if (!t.empty() && !baseDir.empty() && t[0] != '/' && t.find(':') == std::string::npos) t = baseDir + t;
                return t;
            };
            mat.texture = fix(tex1);
            if (type.compare(0, 9, "normalmap") == 0 || type.compare(0, 11, "parallaxmap") == 0) mat.normalMap = fix(tex2);
            else if (!tex2.empty()) ++lightmaps;   // a lightmap or a second layer
        }
        // ---- vertices ----
        const int vi = doc.First(bi, "vertices");
        if (vi < 0) { ++noVerts; continue; }
        const std::string vtype = Lower(doc.AttrOr(vi, "type", "standard"));
        std::size_t stride;
        if (vtype == "standard") stride = 9;
        else if (vtype == "2tcoords") stride = 11;
        else if (vtype == "tangents") stride = 15;
        else return Fail(err, "unknown vertex type '" + vtype + "'");
        double vcount;
        if (!detail::rc::ParseNumber(doc.AttrOr(vi, "vertexCount", "-1"), vcount) || vcount < 0 || vcount > (double)kMaxVerts || vcount != std::floor(vcount))
            return Fail(err, "bad vertexCount '" + doc.AttrOr(vi, "vertexCount", "") + "'");
        const std::size_t nv = (std::size_t)vcount;
        if (nv == 0) { ++noVerts; continue; }
        const std::string& vt = doc.Text(vi);
        // every token is a number except the colour, which is 8 hex digits: check there are enough before allocating
        detail::rc::Soup soup;
        std::vector<Vec3> nrm;
        std::vector<Vec2> uv;
        const char* p = vt.data();
        const char* end = p + vt.size();
        soup.pos.reserve(std::min<std::size_t>(nv, vt.size() / 8 + 1));
        for (std::size_t i = 0; i < nv; ++i) {
            double f[15];
            std::uint32_t col = 0xFFFFFFFFu;
            const char *b, *e;
            for (std::size_t k = 0; k < stride; ++k) {
                if (!Tok(p, end, b, e)) return Fail(err, "the <vertices> text ends after " + std::to_string(i) + " of " + std::to_string(nv) + " vertices");
                if (k == 6) {
                    if (!HexColor(std::string(b, e), col)) return Fail(err, "vertex " + std::to_string(i) + " has a bad colour");
                    f[k] = 0;
                } else if (!detail::rc::ParseNumber(b, e, f[k])) return Fail(err, "vertex " + std::to_string(i) + " has a bad number");
            }
            if (col != 0xFFFFFFFFu) ++vertexColors;
            soup.pos.push_back(Vec3{(float)f[0], (float)f[1], (float)f[2]});
            nrm.push_back(Vec3{(float)f[3], (float)f[4], (float)f[5]});
            uv.push_back(Vec2{(float)f[7], (float)f[8]});   // as written; Irrlicht's v runs down the picture like the file's own rows
        }
        if (stride == 11) ++secondUv;
        if (stride == 15) ++tangents;
        // ---- indices ----
        const int ii = doc.First(bi, "indices");
        if (ii < 0) { ++noIdx; continue; }
        double icount;
        if (!detail::rc::ParseNumber(doc.AttrOr(ii, "indexCount", "-1"), icount) || icount < 0 || icount > (double)kMaxIndices || icount != std::floor(icount))
            return Fail(err, "bad indexCount '" + doc.AttrOr(ii, "indexCount", "") + "'");
        const std::size_t ni = (std::size_t)icount;
        if (ni == 0) { ++noIdx; continue; }
        const std::string& it = doc.Text(ii);
        std::vector<std::uint32_t> idx;
        idx.reserve(std::min<std::size_t>(ni, it.size() / 2 + 1));
        const char* q = it.data();
        const char* qend = q + it.size();
        for (std::size_t i = 0; i < ni; ++i) {
            const char *b, *e;
            if (!Tok(q, qend, b, e)) return Fail(err, "the <indices> text ends after " + std::to_string(i) + " of " + std::to_string(ni) + " indices");
            std::uint64_t v = 0;
            if (e - b > 10) return Fail(err, "an index is not a number");
            for (const char* c = b; c < e; ++c) { if (*c < '0' || *c > '9') return Fail(err, "an index is not a number"); v = v * 10 + (std::uint64_t)(*c - '0'); }
            idx.push_back(v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (std::uint32_t)v);
        }
        if (ni % 3) ++partialTris;
        soup.ci.reserve(ni);
        soup.cuv.reserve(ni);
        soup.cnrm.reserve(ni);
        for (std::size_t t = 0; t + 2 < ni; t += 3) {
            if (idx[t] >= nv || idx[t + 1] >= nv || idx[t + 2] >= nv) { ++badIdx; continue; }
            for (std::size_t k = 0; k < 3; ++k) { soup.ci.push_back(idx[t + k]); soup.cuv.push_back(uv[idx[t + k]]); soup.cnrm.push_back(nrm[idx[t + k]]); }
        }
        ImportedPart part;
        detail::rc::BuildStats st;
        if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &st, kMaxOutVerts - outVerts)) return Fail(err, "too many output vertices");
        degenerate += st.degenerate;
        outVerts += part.mesh.vertices.size();
        if (part.mesh.triangles.empty()) { ++noIdx; continue; }
        if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
        part.mesh.name = mat.name;
        part.node = 0;
        part.material = (int)out.materials.size();
        out.materials.push_back(std::move(mat));
        out.parts.push_back(std::move(part));
    }
    if (buffers == 0) return Fail(err, "the file has no <buffer>");
    if (out.parts.empty()) return Fail(err, "no buffer has both vertices and triangles");
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("IRRMESH: " + s); };
    note("Irrlicht is left-handed: coordinates and winding are kept as written (like Assimp), so the model is the mirror image of what a right-handed viewer expects; normals and winding stay consistent");
    if (lightmaps) note(std::to_string(lightmaps) + " buffer(s) with a second texture (a lightmap or a detail layer) that is not imported");
    if (extraTex) note(std::to_string(extraTex) + " third or fourth texture name(s) not imported");
    if (secondUv) note(std::to_string(secondUv) + " buffer(s) with a second set of texture coordinates (type 2tcoords): the second set is not imported");
    if (tangents) note(std::to_string(tangents) + " buffer(s) with stored tangents and bitangents (type tangents): not imported");
    if (vertexColors) note(std::to_string(vertexColors) + " vertex colour(s) that are not white: vertex colours are not imported");
    if (noVerts) note(std::to_string(noVerts) + " buffer(s) without vertices skipped");
    if (noIdx) note(std::to_string(noIdx) + " buffer(s) without indices or triangles skipped");
    if (partialTris) note(std::to_string(partialTris) + " buffer(s) whose index count is not a multiple of 3: the leftover indices were dropped");
    if (badIdx) note(std::to_string(badIdx) + " triangle(s) with an out-of-range index skipped");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    note("material types, blending, wrap modes, flags and the bounding boxes are not read; texture paths are relative to the model's folder");
    return true;
}

bool ReadIrrMeshBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadIrrMeshImpl(text, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadIrrMeshFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, irr::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadIrrMeshBuffer(bytes, dir, out, err);
}

} // namespace okay
