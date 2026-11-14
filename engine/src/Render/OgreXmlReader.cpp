// Our own OGRE XML mesh (.mesh.xml) reader. Written from the public description of the OGRE mesh XML format (the file the
// OgreXMLConverter tool and the exporters write: <mesh> with <sharedgeometry>, <submeshes>, <vertexbuffer> blocks of
// <vertex> elements, <faces>) and of the OGRE material script language (the .material files that name the colours and the
// texture of a submesh's material); no code from any other importer. One part per submesh. Uses the strict XML reader of the
// batch-B readers. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/own_ogre_reader.md.
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "XmlLite.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace ogre {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr long kMaxVerts = 8000000, kMaxFaces = 20000000;
constexpr std::size_t kMaxOutVerts = 40000000, kMaxSubmeshes = 100000;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "OGRE: " + m; return false; }

struct Geom {
    std::vector<Vec3> pos, nrm;
    std::vector<Vec2> uv;
    std::vector<char> hasPos, hasNrm, hasUv;
    long count = 0;
};

bool Dbl(const std::string* s, double& v) { return s && detail::rc::ParseNumber(*s, v); }

// One <geometry> / <sharedgeometry> element: the vertexcount attribute and the <vertexbuffer> children (position, normal and
// the first texcoord set may be spread over several buffers; vertex k of every buffer is vertex k of the geometry).
bool ReadGeometry(const detail::xml::Doc& d, int g, Geom& out, std::string* err, int& extraUv, int& colours, int& tangentsSeen) {
    double vc;
    if (!Dbl(d.Attr(g, "vertexcount"), vc) || vc < 0 || vc > (double)kMaxVerts || vc != std::floor(vc))
        return Fail(err, "bad vertexcount '" + d.AttrOr(g, "vertexcount", "") + "'");
    out.count = (long)vc;
    const std::size_t n = (std::size_t)vc;
    {   // the claimed count must be backed by <vertex> elements before anything is allocated from it
        std::size_t most = 0;
        for (int b : d.Kids(g)) {
            if (d.Name(b) != "vertexbuffer") continue;
            std::size_t k = 0;
            for (int vi : d.Kids(b)) if (d.Name(vi) == "vertex") ++k;
            most = std::max(most, k);
        }
        if (most != n) return Fail(err, "the vertexcount " + std::to_string(n) + " does not match the " + std::to_string(most) + " <vertex> elements");
    }
    out.pos.assign(n, Vec3{0, 0, 0}); out.nrm.assign(n, Vec3{0, 0, 0}); out.uv.assign(n, Vec2{0, 0});
    out.hasPos.assign(n, 0); out.hasNrm.assign(n, 0); out.hasUv.assign(n, 0);
    for (int b : d.Kids(g)) {
        if (d.Name(b) != "vertexbuffer") continue;
        std::size_t k = 0;
        for (int vi : d.Kids(b)) {
            if (d.Name(vi) != "vertex") continue;
            if (k >= n) return Fail(err, "a vertexbuffer has more <vertex> elements than the vertexcount " + std::to_string(n));
            bool uvDone = false;
            for (int e : d.Kids(vi)) {
                const std::string& nm = d.Name(e);
                double a, bb, c;
                if (nm == "position") {
                    if (!Dbl(d.Attr(e, "x"), a) || !Dbl(d.Attr(e, "y"), bb) || !Dbl(d.Attr(e, "z"), c)) return Fail(err, "bad <position> in vertex " + std::to_string(k));
                    out.pos[k] = Vec3{(float)a, (float)bb, (float)c}; out.hasPos[k] = 1;
                } else if (nm == "normal") {
                    if (!Dbl(d.Attr(e, "x"), a) || !Dbl(d.Attr(e, "y"), bb) || !Dbl(d.Attr(e, "z"), c)) return Fail(err, "bad <normal> in vertex " + std::to_string(k));
                    out.nrm[k] = Vec3{(float)a, (float)bb, (float)c}; out.hasNrm[k] = 1;
                } else if (nm == "texcoord") {
                    if (uvDone) { ++extraUv; continue; }
                    if (!Dbl(d.Attr(e, "u"), a) || !Dbl(d.Attr(e, "v"), bb)) return Fail(err, "bad <texcoord> in vertex " + std::to_string(k));
                    out.uv[k] = Vec2{(float)a, (float)bb}; out.hasUv[k] = 1; uvDone = true;
                } else if (nm == "colour_diffuse" || nm == "colour_specular") ++colours;
                else if (nm == "tangent" || nm == "binormal") ++tangentsSeen;
            }
            ++k;
        }
    }
    return true;
}

// ---- material scripts ----
struct Mat { std::string name; bool hasDiffuse = false; Color diffuse{1, 1, 1, 1}; bool hasEmissive = false; Color emissive{0, 0, 0, 1}; float shininess = -1; std::string texture; };

// Words and braces of a material script: "//" comments dropped, quoted strings kept whole.
void Words(const std::string& s, std::vector<std::string>& w) {
    std::size_t i = 0;
    w.clear();
    while (i < s.size() && w.size() < 2000000) {
        const char c = s[i];
        if (detail::txt::IsWs(c)) { ++i; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n' && s[i] != '\r') ++i; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') { const std::size_t e = s.find("*/", i + 2); i = e == std::string::npos ? s.size() : e + 2; continue; }
        if (c == '{' || c == '}' || c == ':') { w.push_back(std::string(1, c)); ++i; continue; }
        if (c == '"') { std::size_t e = s.find('"', i + 1); if (e == std::string::npos) e = s.size(); w.push_back(s.substr(i + 1, e - i - 1)); i = e + 1; continue; }
        std::size_t e = i;
        while (e < s.size() && !detail::txt::IsWs(s[e]) && s[e] != '{' && s[e] != '}') ++e;
        w.push_back(s.substr(i, e - i));
        i = e;
    }
}

void ParseScript(const std::string& script, std::map<std::string, Mat>& mats) {
    std::vector<std::string> w;
    Words(script, w);
    std::size_t i = 0;
    while (i < w.size()) {
        if (w[i] != "material") { ++i; continue; }
        ++i;
        if (i >= w.size()) break;
        Mat m;
        m.name = w[i++];
        while (i < w.size() && w[i] != "{") ++i;   // skips ": parent"
        if (i >= w.size()) break;
        ++i;
        int depth = 1;
        std::vector<std::string> kind;   // keyword that opened each open block
        std::string last;
        bool inFirstPass = true, seenPass = false, haveTex = false;
        kind.push_back("material");
        while (i < w.size() && depth > 0) {
            const std::string& t = w[i];
            if (t == "{") { ++depth; kind.push_back(last); last.clear(); ++i; continue; }
            if (t == "}") {
                --depth;
                if (!kind.empty()) { if (kind.back() == "pass") { if (seenPass) inFirstPass = false; } kind.pop_back(); }
                ++i; continue;
            }
            if (t == "pass") seenPass = true;
            if (t == "pass" || t == "technique" || t == "texture_unit") last = t;   // the block keyword; a name may follow it before the brace
            const std::string top = kind.empty() ? "" : kind.back();
            auto num = [&](std::size_t at, double& v) { return at < w.size() && detail::rc::ParseNumber(w[at], v); };
            if (top == "pass" && inFirstPass) {
                double a, b, c, d;
                if (t == "diffuse" && num(i + 1, a) && num(i + 2, b) && num(i + 3, c)) { m.hasDiffuse = true; m.diffuse = Color{(float)a, (float)b, (float)c, num(i + 4, d) ? (float)d : 1.0f}; }
                else if (t == "emissive" && num(i + 1, a) && num(i + 2, b) && num(i + 3, c)) { if (a + b + c > 0) { m.hasEmissive = true; m.emissive = Color{(float)a, (float)b, (float)c, 1}; } }
                else if (t == "specular" && num(i + 1, a) && num(i + 2, b) && num(i + 3, c) && num(i + 4, d)) { if (num(i + 5, a)) m.shininess = (float)std::max(0.0, a); }
            } else if (top == "texture_unit" && inFirstPass && !haveTex && t == "texture" && i + 1 < w.size()) { m.texture = w[i + 1]; haveTex = true; }
            ++i;
        }
        mats[m.name] = m;
    }
}

} // namespace ogre
} // namespace

static bool ReadOgreImpl(const std::string& input, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* scripts) {
    using namespace ogre;
    std::string text = input;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);
    detail::xml::Doc doc;
    std::string xerr;
    if (!detail::xml::Parse(text, doc, xerr)) return Fail(err, xerr);
    if (doc.Name(0) != "mesh") return Fail(err, "not an OGRE mesh (the root element is <" + doc.Name(0) + ">, not <mesh>)");

    std::map<std::string, Mat> mats;
    if (scripts) for (const std::string& s : *scripts) ParseScript(s, mats);

    int extraUv = 0, colours = 0, tangents = 0, boneAssign = 0, lines = 0, badFaces = 0, partialStrips = 0, noGeom = 0;
    Geom shared;
    bool haveShared = false;
    const int sg = doc.First(0, "sharedgeometry");
    if (sg >= 0) {
        if (!ReadGeometry(doc, sg, shared, err, extraUv, colours, tangents)) return false;
        haveShared = true;
    }
    const int sm = doc.First(0, "submeshes");
    if (sm < 0) return Fail(err, "the file has no <submeshes>");

    ImportedNode root;
    root.name = "OGRE";
    out.nodes.push_back(root);

    std::map<int, std::string> names;   // <submeshnames>: index -> name
    {
        const int sn = doc.First(0, "submeshnames");
        if (sn >= 0) for (int e : doc.Kids(sn)) {
            double ix;
            if (doc.Name(e) == "submeshname" && Dbl(doc.Attr(e, "index"), ix) && ix >= 0 && ix < 1e9 && names.size() < kMaxSubmeshes) names[(int)ix] = doc.AttrOr(e, "name", "");
        }
    }
    std::map<std::string, int> matOut;
    std::size_t outVerts = 0;
    int idx = 0, degenerate = 0, badIdx = 0;
    for (int s : doc.Kids(sm)) {
        if (doc.Name(s) != "submesh") continue;
        if ((std::size_t)idx >= kMaxSubmeshes) return Fail(err, "too many submeshes");
        const int myIdx = idx++;
        const std::string op = detail::txt::Lower(doc.AttrOr(s, "operationtype", "triangle_list"));
        if (op != "triangle_list" && op != "triangle_strip" && op != "triangle_fan") { ++lines; continue; }
        const std::string us = detail::txt::Lower(doc.AttrOr(s, "usesharedvertices", "false"));
        const bool useShared = us == "true" || us == "1";
        Geom own;
        const Geom* g = &own;
        if (useShared) {
            if (!haveShared) return Fail(err, "submesh " + std::to_string(myIdx) + " uses shared vertices but the file has no <sharedgeometry>");
            g = &shared;
        } else {
            const int gi = doc.First(s, "geometry");
            if (gi < 0) { ++noGeom; continue; }
            if (!ReadGeometry(doc, gi, own, err, extraUv, colours, tangents)) return false;
        }
        const int fi = doc.First(s, "faces");
        if (fi < 0) { ++noGeom; continue; }
        double fc;
        if (!Dbl(doc.Attr(fi, "count"), fc) || fc < 0 || fc > (double)kMaxFaces || fc != std::floor(fc)) return Fail(err, "bad faces count '" + doc.AttrOr(fi, "count", "") + "'");
        detail::rc::Soup soup;
        const std::size_t nv = (std::size_t)g->count;
        // positions are the geometry's; only the vertices the faces use are copied (a submesh may use a few of a big shared buffer)
        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        bool anyUv = false, anyNrm = false;
        std::vector<std::uint32_t> faceIdx;   // 3 per triangle
        std::size_t seen = 0;
        for (int f : doc.Kids(fi)) {
            if (doc.Name(f) != "face") continue;
            if (++seen > (std::size_t)fc) return Fail(err, "more <face> elements than the faces count " + std::to_string((long)fc));
            double a, b, c;
            if (!Dbl(doc.Attr(f, "v1"), a) || !Dbl(doc.Attr(f, "v2"), b) || !Dbl(doc.Attr(f, "v3"), c) || a < 0 || b < 0 || c < 0 || a != std::floor(a) || b != std::floor(b) || c != std::floor(c))
                return Fail(err, "bad <face> in submesh " + std::to_string(myIdx));
            faceIdx.push_back((std::uint32_t)std::min(a, 4294967295.0)); faceIdx.push_back((std::uint32_t)std::min(b, 4294967295.0)); faceIdx.push_back((std::uint32_t)std::min(c, 4294967295.0));
        }
        (void)partialStrips; (void)badFaces;
        for (std::size_t t = 0; t + 2 < faceIdx.size(); t += 3) {
            if (faceIdx[t] >= nv || faceIdx[t + 1] >= nv || faceIdx[t + 2] >= nv) { ++badIdx; continue; }
            for (int c = 0; c < 3; ++c) {
                const std::uint32_t v = faceIdx[t + (std::size_t)c];
                auto r = remap.emplace(v, (std::uint32_t)soup.pos.size());
                if (r.second) soup.pos.push_back(g->pos[v]);
                soup.ci.push_back(r.first->second);
                soup.cuv.push_back(g->uv[v]);
                soup.cnrm.push_back(g->nrm[v]);
                if (g->hasUv[v]) anyUv = true;
                if (g->hasNrm[v]) anyNrm = true;
            }
        }
        if (soup.ci.empty()) { ++noGeom; continue; }
        if (!anyUv) soup.cuv.clear();
        const bool allNrm = anyNrm && [&] { for (std::size_t c = 0; c < soup.ci.size(); ++c) { const Vec3& n = soup.cnrm[c]; if (n.x == 0 && n.y == 0 && n.z == 0) return false; } return true; }();
        if (!allNrm) soup.cnrm.clear();
        ImportedPart part;
        detail::rc::BuildStats st;
        if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &st, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) return Fail(err, "too many output vertices");
        degenerate += st.degenerate;
        outVerts += part.mesh.vertices.size();
        if (part.mesh.triangles.empty()) continue;
        if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
        // material
        const std::string matName = doc.AttrOr(s, "material", "");
        std::string pname = matName.empty() ? "submesh" + std::to_string(myIdx) : matName;
        { auto nm = names.find(myIdx); if (nm != names.end()) pname = nm->second; }
        part.mesh.name = pname;
        part.node = 0;
        if (!matName.empty()) {
            auto it = matOut.find(matName);
            if (it == matOut.end()) {
                ImportedMaterial im;
                im.name = matName;
                auto mi = mats.find(matName);
                if (mi != mats.end()) {
                    const Mat& m = mi->second;
                    im.hasDiffuse = m.hasDiffuse; im.diffuse = m.diffuse; im.hasEmissive = m.hasEmissive; im.emissive = m.emissive; im.shininess = m.shininess;
                    if (!m.texture.empty()) {
                        std::string t = m.texture;
                        for (char& c : t) if (c == '\\') c = '/';
                        if (!baseDir.empty() && t[0] != '/' && t.find(':') == std::string::npos) t = baseDir + t;
                        im.texture = t;
                    }
                }
                out.materials.push_back(im);
                it = matOut.emplace(matName, (int)out.materials.size() - 1).first;
            }
            part.material = it->second;
        }
        if (doc.First(s, "boneassignments") >= 0) ++boneAssign;
        out.parts.push_back(std::move(part));
    }
    if (out.parts.empty()) return Fail(err, "no submesh has both vertices and triangles");
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("OGRE: " + s); };
    const int skel = doc.First(0, "skeletonlink");
    if (skel >= 0) note("skeleton '" + doc.AttrOr(skel, "name", "") + "' (and the bone assignments of " + std::to_string(boneAssign + (doc.First(0, "boneassignments") >= 0 ? 1 : 0)) + " mesh part(s)) not read: the mesh is imported at its bind pose, rigid");
    else if (doc.First(0, "boneassignments") >= 0) note("bone assignments without a skeleton link not read");
    if (doc.First(0, "poses") >= 0 || doc.First(0, "animations") >= 0) note("morph / pose animations of the mesh are not read");
    if (doc.First(0, "levelofdetail") >= 0) note("levels of detail are not read");
    if (lines) note(std::to_string(lines) + " submesh(es) that are not triangle lists, strips or fans (lines, points) skipped");
    if (noGeom) note(std::to_string(noGeom) + " submesh(es) without geometry or faces skipped");
    if (extraUv) note(std::to_string(extraUv) + " texture coordinate set(s) after the first are not imported");
    if (colours) note(std::to_string(colours) + " vertex colour(s) not imported");
    if (tangents) note("stored tangents / binormals are not imported");
    if (badIdx) note(std::to_string(badIdx) + " triangle(s) with an index past the vertex list skipped");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    if (!scripts || scripts->empty()) note("no .material script was supplied: materials carry their names only");
    note("coordinates, winding and texture coordinates as written; the material scripts' first pass colours and first texture unit texture are read, nothing else of them");
    return true;
}

bool ReadOgreXmlBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* materialScripts) {
    out = ImportedModel{};
    const bool ok = ReadOgreImpl(text, baseDir, out, err, materialScripts);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadOgreXmlFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ogre::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    // the .material scripts next to the mesh (a few, bounded): the material names of the submeshes are looked up in them.
    // The script named like the mesh comes first, then the others in name order; at most 32 are read.
    std::vector<std::string> scripts;
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        std::vector<fs::path> found;
        std::string stem = path.substr(sl == std::string::npos ? 0 : sl + 1);
        if (stem.size() >= 9) stem.resize(stem.size() - 9);   // ".mesh.xml"
        stem = detail::txt::Lower(stem);
        for (fs::directory_iterator it(fs::u8path(dir.empty() ? std::string(".") : dir), ec), end; !ec && it != end && found.size() < 4000; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            if (detail::txt::Lower(it->path().extension().string()) == ".material") found.push_back(it->path());
        }
        std::sort(found.begin(), found.end(), [&](const fs::path& a, const fs::path& b) {
            const bool sa = detail::txt::Lower(a.stem().string()) == stem, sb = detail::txt::Lower(b.stem().string()) == stem;
            if (sa != sb) return sa;
            return a < b;
        });
        for (const fs::path& p : found) {
            if (scripts.size() >= 32) break;
            std::string s, e3;
            if (detail::txt::ReadFileCapped(p.u8string(), 4u * 1024u * 1024u, s, &e3)) scripts.push_back(std::move(s));
        }
    }
    return ReadOgreXmlBuffer(bytes, dir, out, err, &scripts);
}

} // namespace okay
