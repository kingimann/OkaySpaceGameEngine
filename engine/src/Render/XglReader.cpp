// Our own XGL / ZGL reader. Written from the public description of the XGL scene format (an XML file: <WORLD> with
// <MESH> blocks of <P> positions, <N> normals, <TC> texture coordinates, <MAT> materials and <F> faces whose <FV1>...<FVn>
// corners point at them by <PREF>, <NREF>, <TCREF>, and <OBJECT> blocks that place a mesh with a <TRANSFORM>), and of ZGL,
// which is the same text compressed as one zlib stream; no code from any other importer. One part per (mesh, material) on
// the node of each object that uses the mesh. Uses the strict XML reader of the batch-B readers and the engine's own
// inflate. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/own_xgl_reader.md.
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "XmlLite.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace xgl {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;       // the file, and the text after inflating it
constexpr std::size_t kMaxVectors = 8000000, kMaxFaces = 20000000, kMaxObjects = 1000000, kMaxOutVerts = 40000000;
constexpr int kMaxDepth = 256;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "XGL: " + m; return false; }

bool Is(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) { char x = a[i], y = b[i]; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if (y >= 'A' && y <= 'Z') y = (char)(y + 32); if (x != y) return false; }
    return i == a.size() && !b[i];
}

// Attribute "ID" or "id" (the files use both).
bool IdOf(const detail::xml::Doc& d, int i, long& id) {
    const std::string* s = d.Attr(i, "ID");
    if (!s) s = d.Attr(i, "id");
    if (!s) s = d.Attr(i, "Id");
    double v;
    if (!s || !detail::rc::ParseNumber(*s, v) || v < 0 || v > 2e9 || v != std::floor(v)) return false;
    id = (long)v;
    return true;
}
int Kid(const detail::xml::Doc& d, int i, const char* name) {
    for (int k : d.Kids(i)) if (Is(d.Name(k), name)) return k;
    return -1;
}
bool Floats(const std::string& t, std::size_t n, double* v) {
    std::vector<double> l;
    if (!detail::rc::ParseList(t, l, n, true) || l.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) v[i] = l[i];
    return true;
}
bool Int(const std::string& t, long& v) {
    double d;
    if (!detail::rc::ParseNumber(t, d) || d < 0 || d > 2e9 || d != std::floor(d)) return false;
    v = (long)d;
    return true;
}

struct Mat { long id = 0; ImportedMaterial m; };

struct MeshDef {
    std::unordered_map<long, Vec3> p, n;
    std::unordered_map<long, Vec2> tc;
    std::map<long, ImportedMaterial> mats;   // materials declared inside the mesh
    int node = -1;                           // doc node of the <MESH>
    long id = -1;
};

bool ReadMaterial(const detail::xml::Doc& d, int i, ImportedMaterial& m, std::string* err) {
    double v[3];
    const int diff = Kid(d, i, "DIFF"), amb = Kid(d, i, "AMB"), em = Kid(d, i, "EMISS"), sh = Kid(d, i, "SHINE"), al = Kid(d, i, "ALPHA");
    const int dc = diff >= 0 ? diff : amb;
    if (dc >= 0) {
        if (!Floats(d.Text(dc), 3, v)) return Fail(err, "bad colour in a <MAT>");
        m.hasDiffuse = true;
        m.diffuse = Color{(float)std::max(0.0, std::min(1.0, v[0])), (float)std::max(0.0, std::min(1.0, v[1])), (float)std::max(0.0, std::min(1.0, v[2])), 1.0f};
    }
    if (em >= 0) {
        if (!Floats(d.Text(em), 3, v)) return Fail(err, "bad <EMISS> in a <MAT>");
        if (v[0] + v[1] + v[2] > 0) { m.hasEmissive = true; m.emissive = Color{(float)v[0], (float)v[1], (float)v[2], 1.0f}; }
    }
    double x;
    if (sh >= 0) { if (!detail::rc::ParseNumber(d.Text(sh), x)) return Fail(err, "bad <SHINE> in a <MAT>"); if (x >= 0) m.shininess = (float)x; }
    if (al >= 0) {
        if (!detail::rc::ParseNumber(d.Text(al), x)) return Fail(err, "bad <ALPHA> in a <MAT>");
        m.hasDiffuse = true;
        m.diffuse.a = (float)std::max(0.0, std::min(1.0, x));
    }
    return true;
}

} // namespace xgl
} // namespace

static bool ReadXglImpl(const std::string& input, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace xgl;
    (void)baseDir;
    std::string text;
    bool zlibbed = false;
    if (input.size() >= 2 && (unsigned char)input[0] == 0x78 && (((unsigned)(unsigned char)input[0] << 8) | (unsigned char)input[1]) % 31 == 0) {
        std::vector<std::uint8_t> raw;
        if (!okay::Inflate(reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), true, raw, kMaxFileBytes)) return Fail(err, "the zlib stream of this ZGL file is damaged or larger than 256 MiB");
        text.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
        zlibbed = true;
    } else text = input;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);
    detail::xml::Doc doc;
    std::string xerr;
    if (!detail::xml::Parse(text, doc, xerr)) return Fail(err, xerr);
    if (!Is(doc.Name(0), "world")) return Fail(err, "not an XGL file (the root element is <" + doc.Name(0) + ">, not <WORLD>)");
    (void)zlibbed;

    // world-level materials and meshes
    std::map<long, ImportedMaterial> worldMats;
    std::unordered_map<long, int> meshById;   // mesh id -> doc node
    std::vector<int> worldMeshes;
    for (int k : doc.Kids(0)) {
        if (Is(doc.Name(k), "mat")) {
            long id;
            if (!IdOf(doc, k, id)) return Fail(err, "a <MAT> has no ID");
            ImportedMaterial m; m.name = "mat" + std::to_string(id);
            if (!ReadMaterial(doc, k, m, err)) return false;
            worldMats[id] = m;
        } else if (Is(doc.Name(k), "mesh")) {
            worldMeshes.push_back(k);
            long id;
            if (IdOf(doc, k, id)) meshById[id] = k;
        }
    }

    ImportedNode rootNode;
    rootNode.name = "XGL";
    out.nodes.push_back(rootNode);

    std::size_t outVerts = 0;
    int degenerate = 0, badRefs = 0, lines = 0, noFaces = 0, badMeshRef = 0, lights = 0, patches = 0;
    std::map<std::pair<int, long>, int> matOut;   // (mesh node, matref) -> output material index, per use is not needed: materials are per mesh

    // Builds the parts of one <MESH> on `node`.
    auto buildMesh = [&](int mesh, int node) -> bool {
        MeshDef md;
        md.node = mesh;
        // gather the arrays
        for (int k : doc.Kids(mesh)) {
            const std::string& nm = doc.Name(k);
            long id;
            if (Is(nm, "p") || Is(nm, "n") || Is(nm, "tc")) {
                if (!IdOf(doc, k, id)) return Fail(err, "a <" + nm + "> has no ID");
                if (Is(nm, "p")) {
                    double v[3]; if (!Floats(doc.Text(k), 3, v)) return Fail(err, "bad <P> " + std::to_string(id));
                    if (md.p.size() >= kMaxVectors) return Fail(err, "too many <P>");
                    md.p[id] = Vec3{(float)v[0], (float)v[1], (float)v[2]};
                } else if (Is(nm, "n")) {
                    double v[3]; if (!Floats(doc.Text(k), 3, v)) return Fail(err, "bad <N> " + std::to_string(id));
                    if (md.n.size() >= kMaxVectors) return Fail(err, "too many <N>");
                    md.n[id] = Vec3{(float)v[0], (float)v[1], (float)v[2]};
                } else {
                    double v[2]; if (!Floats(doc.Text(k), 2, v)) return Fail(err, "bad <TC> " + std::to_string(id));
                    if (md.tc.size() >= kMaxVectors) return Fail(err, "too many <TC>");
                    md.tc[id] = Vec2{(float)v[0], (float)v[1]};
                }
            } else if (Is(nm, "mat")) {
                if (!IdOf(doc, k, id)) return Fail(err, "a <MAT> has no ID");
                ImportedMaterial m; m.name = "mat" + std::to_string(id);
                if (!ReadMaterial(doc, k, m, err)) return false;
                md.mats[id] = m;
            } else if (Is(nm, "l") || Is(nm, "line")) ++lines;
            else if (Is(nm, "patch")) ++patches;
        }
        // faces grouped by material
        struct Group { detail::rc::Soup soup; std::unordered_map<long, std::uint32_t> remap; bool allN = true, anyUv = false; long mat = -1; };
        std::map<long, Group> groups;
        std::size_t faces = 0;
        std::vector<Vec3> poly;
        for (int f : doc.Kids(mesh)) {
            if (!Is(doc.Name(f), "f")) continue;
            if (++faces > kMaxFaces) return Fail(err, "too many faces");
            long matref = -1;
            const int mr = Kid(doc, f, "MATREF");
            if (mr >= 0 && !Int(doc.Text(mr), matref)) return Fail(err, "bad <MATREF>");
            struct Corner { Vec3 p, n; Vec2 uv; long pr = 0; bool hasN = false, hasUv = false; };
            std::vector<Corner> cs;
            bool bad = false;
            for (int c : doc.Kids(f)) {
                const std::string& cn = doc.Name(c);
                if (cn.size() < 3 || !(cn[0] == 'F' || cn[0] == 'f') || !(cn[1] == 'V' || cn[1] == 'v')) continue;
                long pr, nr, tr;
                Corner co;
                const int pp = Kid(doc, c, "PREF");
                if (pp < 0 || !Int(doc.Text(pp), pr)) { bad = true; break; }
                auto pit = md.p.find(pr);
                if (pit == md.p.end()) { bad = true; break; }
                co.p = pit->second; co.pr = pr;
                const int np = Kid(doc, c, "NREF");
                if (np >= 0 && Int(doc.Text(np), nr)) { auto it = md.n.find(nr); if (it != md.n.end()) { co.n = it->second; co.hasN = true; } }
                const int tp = Kid(doc, c, "TCREF");
                if (tp >= 0 && Int(doc.Text(tp), tr)) { auto it = md.tc.find(tr); if (it != md.tc.end()) { co.uv = it->second; co.hasUv = true; } }
                cs.push_back(co);
                if (cs.size() > 10000) { bad = true; break; }
            }
            if (bad || cs.size() < 3) { ++badRefs; continue; }
            Group& g = groups[matref];
            g.mat = matref;
            // fan (faces are triangles in practice)
            for (std::size_t i = 1; i + 1 < cs.size(); ++i) {
                const Corner* tri[3] = {&cs[0], &cs[i], &cs[i + 1]};
                for (const Corner* co : tri) {
                    auto r = g.remap.emplace(co->pr, (std::uint32_t)g.soup.pos.size());
                    if (r.second) g.soup.pos.push_back(co->p);
                    g.soup.ci.push_back(r.first->second);
                    g.soup.cuv.push_back(co->uv);
                    g.soup.cnrm.push_back(co->n);
                    if (!co->hasN) g.allN = false;
                    if (co->hasUv) g.anyUv = true;
                }
            }
        }
        if (groups.empty()) { ++noFaces; return true; }
        for (auto& gp : groups) {
            Group& g = gp.second;
            if (!g.allN) g.soup.cnrm.clear();
            if (!g.anyUv) g.soup.cuv.clear();
            ImportedPart part;
            detail::rc::BuildStats st;
            if (!detail::rc::BuildMesh(g.soup, 180.0, part.mesh, &st, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) return Fail(err, "too many output vertices");
            degenerate += st.degenerate;
            outVerts += part.mesh.vertices.size();
            if (part.mesh.triangles.empty()) continue;
            if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
            part.node = node;
            // material: the mesh's own, else the world's
            const ImportedMaterial* src = nullptr;
            auto mi = md.mats.find(g.mat);
            if (mi != md.mats.end()) src = &mi->second;
            else { auto wi = worldMats.find(g.mat); if (wi != worldMats.end()) src = &wi->second; }
            if (src) {
                const auto key = std::make_pair(mesh, g.mat);
                auto it = matOut.find(key);
                if (it == matOut.end()) { out.materials.push_back(*src); it = matOut.emplace(key, (int)out.materials.size() - 1).first; }
                part.material = it->second;
            }
            out.parts.push_back(std::move(part));
        }
        return true;
    };

    // objects: an explicit work list, parents before children
    struct Item { int doc; int parentNode; int depth; };
    std::vector<Item> work;
    for (auto it = doc.Kids(0).rbegin(); it != doc.Kids(0).rend(); ++it) if (Is(doc.Name(*it), "object")) work.push_back({*it, 0, 1});
    std::size_t objects = 0;
    bool anyObjectParts = false;
    while (!work.empty()) {
        const Item it = work.back();
        work.pop_back();
        if (++objects > kMaxObjects) return Fail(err, "too many objects");
        if (it.depth > kMaxDepth) return Fail(err, "objects nested deeper than " + std::to_string(kMaxDepth));
        ImportedNode n;
        const int nm = Kid(doc, it.doc, "NAME");
        n.name = nm >= 0 && !doc.Text(nm).empty() ? doc.Text(nm) : "object";
        n.parent = it.parentNode;
        const int tr = Kid(doc, it.doc, "TRANSFORM");
        if (tr >= 0) {
            double f[3] = {0, 0, 1}, u[3] = {0, 1, 0}, p[3] = {0, 0, 0}, sc = 1;
            const int fw = Kid(doc, tr, "FORWARD"), up = Kid(doc, tr, "UP"), po = Kid(doc, tr, "POSITION"), sk = Kid(doc, tr, "SCALE");
            if ((fw >= 0 && !Floats(doc.Text(fw), 3, f)) || (up >= 0 && !Floats(doc.Text(up), 3, u)) || (po >= 0 && !Floats(doc.Text(po), 3, p))) return Fail(err, "bad <TRANSFORM>");
            if (sk >= 0) {
                std::vector<double> sv;
                if (!detail::rc::ParseList(doc.Text(sk), sv, 3, true) || sv.empty()) return Fail(err, "bad <SCALE>");
                sc = sv[0];
            }
            // axes: z = forward, y = up, x = up cross forward (right handed)
            auto norm = [](double* v) { const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-12) { v[0] /= l; v[1] /= l; v[2] /= l; } return l > 1e-12; };
            if (norm(f) && norm(u)) {
                double x[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
                if (norm(x)) {
                    // re-orthogonalise y so a slightly off UP still gives a rotation
                    double y[3] = {f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0]};
                    norm(y);
                    double m[9] = {x[0], y[0], f[0], x[1], y[1], f[1], x[2], y[2], f[2]};   // columns x, y, z
                    const double tr3 = m[0] + m[4] + m[8];
                    double qw, qx, qy, qz;
                    if (tr3 > 0) { const double S = std::sqrt(tr3 + 1.0) * 2; qw = 0.25 * S; qx = (m[7] - m[5]) / S; qy = (m[2] - m[6]) / S; qz = (m[3] - m[1]) / S; }
                    else if (m[0] > m[4] && m[0] > m[8]) { const double S = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2; qw = (m[7] - m[5]) / S; qx = 0.25 * S; qy = (m[1] + m[3]) / S; qz = (m[2] + m[6]) / S; }
                    else if (m[4] > m[8]) { const double S = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2; qw = (m[2] - m[6]) / S; qx = (m[1] + m[3]) / S; qy = 0.25 * S; qz = (m[5] + m[7]) / S; }
                    else { const double S = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2; qw = (m[3] - m[1]) / S; qx = (m[2] + m[6]) / S; qy = (m[5] + m[7]) / S; qz = 0.25 * S; }
                    n.rotation = Quat{(float)qx, (float)qy, (float)qz, (float)qw}.Normalized();
                }
            }
            n.position = Vec3{(float)p[0], (float)p[1], (float)p[2]};
            if (std::isfinite(sc) && sc != 0) n.scale = Vec3{(float)sc, (float)sc, (float)sc};
        }
        const int me = (int)out.nodes.size();
        out.nodes.push_back(n);
        // the object's mesh: inline, or by <MESHREF>
        const std::size_t before = out.parts.size();
        const int inl = Kid(doc, it.doc, "MESH");
        const int ref = Kid(doc, it.doc, "MESHREF");
        if (inl >= 0) { if (!buildMesh(inl, me)) return false; }
        else if (ref >= 0) {
            long id;
            auto f = Int(doc.Text(ref), id) ? meshById.find(id) : meshById.end();
            if (f == meshById.end()) ++badMeshRef;
            else if (!buildMesh(f->second, me)) return false;
        }
        if (out.parts.size() > before) anyObjectParts = true;
        if (Kid(doc, it.doc, "LIGHT") >= 0) ++lights;
        // children, in file order (pushed backwards so they pop in order)
        std::vector<int> kids;
        for (int k : doc.Kids(it.doc)) if (Is(doc.Name(k), "object")) kids.push_back(k);
        for (auto r = kids.rbegin(); r != kids.rend(); ++r) work.push_back({*r, me, it.depth + 1});
    }
    // a file with meshes but no object that uses them: each world-level mesh once, at the root
    if (!anyObjectParts && objects == 0)
        for (int m : worldMeshes) if (!buildMesh(m, 0)) return false;

    if (out.parts.empty()) return Fail(err, "the file has no mesh with faces");
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("XGL: " + s); };
    if (badRefs) note(std::to_string(badRefs) + " face(s) with a missing or unknown <PREF> skipped");
    if (badMeshRef) note(std::to_string(badMeshRef) + " object(s) with an unknown <MESHREF>");
    if (lines) note(std::to_string(lines) + " line element(s) skipped");
    if (patches) note(std::to_string(patches) + " patch element(s) skipped");
    if (noFaces) note(std::to_string(noFaces) + " mesh(es) without faces");
    if (lights) note(std::to_string(lights) + " light element(s) not imported");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    note("backgrounds, scene lighting, textures, the object data blocks and transparency values other than <ALPHA> are not imported; faces that give a normal per corner keep it");
    return true;
}

bool ReadXglBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadXglImpl(bytes, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadXglFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, xgl::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadXglBuffer(bytes, dir, out, err);
}

} // namespace okay
