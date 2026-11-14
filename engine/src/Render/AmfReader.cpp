// Our own AMF reader. Written from the public ASTM ISO/ASTM 52915 description of the format, no code from any other
// importer. See docs/own_amf_reader.md.
#include "FileOpen.hpp"
#include "ReaderCommon.hpp"
#include "XmlLite.hpp"
#include "ZipLite.hpp"
#include "okay/Render/AmfReader.hpp"
#include <map>
#include <set>
#include <unordered_map>

namespace okay {

namespace {
namespace amf {

using namespace okay::detail;
using rc::M4;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxVerts     = 20000000;
constexpr std::size_t kMaxTris      = 20000000;
constexpr std::size_t kMaxObjects   = 1000000;
constexpr std::size_t kMaxInstances = 100000;
constexpr std::size_t kMaxOutTris   = 20000000;
constexpr std::size_t kMaxParts     = 200000;
constexpr std::size_t kMaxMaterials = 100000;
constexpr double      kCrease       = 45.0;

struct Rgba { bool has = false; float c[4] = {1, 1, 1, 1}; };
struct Tri { std::uint32_t v[3]; Rgba col; };
struct Volume { std::string materialId; Rgba col; std::vector<Tri> tris; };
struct Obj {
    std::string id, name;
    Rgba col;
    std::vector<double> v;
    std::vector<Volume> vols;
};
struct Mat { std::string name; Rgba col; };
struct Inst { std::string objectId; M4 xf; };

struct Reader {
    std::string err;
    ImportedModel* out = nullptr;
    bool flatten = false;
    std::vector<Obj> objs;
    std::map<std::string, std::size_t> objById;
    std::map<std::string, Mat> mats;
    std::vector<Inst> insts;
    std::map<std::string, int> matIndex;   // resolved key -> ImportedMaterial index
    std::string unit = "millimeter";
    int textures = 0, vertexColors = 0, vertexNormals = 0, formulas = 0, badRefs = 0, badTris = 0, curved = 0, unplaced = 0, nestedConst = 0, degenerate = 0, badColors = 0, otherConst = 0;
    std::size_t totalVerts = 0, totalTris = 0, instances = 0, outTris = 0;

    bool Fail(const std::string& m) { if (err.empty()) err = "AMF: " + m; return false; }

    // Number in the text of child element `name` of node i. 0 = absent, 1 = ok, -1 = bad (formula or junk).
    int Num(const xml::Doc& d, int i, const char* name, double& v) {
        const int k = d.First(i, name);
        if (k < 0) return 0;
        if (!rc::ParseNumber(d.Text(k), v)) return -1;
        return 1;
    }
    bool ReadColor(const xml::Doc& d, int i, Rgba& c) {   // i = a <color> element
        c.has = true;
        const char* names[4] = {"r", "g", "b", "a"};
        for (int k = 0; k < 4; ++k) {
            double v;
            const int r = Num(d, i, names[k], v);
            if (r < 0) { ++formulas; c.has = false; return true; }
            if (r > 0) c.c[k] = (float)std::max(0.0, std::min(1.0, v));
            else if (k < 3) c.c[k] = 0;
        }
        if (d.First(i, "composite") >= 0) { ++formulas; c.has = false; }
        return true;
    }
    int MaterialFor(const Rgba& c, const std::string& name) {
        char key[96];
        std::snprintf(key, sizeof key, "%.4f,%.4f,%.4f,%.4f|", c.c[0], c.c[1], c.c[2], c.c[3]);
        const std::string k = std::string(key) + name;
        auto it = matIndex.find(k);
        if (it != matIndex.end()) return it->second;
        if (out->materials.size() >= kMaxMaterials) return -1;
        ImportedMaterial m;
        m.name = name.empty() ? "color" + std::to_string(out->materials.size()) : name;
        m.hasDiffuse = true;
        m.diffuse = Color(c.c[0], c.c[1], c.c[2], c.c[3]);
        out->materials.push_back(m);
        const int idx = (int)out->materials.size() - 1;
        matIndex[k] = idx;
        return idx;
    }

    std::string NameOf(const xml::Doc& d, int i) {
        for (int k : d.Kids(i)) if (d.Name(k) == "metadata" && d.AttrOr(k, "type") == "name") return d.Text(k);
        return std::string();
    }

    bool ParseDoc(const std::string& xmlText) {
        xml::Doc d;
        xml::Limits lim; lim.maxElements = 5000000;
        std::string e;
        if (!xml::Parse(xmlText, d, e, lim)) return Fail(e);
        if (d.Name(0) != "amf") return Fail("the root element is not <amf>");
        unit = rc::Lower(d.AttrOr(0, "unit", "millimeter"));
        for (int k : d.Kids(0)) {
            const std::string& nm = d.Name(k);
            if (nm == "material") {
                if (mats.size() >= kMaxMaterials) return Fail("too many materials");
                Mat m; m.name = NameOf(d, k);
                const int c = d.First(k, "color");
                if (c >= 0) ReadColor(d, c, m.col);
                mats[d.AttrOr(k, "id")] = m;
            } else if (nm == "texture") ++textures;
            else if (nm == "object") {
                if (objs.size() >= kMaxObjects) return Fail("too many objects");
                Obj o; o.id = d.AttrOr(k, "id"); o.name = NameOf(d, k);
                const int oc = d.First(k, "color");
                if (oc >= 0) ReadColor(d, oc, o.col);
                const int mesh = d.First(k, "mesh");
                if (mesh >= 0) {
                    const int vs = d.First(mesh, "vertices");
                    if (vs >= 0) for (int vx : d.Kids(vs)) {
                        if (d.Name(vx) != "vertex") continue;
                        const int co = d.First(vx, "coordinates");
                        double x = 0, y = 0, z = 0;
                        if (co < 0 || Num(d, co, "x", x) < 0 || Num(d, co, "y", y) < 0 || Num(d, co, "z", z) < 0) return Fail("a vertex with a missing or invalid coordinate");
                        if (d.First(vx, "color") >= 0) ++vertexColors;
                        if (d.First(vx, "normal") >= 0) ++vertexNormals;
                        if (++totalVerts > kMaxVerts) return Fail("too many vertices");
                        o.v.push_back(x); o.v.push_back(y); o.v.push_back(z);
                    }
                    for (int vl : d.Kids(mesh)) {
                        if (d.Name(vl) == "edge" ) { ++curved; continue; }
                        if (d.Name(vl) != "volume") continue;
                        Volume vol; vol.materialId = d.AttrOr(vl, "materialid");
                        if (d.Attr(vl, "materialid") == nullptr) vol.materialId = "\x01";
                        const int vc = d.First(vl, "color");
                        if (vc >= 0) ReadColor(d, vc, vol.col);
                        if (d.Attr(vl, "type")) {}   // volume type (object / support) is not used
                        for (int tx : d.Kids(vl)) {
                            if (d.Name(tx) == "map") { ++textures; continue; }
                            if (d.Name(tx) != "triangle") continue;
                            Tri t;
                            for (int q = 0; q < 3; ++q) {
                                const char* nm[3] = {"v1", "v2", "v3"};
                                const int ve = d.First(tx, nm[q]);
                                std::vector<long long> iv;
                                if (ve < 0 || !rc::ParseIntList(d.Text(ve), iv, 1) || iv.size() != 1 || iv[0] < 0) return Fail("a triangle with a missing or invalid vertex index");
                                t.v[q] = (std::uint32_t)iv[0];
                            }
                            const int tc = d.First(tx, "color");
                            if (tc >= 0) ReadColor(d, tc, t.col);
                            if (++totalTris > kMaxTris) return Fail("too many triangles");
                            vol.tris.push_back(t);
                        }
                        o.vols.push_back(std::move(vol));
                    }
                }
                objById[o.id] = objs.size();
                objs.push_back(std::move(o));
            } else if (nm == "constellation") {
                if (!insts.empty() || otherConst) { ++otherConst; continue; }   // the first constellation is the default one
                bool any = false;
                for (int in : d.Kids(k)) {
                    if (d.Name(in) == "constellation") { ++nestedConst; continue; }
                    if (d.Name(in) != "instance") continue;
                    any = true;
                    Inst it; it.objectId = d.AttrOr(in, "objectid");
                    double dx = 0, dy = 0, dz = 0, rx = 0, ry = 0, rz = 0;
                    const char* nm2[6] = {"deltax", "deltay", "deltaz", "rx", "ry", "rz"};
                    double* tgt[6] = {&dx, &dy, &dz, &rx, &ry, &rz};
                    for (int q = 0; q < 6; ++q) if (Num(d, in, nm2[q], *tgt[q]) < 0) return Fail("an instance with an invalid delta or rotation");
                    const double kRad = 3.14159265358979323846 / 180.0;
                    // rotate about x, then y, then z, then move
                    it.xf = rc::Mul(rc::Translate(dx, dy, dz), rc::Mul(rc::AxisAngle(0, 0, 1, rz * kRad), rc::Mul(rc::AxisAngle(0, 1, 0, ry * kRad), rc::AxisAngle(1, 0, 0, rx * kRad))));
                    insts.push_back(it);
                    if (insts.size() > kMaxInstances) return Fail("too many instances");
                }
                if (!any) ++otherConst;
            }
        }
        if (objs.empty()) return Fail("the file has no objects");
        return true;
    }

    bool Build() {
        ImportedModel& m = *out;
        ImportedNode root; root.name = "RootNode";
        m.nodes.push_back(root);
        std::vector<Inst> place = insts;
        if (place.empty()) for (const Obj& o : objs) { Inst i; i.objectId = o.id; place.push_back(i); }
        else {
            std::set<std::string> used;
            for (const Inst& i : place) used.insert(i.objectId);
            for (const Obj& o : objs) if (!used.count(o.id)) ++unplaced;
        }
        std::size_t emptyInst = 0;
        for (const Inst& inst : place) {
            auto f = objById.find(inst.objectId);
            if (f == objById.end()) { ++badRefs; continue; }
            if (++instances > kMaxInstances) return Fail("too many instances");
            const Obj& o = objs[f->second];
            const bool mirror = rc::Det3(inst.xf) < 0;
            const std::size_t nv = o.v.size() / 3;
            std::map<int, rc::Soup> soups;   // material + 1
            std::unordered_map<std::uint64_t, std::uint32_t> remap;
            for (const Volume& vol : o.vols) {
                // the volume's own material or colour, else the object's
                int volMat = -2;   // -2 = decide per triangle
                int matBase = -1;
                if (vol.materialId != "\x01") {
                    auto mt = mats.find(vol.materialId);
                    if (mt != mats.end()) { matBase = mt->second.col.has ? MaterialFor(mt->second.col, mt->second.name) : -1; if (matBase < 0) ++badColors; }
                    else ++badRefs;
                } else if (vol.col.has) matBase = MaterialFor(vol.col, "");
                else if (o.col.has) matBase = MaterialFor(o.col, "");
                (void)volMat;
                for (const Tri& t : vol.tris) {
                    if (t.v[0] >= nv || t.v[1] >= nv || t.v[2] >= nv) { ++badTris; continue; }
                    const int mi = t.col.has ? MaterialFor(t.col, "") : matBase;
                    rc::Soup& s = soups[mi + 1];
                    if (++outTris > kMaxOutTris) return Fail("too many triangles");
                    const std::uint32_t src[3] = {t.v[0], mirror ? t.v[2] : t.v[1], mirror ? t.v[1] : t.v[2]};
                    std::uint32_t ix[3];
                    for (int k = 0; k < 3; ++k) {
                        const std::uint64_t key = (std::uint64_t)(unsigned)(mi + 1) << 32 | src[k];
                        auto it = remap.find(key);
                        if (it != remap.end()) { ix[k] = it->second; continue; }
                        double x, y, z;
                        rc::Apply(inst.xf, o.v[src[k] * 3], o.v[src[k] * 3 + 1], o.v[src[k] * 3 + 2], x, y, z);
                        if (!std::isfinite(x + y + z) || std::fabs(x) > rc::kMaxCoord || std::fabs(y) > rc::kMaxCoord || std::fabs(z) > rc::kMaxCoord) return Fail("coordinates overflow");
                        s.pos.push_back(Vec3{(float)x, (float)y, (float)z});
                        ix[k] = (std::uint32_t)s.pos.size() - 1;
                        remap.emplace(key, ix[k]);
                    }
                    s.ci.push_back(ix[0]); s.ci.push_back(ix[1]); s.ci.push_back(ix[2]);
                }
            }
            ImportedNode nd;
            nd.name = !o.name.empty() ? o.name : "object" + o.id;
            nd.parent = 0;
            const int ni = (int)m.nodes.size();
            bool any = false;
            for (auto& kv : soups) {
                ImportedPart p;
                rc::BuildStats bs;
                if (!rc::BuildMesh(kv.second, kCrease, p.mesh, &bs)) return Fail("too many output vertices");
                degenerate += bs.degenerate;
                if (p.mesh.triangles.empty()) continue;
                if (m.parts.size() >= kMaxParts) return Fail("too many mesh parts");
                p.material = kv.first - 1; p.node = ni;
                if (!rc::PartInRange(p)) return Fail("coordinates overflow");
                m.parts.push_back(std::move(p));
                any = true;
            }
            if (any) m.nodes.push_back(nd); else ++emptyInst;
        }
        if (emptyInst) out->notes.push_back("AMF: " + std::to_string(emptyInst) + " object instance(s) without triangles skipped");
        if (m.parts.empty()) return Fail("the file has no triangles");
        return true;
    }

    bool Run(const std::string& bytes, ImportedModel& model, bool flat) {
        out = &model; flatten = flat;
        if (bytes.size() > kMaxFileBytes) return Fail("file too large");
        std::string text;
        if (bytes.size() >= 2 && bytes.compare(0, 2, "PK") == 0) {
            zip::Archive z;
            auto zerr = [&]() { return Fail("zip package: " + z.err.substr(z.err.compare(0, 5, "ZIP: ") == 0 ? 5 : 0)); };
            if (!z.Open(bytes)) return zerr();
            int pick = -1;
            for (std::size_t i = 0; i < z.entries.size() && pick < 0; ++i) {
                const std::string l = rc::Lower(z.entries[i].name);
                if (l.size() > 4 && l.compare(l.size() - 4, 4, ".amf") == 0) pick = (int)i;
            }
            for (std::size_t i = 0; i < z.entries.size() && pick < 0; ++i) if (!z.entries[i].name.empty() && z.entries[i].name.back() != '/') pick = (int)i;
            if (pick < 0) return Fail("the zip archive is empty");
            if (!z.Extract(pick, text)) return zerr();
        } else text = bytes;
        if (!ParseDoc(text)) return false;
        if (!Build()) return false;
        rc::ApplyUpAxis(model, true, flat);
        auto note = [&](const std::string& s) { model.notes.push_back("AMF: " + s); };
        if (unit != "millimeter") note("unit is '" + unit + "'; coordinates are kept in the file's units");
        if (textures) note(std::to_string(textures) + " texture / texture map element(s) not read");
        if (vertexColors) note(std::to_string(vertexColors) + " vertex colour(s) not read (triangle, volume, object and material colours are)");
        if (vertexNormals) note(std::to_string(vertexNormals) + " vertex normal(s) not used; smooth normals are generated");
        if (formulas) note(std::to_string(formulas) + " colour(s) given as a formula, composite or an invalid number not read");
        if (curved) note(std::to_string(curved) + " curved edge(s) not read; triangles are flat");
        if (nestedConst) note(std::to_string(nestedConst) + " nested constellation(s) not read");
        if (otherConst) note(std::to_string(otherConst) + " further constellation(s) not read; the first is used");
        if (unplaced) note(std::to_string(unplaced) + " object(s) not in the constellation were not output");
        if (badRefs) note(std::to_string(badRefs) + " reference(s) to a missing object or material skipped");
        if (badTris) note(std::to_string(badTris) + " triangle(s) with an out-of-range vertex index skipped");
        if (badColors) note(std::to_string(badColors) + " material(s) without a usable colour");
        if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        return true;
    }
};

} // namespace amf
} // namespace

bool ReadAmfBuffer(const std::string& bytes, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    amf::Reader r;
    if (!r.Run(bytes, out, flatten)) {
        const std::string m = r.err.empty() ? "AMF: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool ReadAmfFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    bool big = false;
    std::string bytes = detail::rc::ReadAllBounded(fp, amf::kMaxFileBytes, &big);
    std::fclose(fp);
    if (big) { if (err) *err = "file too large"; return false; }
    return ReadAmfBuffer(bytes, out, err, flatten);
}

} // namespace okay
