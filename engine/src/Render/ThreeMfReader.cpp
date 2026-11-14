// Our own 3MF reader. 3MF is a zip (OPC) package whose 3D/3dmodel.model part is XML. Written from the public
// 3MF Core Specification 1.3, no code from any other importer. See docs/own_3mf_reader.md.
#include "FileOpen.hpp"
#include "ReaderCommon.hpp"
#include "XmlLite.hpp"
#include "ZipLite.hpp"
#include "okay/Render/ThreeMfReader.hpp"
#include <map>
#include <set>
#include <unordered_map>

namespace okay {

namespace {
namespace tmf {

using namespace okay::detail;
using rc::M4;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxVerts     = 20000000;
constexpr std::size_t kMaxTris      = 20000000;
constexpr std::size_t kMaxObjects   = 1000000;
constexpr std::size_t kMaxInstances = 200000;   // component + item expansions together
constexpr int         kMaxCompDepth = 16;
constexpr std::size_t kMaxOutTris   = 20000000;
constexpr std::size_t kMaxParts     = 200000;
constexpr double      kCrease       = 45.0;

struct Comp { int id = 0; M4 xf; bool external = false; };
struct Obj {
    int id = 0;
    std::string name, type = "model";
    int pid = -1, pindex = 0;
    bool hasMesh = false;
    std::vector<double> v;                 // x y z per vertex
    std::vector<std::uint32_t> t;          // 3 per triangle
    std::vector<int> tpid, tp1;            // per triangle, -1 = not given
    std::vector<Comp> comps;
};
struct PropGroup { bool isColor = false; std::vector<Color> colors; std::vector<std::string> names; };

struct Reader {
    std::string err;
    ImportedModel* out = nullptr;
    bool flatten = false;
    std::map<int, Obj> objs;
    std::map<int, PropGroup> groups;
    std::set<int> textureGroups;
    std::map<std::pair<int, int>, int> matIndex;
    int badTris = 0, badRefs = 0, supportObjs = 0, externalComps = 0, texTris = 0, beamObjs = 0, sliceObjs = 0, gradientTris = 0, badProps = 0, extraProps = 0;
    std::string requiredExt, unit = "millimeter";
    std::size_t totalVerts = 0, totalTris = 0, instances = 0, outTris = 0;

    bool Fail(const std::string& m) { if (err.empty()) err = "3MF: " + m; return false; }

    static bool ToInt(const std::string* s, long long& v) {
        if (!s) return false;
        std::vector<long long> l;
        if (!rc::ParseIntList(*s, l, 1) || l.size() != 1) return false;
        v = l[0];
        return true;
    }

    bool Transform(const std::string* s, M4& m) {
        m = M4{};
        if (!s) return true;
        std::vector<double> d;
        if (!rc::ParseList(*s, d, 12) || d.size() != 12) return Fail("a transform is not 12 finite numbers");
        // row vector convention: x' = m0 x + m3 y + m6 z + m9 ...
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) m.at(r, c) = d[(std::size_t)(r * 3 + c)];   // m.at(col, row): column r = input axis
        m.at(3, 0) = d[9]; m.at(3, 1) = d[10]; m.at(3, 2) = d[11];
        return true;
    }

    int PropIndex(const Obj& o, int tpid, int tp1, bool* textured) {
        const int pid = tpid >= 0 ? tpid : o.pid;
        const int pi = tp1 >= 0 ? tp1 : o.pindex;
        if (pid < 0) return -1;
        if (textureGroups.count(pid)) { if (textured) *textured = true; return -1; }
        auto it = groups.find(pid);
        if (it == groups.end()) { ++badProps; return -1; }
        const PropGroup& g = it->second;
        if (pi < 0 || (std::size_t)pi >= g.colors.size()) { ++badProps; return -1; }
        auto key = std::make_pair(pid, pi);
        auto m = matIndex.find(key);
        if (m != matIndex.end()) return m->second;
        ImportedMaterial mat;
        mat.name = g.isColor ? "color" + std::to_string(pid) + "_" + std::to_string(pi) : (pi < (int)g.names.size() && !g.names[(std::size_t)pi].empty() ? g.names[(std::size_t)pi] : "material" + std::to_string(pid) + "_" + std::to_string(pi));
        mat.hasDiffuse = true;
        mat.diffuse = g.colors[(std::size_t)pi];
        out->materials.push_back(mat);
        const int idx = (int)out->materials.size() - 1;
        matIndex[key] = idx;
        return idx;
    }

    struct ItemOut { std::map<int, rc::Soup> bySoup; };   // material index (+1, 0 = none) -> soup

    bool Emit(const Obj& o, const M4& xf, int depth, ItemOut& io, std::set<int>& path) {
        if (depth > kMaxCompDepth) return Fail("components nested too deep");
        if (++instances > kMaxInstances) return Fail("too many component instances");
        if (o.type == "support" || o.type == "solidsupport") { ++supportObjs; return true; }
        if (o.hasMesh) {
            const bool mirror = rc::Det3(xf) < 0;
            const std::size_t nv = o.v.size() / 3;
            std::unordered_map<std::uint64_t, std::uint32_t> remap;
            for (std::size_t t = 0; t < o.t.size() / 3; ++t) {
                const std::uint32_t a = o.t[t * 3], b = o.t[t * 3 + 1], c = o.t[t * 3 + 2];
                if (a >= nv || b >= nv || c >= nv) { ++badTris; continue; }
                bool textured = false;
                const int mi = PropIndex(o, o.tpid[t], o.tp1[t], &textured);
                if (textured) ++texTris;
                rc::Soup& s = io.bySoup[mi + 1];
                if (++outTris > kMaxOutTris) return Fail("too many triangles");
                std::uint32_t ix[3];
                const std::uint32_t src[3] = {a, mirror ? c : b, mirror ? b : c};
                for (int k = 0; k < 3; ++k) {
                    const std::uint64_t key = (std::uint64_t)(unsigned)(mi + 1) << 32 | src[k];
                    // the remap is per (instance, material soup)
                    auto it = remap.find(key);
                    if (it != remap.end()) { ix[k] = it->second; continue; }
                    double x, y, z;
                    rc::Apply(xf, o.v[src[k] * 3], o.v[src[k] * 3 + 1], o.v[src[k] * 3 + 2], x, y, z);
                    if (!std::isfinite(x + y + z) || std::fabs(x) > rc::kMaxCoord || std::fabs(y) > rc::kMaxCoord || std::fabs(z) > rc::kMaxCoord) return Fail("coordinates overflow");
                    s.pos.push_back(Vec3{(float)x, (float)y, (float)z});
                    ix[k] = (std::uint32_t)s.pos.size() - 1;
                    remap.emplace(key, ix[k]);
                }
                s.ci.push_back(ix[0]); s.ci.push_back(ix[1]); s.ci.push_back(ix[2]);
            }
        }
        if (!o.comps.empty()) {
            if (path.count(o.id)) return Fail("a component refers to its own parent");
            path.insert(o.id);
            for (const Comp& c : o.comps) {
                if (c.external) { ++externalComps; continue; }
                auto it = objs.find(c.id);
                if (it == objs.end()) { ++badRefs; continue; }
                if (!Emit(it->second, rc::Mul(xf, c.xf), depth + 1, io, path)) return false;
            }
            path.erase(o.id);
        }
        return true;
    }

    bool ParseModel(const std::string& xmlText) {
        xml::Doc d;
        xml::Limits lim; lim.maxElements = 3000000;
        std::string e;
        if (!xml::Parse(xmlText, d, e, lim)) return Fail(e);
        if (d.Name(0) != "model") return Fail("the model part is not a <model> element");
        unit = rc::Lower(d.AttrOr(0, "unit", "millimeter"));
        requiredExt = d.AttrOr(0, "requiredextensions");
        const int res = d.First(0, "resources");
        if (res < 0) return Fail("no <resources>");
        int textureDefs = 0, composites = 0, multi = 0;
        for (int k : d.Kids(res)) {
            const std::string& nm = d.Name(k);
            long long id = 0;
            if (nm == "basematerials" || nm == "colorgroup") {
                if (!ToInt(d.Attr(k, "id"), id)) return Fail("a property group without an id");
                PropGroup g;
                g.isColor = nm == "colorgroup";
                for (int c : d.Kids(k)) {
                    if (d.Name(c) != (g.isColor ? "color" : "base")) continue;
                    bool ok = false;
                    Color col = Color::Parse(d.AttrOr(c, g.isColor ? "color" : "displaycolor"), &ok, Color(1, 1, 1, 1));
                    if (!ok) { ++badProps; col = Color(1, 1, 1, 1); }
                    g.colors.push_back(col);
                    g.names.push_back(d.AttrOr(c, "name"));
                    if (g.colors.size() > 1000000) return Fail("too many properties in one group");
                }
                groups[(int)id] = std::move(g);
            } else if (nm == "texture2dgroup" || nm == "texture2d") {
                if (nm == "texture2d") ++textureDefs;
                if (ToInt(d.Attr(k, "id"), id)) textureGroups.insert((int)id);
            } else if (nm == "compositematerials") { ++composites; if (ToInt(d.Attr(k, "id"), id)) textureGroups.insert((int)id); }
            else if (nm == "multiproperties") { ++multi; if (ToInt(d.Attr(k, "id"), id)) textureGroups.insert((int)id); }
            else if (nm == "object") {
                if (objs.size() >= kMaxObjects) return Fail("too many objects");
                Obj o;
                if (!ToInt(d.Attr(k, "id"), id)) return Fail("an object without an id");
                o.id = (int)id;
                o.name = d.AttrOr(k, "name");
                o.type = d.AttrOr(k, "type", "model");
                long long v;
                if (ToInt(d.Attr(k, "pid"), v)) o.pid = (int)v;
                if (ToInt(d.Attr(k, "pindex"), v)) o.pindex = (int)v;
                if (d.Attr(k, "slicestackid")) ++sliceObjs;
                for (int c : d.Kids(k)) {
                    if (d.Name(c) == "mesh") {
                        o.hasMesh = true;
                        const int vs = d.First(c, "vertices"), ts = d.First(c, "triangles");
                        if (d.First(c, "beamlattice") >= 0) ++beamObjs;
                        if (vs >= 0) for (int vx : d.Kids(vs)) {
                            if (d.Name(vx) != "vertex") continue;
                            double x, y, z;
                            if (!d.Attr(vx, "x") || !d.Attr(vx, "y") || !d.Attr(vx, "z") || !rc::ParseNumber(*d.Attr(vx, "x"), x) || !rc::ParseNumber(*d.Attr(vx, "y"), y) || !rc::ParseNumber(*d.Attr(vx, "z"), z))
                                return Fail("a vertex with a missing or invalid coordinate");
                            if (++totalVerts > kMaxVerts) return Fail("too many vertices");
                            o.v.push_back(x); o.v.push_back(y); o.v.push_back(z);
                        }
                        if (ts >= 0) for (int tx : d.Kids(ts)) {
                            if (d.Name(tx) != "triangle") continue;
                            long long a, b, c2;
                            if (!ToInt(d.Attr(tx, "v1"), a) || !ToInt(d.Attr(tx, "v2"), b) || !ToInt(d.Attr(tx, "v3"), c2) || a < 0 || b < 0 || c2 < 0)
                                return Fail("a triangle with a missing or invalid vertex index");
                            if (++totalTris > kMaxTris) return Fail("too many triangles");
                            long long p1 = -1, p2 = -1, p3 = -1, tp = -1;
                            ToInt(d.Attr(tx, "p1"), p1); ToInt(d.Attr(tx, "p2"), p2); ToInt(d.Attr(tx, "p3"), p3); ToInt(d.Attr(tx, "pid"), tp);
                            if (p1 >= 0 && ((p2 >= 0 && p2 != p1) || (p3 >= 0 && p3 != p1))) ++gradientTris;
                            o.t.push_back((std::uint32_t)a); o.t.push_back((std::uint32_t)b); o.t.push_back((std::uint32_t)c2);
                            o.tpid.push_back((int)tp); o.tp1.push_back((int)p1);
                        }
                    } else if (d.Name(c) == "components") {
                        for (int cx : d.Kids(c)) {
                            if (d.Name(cx) != "component") continue;
                            Comp cm;
                            long long v2;
                            if (!ToInt(d.Attr(cx, "objectid"), v2)) return Fail("a component without an objectid");
                            cm.id = (int)v2;
                            cm.external = d.Attr(cx, "path") != nullptr;
                            if (!Transform(d.Attr(cx, "transform"), cm.xf)) return false;
                            o.comps.push_back(cm);
                            if (o.comps.size() > 100000) return Fail("too many components in one object");
                        }
                    }
                }
                objs[o.id] = std::move(o);
            }
        }
        if (textureDefs) notes.push_back("3MF: " + std::to_string(textureDefs) + " texture(s) not read; triangles using texture coordinates get no material");
        if (composites) notes.push_back("3MF: " + std::to_string(composites) + " composite material group(s) not read");
        if (multi) notes.push_back("3MF: " + std::to_string(multi) + " multi-property group(s) not read");

        // build items
        struct Item { int obj; M4 xf; std::string name; };
        std::vector<Item> items;
        const int build = d.First(0, "build");
        if (build >= 0) for (int k : d.Kids(build)) {
            if (d.Name(k) != "item") continue;
            long long id;
            if (!ToInt(d.Attr(k, "objectid"), id)) return Fail("a build item without an objectid");
            Item it; it.obj = (int)id;
            if (!Transform(d.Attr(k, "transform"), it.xf)) return false;
            it.name = d.AttrOr(k, "partnumber");
            items.push_back(std::move(it));
            if (items.size() > 100000) return Fail("too many build items");
        }
        if (items.empty()) {
            // not valid per the spec; show every object nothing else places so the file is still useful
            std::set<int> used;
            for (auto& kv : objs) for (const Comp& c : kv.second.comps) used.insert(c.id);
            for (auto& kv : objs) if (!used.count(kv.first)) { Item it; it.obj = kv.first; items.push_back(it); }
            if (!items.empty()) notes.push_back("3MF: no build items; every top-level object was used");
        }
        if (items.empty()) return Fail("the file has no objects to build");

        ImportedModel& m = *out;
        ImportedNode root; root.name = "RootNode";
        m.nodes.push_back(root);
        std::size_t emptyItems = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto it = objs.find(items[i].obj);
            if (it == objs.end()) { ++badRefs; continue; }
            ItemOut io;
            std::set<int> path;
            if (!Emit(it->second, items[i].xf, 0, io, path)) return false;
            ImportedNode nd;
            nd.name = !items[i].name.empty() ? items[i].name : !it->second.name.empty() ? it->second.name : "object" + std::to_string(it->second.id);
            nd.parent = 0;
            const int ni = (int)m.nodes.size();
            bool any = false;
            for (auto& kv : io.bySoup) {
                ImportedPart p;
                rc::BuildStats bs;
                if (!rc::BuildMesh(kv.second, kCrease, p.mesh, &bs)) return Fail("too many output vertices");
                degenerate += bs.degenerate;
                if (p.mesh.triangles.empty()) continue;
                if (m.parts.size() >= kMaxParts) return Fail("too many mesh parts");
                p.material = kv.first - 1;
                p.node = ni;
                if (!rc::PartInRange(p)) return Fail("coordinates overflow");
                m.parts.push_back(std::move(p));
                any = true;
            }
            if (any) m.nodes.push_back(nd); else ++emptyItems;
        }
        if (emptyItems) notes.push_back("3MF: " + std::to_string(emptyItems) + " build item(s) without printable triangles skipped");
        if (m.parts.empty()) return Fail("the file has no triangles");
        return true;
    }

    std::vector<std::string> notes;
    int degenerate = 0;

    bool Run(const std::string& bytes, ImportedModel& model, bool flat) {
        out = &model; flatten = flat;
        std::string xmlText;
        if (bytes.size() > kMaxFileBytes) return Fail("file too large");
        if (bytes.size() >= 4 && bytes.compare(0, 2, "PK") == 0) {
            zip::Archive z;
            if (!z.Open(bytes)) return Fail(z.err.substr(z.err.compare(0, 5, "ZIP: ") == 0 ? 5 : 0).insert(0, "zip package: "));
            std::string partName = "3D/3dmodel.model";
            const int rels = z.Find("_rels/.rels");
            if (rels >= 0) {
                std::string r;
                if (z.Extract(rels, r)) {
                    xml::Doc rd; std::string e;
                    xml::Limits l; l.maxElements = 10000;
                    if (xml::Parse(r, rd, e, l)) for (int k : rd.Kids(0)) {
                        const std::string type = rd.AttrOr(k, "Type");
                        if (type.find("3dmodel") != std::string::npos) {
                            std::string t = rd.AttrOr(k, "Target");
                            while (!t.empty() && t[0] == '/') t.erase(0, 1);
                            if (!t.empty()) { partName = t; break; }
                        }
                    }
                } else z.err.clear();
            }
            const int mi = z.Find(partName);
            if (mi < 0) return Fail("the package has no 3D model part (" + partName + ")");
            if (!z.Extract(mi, xmlText)) return Fail("zip package: " + z.err.substr(z.err.compare(0, 5, "ZIP: ") == 0 ? 5 : 0));
        } else return Fail("not a zip package (3MF files are zip archives)");
        if (!ParseModel(xmlText)) return false;
        rc::ApplyUpAxis(model, true, flat);
        // notes
        auto note = [&](const std::string& s) { model.notes.push_back("3MF: " + s); };
        for (const std::string& n : notes) model.notes.push_back(n);
        if (unit != "millimeter") note("unit is '" + unit + "'; coordinates are kept in the file's units");
        if (!requiredExt.empty()) note("the file requires extension(s) '" + requiredExt + "'; only the core specification is read");
        if (supportObjs) note(std::to_string(supportObjs) + " support object(s) not output");
        if (externalComps) note(std::to_string(externalComps) + " component(s) reference another model file; not read");
        if (beamObjs) note(std::to_string(beamObjs) + " object(s) with a beam lattice: the beams are not read");
        if (sliceObjs) note(std::to_string(sliceObjs) + " object(s) with a slice stack: slices are not read");
        if (texTris) note(std::to_string(texTris) + " triangle(s) use texture, composite or multi-property groups and have no material");
        if (gradientTris) note(std::to_string(gradientTris) + " triangle(s) with different colours per corner: the first corner's colour is used for the face");
        if (badTris) note(std::to_string(badTris) + " triangle(s) with an out-of-range vertex index skipped");
        if (badRefs) note(std::to_string(badRefs) + " reference(s) to a missing object skipped");
        if (badProps) note(std::to_string(badProps) + " material or colour reference(s) not usable");
        if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        return true;
    }
};

} // namespace tmf
} // namespace

bool Read3mfBuffer(const std::string& bytes, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    tmf::Reader r;
    if (!r.Run(bytes, out, flatten)) {
        const std::string m = r.err.empty() ? "3MF: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool Read3mfFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    bool big = false;
    std::string bytes = detail::rc::ReadAllBounded(fp, tmf::kMaxFileBytes, &big);
    std::fclose(fp);
    if (big) { if (err) *err = "file too large"; return false; }
    return Read3mfBuffer(bytes, out, err, flatten);
}

} // namespace okay
