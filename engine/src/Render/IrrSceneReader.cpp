// Our own Irrlicht scene (.irr) reader. Written from the public description of the Irrlicht engine's scene file (an XML
// file written by irrEdit and by the engine's scene manager: <irr_scene> with <attributes> and nested <node type="..."> elements
// that hold typed attributes, <materials> and <animators>); no code from any other importer. Builds the node tree with the
// positions, rotations (degrees) and scales of the nodes; "cube" and "sphere" nodes become meshes of their own; "mesh" and
// "animatedMesh" nodes name another model file, which is read through the loader the caller supplies (the import seam gives
// one that uses the engine's own readers). Uses the strict XML reader of the batch-B readers. Bounds-checked, capped, no
// recursion. See okay/Render/ModelData.hpp and docs/own_irr_reader.md.
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "XmlLite.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace irrs {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxNodes = 1000000, kMaxOutVerts = 40000000, kMaxLoadedFiles = 1000;
constexpr int kMaxDepth = 256;
constexpr double kPi = 3.14159265358979323846;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "IRR: " + m; return false; }

std::string Lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; }

// <attributes> child with this name: its "value".
const std::string* Value(const detail::xml::Doc& d, int attrs, const char* name) {
    if (attrs < 0) return nullptr;
    for (int k : d.Kids(attrs)) {
        const std::string* n = d.Attr(k, "name");
        if (n && *n == name) return d.Attr(k, "value");
    }
    return nullptr;
}
bool Vec(const std::string* s, double* v, std::size_t n) {
    std::vector<double> l;
    if (!s || !detail::rc::ParseList(*s, l, n, true) || l.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) v[i] = l[i];
    return true;
}
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
Color ArgbToColor(std::uint32_t v) { return Color{(float)((v >> 16) & 255) / 255.0f, (float)((v >> 8) & 255) / 255.0f, (float)(v & 255) / 255.0f, (float)(v >> 24) / 255.0f}; }

std::string FixPath(std::string t, const std::string& baseDir) {
    for (char& c : t) if (c == '\\') c = '/';
    if (t.compare(0, 2, "./") == 0) t.erase(0, 2);
    if (!t.empty() && !baseDir.empty() && t[0] != '/' && t.find(':') == std::string::npos) t = baseDir + t;
    return t;
}

// Rotation of Irrlicht's Euler angles (degrees): R = Rz * Ry * Rx, positive angles counter-clockwise about each axis.
Quat EulerQuat(double rx, double ry, double rz) {
    const double d = kPi / 180.0;
    auto q = [](double ax, double ay, double az, double a) { const double s = std::sin(a / 2); return Quat{(float)(ax * s), (float)(ay * s), (float)(az * s), (float)std::cos(a / 2)}; };
    return (q(0, 0, 1, rz * d) * q(0, 1, 0, ry * d) * q(1, 0, 0, rx * d)).Normalized();
}

ImportedMaterial ReadMaterial(const detail::xml::Doc& d, int attrs, const std::string& baseDir) {
    ImportedMaterial m;
    m.name = "material";
    std::uint32_t c;
    double v;
    if (const std::string* s = Value(d, attrs, "Diffuse")) if (HexColor(*s, c)) { m.hasDiffuse = true; m.diffuse = ArgbToColor(c); }
    if (const std::string* s = Value(d, attrs, "Emissive")) if (HexColor(*s, c)) { const Color e = ArgbToColor(c); if (e.r + e.g + e.b > 0) { m.hasEmissive = true; m.emissive = e; } }
    if (const std::string* s = Value(d, attrs, "Shininess")) if (detail::rc::ParseNumber(*s, v) && v >= 0) m.shininess = (float)v;
    if (const std::string* s = Value(d, attrs, "Texture1")) if (!s->empty()) m.texture = FixPath(*s, baseDir);
    const std::string type = Lower(Value(d, attrs, "Type") ? *Value(d, attrs, "Type") : std::string());
    if (const std::string* s = Value(d, attrs, "Texture2")) if (!s->empty() && (type.compare(0, 9, "normalmap") == 0 || type.compare(0, 11, "parallaxmap") == 0)) m.normalMap = FixPath(*s, baseDir);
    return m;
}

// A cube of side `size` centred on the origin, outward counter-clockwise triangles, flat normals, each face with texture coordinates 0..1.
void MakeCube(double size, Mesh& m) {
    const float h = (float)(size / 2);
    struct Face { Vec3 n, u, v; };   // normal and the two in-plane axes (u right, v down the picture)
    const Face faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, -1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}}, {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},
                           {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}}};
    for (const Face& f : faces) {
        const int base = (int)m.vertices.size();
        const float cu[4] = {-1, 1, 1, -1}, cv[4] = {1, 1, -1, -1};   // corners: bottom-left, bottom-right, top-right, top-left (v down means +v is the bottom)
        for (int i = 0; i < 4; ++i) {
            m.vertices.push_back(Vec3{h * (f.n.x + cu[i] * f.u.x + cv[i] * f.v.x), h * (f.n.y + cu[i] * f.u.y + cv[i] * f.v.y), h * (f.n.z + cu[i] * f.u.z + cv[i] * f.v.z)});
            m.normals.push_back(f.n);
            m.uvs.push_back(Vec2{(cu[i] + 1) / 2, (cv[i] + 1) / 2});
        }
        for (int i : {0, 1, 2, 0, 2, 3}) m.triangles.push_back(base + i);
    }
    // make sure every face winds outward whatever the axis choices above came to
    for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
        const Vec3 a = m.vertices[(std::size_t)m.triangles[t]], b = m.vertices[(std::size_t)m.triangles[t + 1]], c = m.vertices[(std::size_t)m.triangles[t + 2]];
        const Vec3 n = Vec3::Cross(b - a, c - a);
        const Vec3 nn = m.normals[(std::size_t)m.triangles[t]];
        if (n.x * nn.x + n.y * nn.y + n.z * nn.z < 0) std::swap(m.triangles[t + 1], m.triangles[t + 2]);
    }
}

// A sphere of radius r with `px` segments around Y and `py` rings from pole to pole; texture coordinates u around, v from the north pole down.
bool MakeSphere(double r, long px, long py, Mesh& m) {
    px = std::max(3L, std::min(px, 512L)); py = std::max(2L, std::min(py, 512L));
    for (long j = 0; j <= py; ++j) for (long i = 0; i <= px; ++i) {
        const double th = kPi * (double)j / (double)py, ph = 2 * kPi * (double)i / (double)px;
        const Vec3 n{(float)(std::sin(th) * std::cos(ph)), (float)std::cos(th), (float)(std::sin(th) * std::sin(ph))};
        m.vertices.push_back(Vec3{(float)(n.x * r), (float)(n.y * r), (float)(n.z * r)});
        m.normals.push_back(n);
        m.uvs.push_back(Vec2{(float)i / (float)px, (float)j / (float)py});
    }
    for (long j = 0; j < py; ++j) for (long i = 0; i < px; ++i) {
        const int a = (int)(j * (px + 1) + i), b = a + 1, c = a + (int)(px + 1), d = c + 1;
        if (j != 0) { m.triangles.push_back(a); m.triangles.push_back(c); m.triangles.push_back(b); }
        if (j != py - 1) { m.triangles.push_back(b); m.triangles.push_back(c); m.triangles.push_back(d); }
    }
    return true;
}

} // namespace irrs
} // namespace

static bool ReadIrrSceneImpl(const std::string& input, const std::string& baseDir, ImportedModel& out, std::string* err, const IrrMeshLoader* loader) {
    using namespace irrs;
    std::string text = input;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);
    detail::xml::Doc doc;
    std::string xerr;
    if (!detail::xml::Parse(text, doc, xerr)) return Fail(err, xerr);
    if (Lower(doc.Name(0)) != "irr_scene") return Fail(err, "not an Irrlicht scene (the root element is <" + doc.Name(0) + ">, not <irr_scene>)");

    ImportedNode root;
    root.name = "IRRScene";
    out.nodes.push_back(root);

    struct Item { int doc; int parent; int depth; };
    std::vector<Item> work;
    for (auto it = doc.Kids(0).rbegin(); it != doc.Kids(0).rend(); ++it) if (Lower(doc.Name(*it)) == "node") work.push_back({*it, 0, 1});

    struct Loaded { bool ok = false; ImportedModel m; std::vector<int> matMap; std::vector<char> matCopied; };
    std::map<std::string, std::unique_ptr<Loaded>> loadedFiles;
    std::map<std::string, int> typeCount;
    int nodes = 0, animators = 0, unreadable = 0, noLoader = 0, droppedClips = 0, badTransform = 0, matOverrides = 0;
    std::size_t outVerts = 0;
    std::string firstMissing;

    while (!work.empty()) {
        const Item it = work.back();
        work.pop_back();
        if (++nodes > (int)kMaxNodes) return Fail(err, "too many nodes");
        if (it.depth > kMaxDepth) return Fail(err, "nodes nested deeper than " + std::to_string(kMaxDepth));
        const std::string type = doc.AttrOr(it.doc, "type", "");
        const std::string ltype = Lower(type);
        ++typeCount[ltype.empty() ? "(none)" : ltype];
        const int attrs = doc.First(it.doc, "attributes");

        ImportedNode n;
        n.parent = it.parent;
        n.name = Value(doc, attrs, "Name") && !Value(doc, attrs, "Name")->empty() ? *Value(doc, attrs, "Name") : (type.empty() ? "node" : type);
        double p[3] = {0, 0, 0}, r[3] = {0, 0, 0}, s[3] = {1, 1, 1};
        const bool okP = !Value(doc, attrs, "Position") || Vec(Value(doc, attrs, "Position"), p, 3);
        const bool okR = !Value(doc, attrs, "Rotation") || Vec(Value(doc, attrs, "Rotation"), r, 3);
        const bool okS = !Value(doc, attrs, "Scale") || Vec(Value(doc, attrs, "Scale"), s, 3);
        if (!okP || !okR || !okS) { ++badTransform; p[0] = p[1] = p[2] = 0; r[0] = r[1] = r[2] = 0; s[0] = s[1] = s[2] = 1; }
        n.position = Vec3{(float)p[0], (float)p[1], (float)p[2]};
        n.rotation = EulerQuat(r[0], r[1], r[2]);
        n.scale = Vec3{(float)s[0], (float)s[1], (float)s[2]};
        const int me = (int)out.nodes.size();
        out.nodes.push_back(n);

        // the first material of the node
        const int mats = doc.First(it.doc, "materials");
        int matAttrs = -1;
        if (mats >= 0) matAttrs = doc.First(mats, "attributes");
        const int anim = doc.First(it.doc, "animators");
        if (anim >= 0) for (int a : doc.Kids(anim)) if (Lower(doc.Name(a)) == "attributes") ++animators;

        if (ltype == "cube" || ltype == "sphere") {
            ImportedPart part;
            if (ltype == "cube") {
                double size = 10;
                if (const std::string* sz = Value(doc, attrs, "Size")) { if (!detail::rc::ParseNumber(*sz, size) || !(size > 0)) size = 10; }
                MakeCube(size, part.mesh);
            } else {
                double rad = 5, px = 16, py = 16;
                if (const std::string* v = Value(doc, attrs, "Radius")) { if (!detail::rc::ParseNumber(*v, rad) || !(rad > 0)) rad = 5; }
                if (const std::string* v = Value(doc, attrs, "PolyCountX")) { if (!detail::rc::ParseNumber(*v, px)) px = 16; }
                if (const std::string* v = Value(doc, attrs, "PolyCountY")) { if (!detail::rc::ParseNumber(*v, py)) py = 16; }
                MakeSphere(rad, (long)px, (long)py, part.mesh);
            }
            outVerts += part.mesh.vertices.size();
            if (outVerts > kMaxOutVerts) return Fail(err, "too many vertices");
            if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
            part.mesh.name = ltype;
            part.node = me;
            if (matAttrs >= 0) { out.materials.push_back(ReadMaterial(doc, matAttrs, baseDir)); part.material = (int)out.materials.size() - 1; }
            out.parts.push_back(std::move(part));
        } else if (ltype == "mesh" || ltype == "animatedmesh") {
            const std::string* mp = Value(doc, attrs, "Mesh");
            if (!mp || mp->empty()) { ++unreadable; if (firstMissing.empty()) firstMissing = "(no Mesh attribute)"; }
            else if (!loader || !*loader) ++noLoader;
            else {
                const std::string path = FixPath(*mp, baseDir);
                auto f = loadedFiles.find(path);
                if (f == loadedFiles.end()) {
                    if (loadedFiles.size() >= kMaxLoadedFiles) return Fail(err, "more than " + std::to_string(kMaxLoadedFiles) + " different mesh files");
                    auto l = std::make_unique<Loaded>();
                    std::string e;
                    l->ok = (*loader)(path, l->m, &e) && !l->m.parts.empty();
                    if (l->ok && l->m.nodes.empty()) l->m.nodes.push_back(ImportedNode{});   // a static mesh with no node tree (OBJ, STL): one root for its parts
                    if (!l->ok) { ++unreadable; if (firstMissing.empty()) firstMissing = *mp + (e.empty() ? std::string() : " (" + e + ")"); l->m = ImportedModel{}; }
                    l->matMap.assign(l->m.materials.size(), -1);
                    f = loadedFiles.emplace(path, std::move(l)).first;
                } else if (!f->second->ok) { ++unreadable; if (firstMissing.empty()) firstMissing = *mp; }
                Loaded& L = *f->second;
                if (L.ok) {
                    if (!L.m.clips.empty()) droppedClips += (int)L.m.clips.size();
                    // copy the mesh's nodes under this node, its parts and (once per file) its materials
                    const int base = (int)out.nodes.size();
                    for (std::size_t i = 0; i < L.m.nodes.size(); ++i) {
                        ImportedNode c = L.m.nodes[i];
                        c.parent = c.parent >= 0 ? base + c.parent : me;
                        out.nodes.push_back(c);
                    }
                    if (out.nodes.size() > kMaxNodes) return Fail(err, "too many nodes");
                    for (const ImportedPart& sp : L.m.parts) {
                        outVerts += sp.mesh.vertices.size();
                        if (outVerts > kMaxOutVerts) return Fail(err, "too many vertices");
                        ImportedPart q = sp;
                        q.node = base + std::max(0, std::min(sp.node, (int)L.m.nodes.size() - 1));
                        if (sp.material >= 0 && (std::size_t)sp.material < L.matMap.size()) {
                            int& mm = L.matMap[(std::size_t)sp.material];
                            if (mm < 0) { out.materials.push_back(L.m.materials[(std::size_t)sp.material]); mm = (int)out.materials.size() - 1; }
                            q.material = mm;
                        } else q.material = -1;
                        out.parts.push_back(std::move(q));
                    }
                    if (matAttrs >= 0) ++matOverrides;
                }
            }
        }
        // children in file order
        std::vector<int> kids;
        for (int k : doc.Kids(it.doc)) if (Lower(doc.Name(k)) == "node") kids.push_back(k);
        for (auto rr = kids.rbegin(); rr != kids.rend(); ++rr) work.push_back({*rr, me, it.depth + 1});
    }
    if (out.parts.empty()) return Fail(err, nodes == 0 ? "the scene has no <node>" : "the scene has no geometry (no cube, sphere or readable mesh node)" + (firstMissing.empty() ? std::string() : "; first mesh that could not be read: " + firstMissing));
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("IRR: " + s); };
    note("Irrlicht is left-handed: coordinates and winding are kept as written (like Assimp); rotations are Euler degrees applied as Rz * Ry * Rx (like Assimp), which is unchecked against Irrlicht itself");
    if (unreadable) note(std::to_string(unreadable) + " mesh node(s) whose file could not be read, first: " + firstMissing);
    if (noLoader) note(std::to_string(noLoader) + " mesh node(s) not loaded: this call had no mesh loader");
    if (animators) note(std::to_string(animators) + " animator(s) (rotation, fly circle, spline, ...) not imported");
    if (droppedClips) note(std::to_string(droppedClips) + " animation clip(s) of the referenced meshes dropped");
    if (matOverrides) note(std::to_string(matOverrides) + " mesh node(s) with their own material list: the mesh file's materials are used, the node's are not applied");
    if (badTransform) note(std::to_string(badTransform) + " node(s) with a bad Position, Rotation or Scale: identity used");
    std::string kinds;
    for (const auto& kv : typeCount) if (kv.first != "cube" && kv.first != "sphere" && kv.first != "mesh" && kv.first != "animatedmesh" && kv.first != "empty") kinds += (kinds.empty() ? "" : ", ") + kv.first + " x" + std::to_string(kv.second);
    if (!kinds.empty()) note("nodes kept as empty nodes, their content not imported: " + kinds);
    note("cubes are Irrlicht's own size (Size is the full edge); spheres are UV spheres with the node's PolyCountX and PolyCountY; material types, flags and wrap modes are not read");
    return true;
}

bool ReadIrrSceneBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err, const IrrMeshLoader* loader) {
    out = ImportedModel{};
    const bool ok = ReadIrrSceneImpl(text, baseDir, out, err, loader);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadIrrSceneFile(const std::string& path, ImportedModel& out, std::string* err, const IrrMeshLoader* loader) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, irrs::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadIrrSceneBuffer(bytes, dir, out, err, loader);
}

} // namespace okay
