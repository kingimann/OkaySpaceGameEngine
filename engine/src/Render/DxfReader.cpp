// Our own DXF (ASCII) reader. Written from the public description of the DXF group-code format, no code from any other
// importer. See docs/own_dxf_reader.md.
#include "FileOpen.hpp"
#include "PolygonTriangulate.hpp"
#include "ReaderCommon.hpp"
#include "okay/Render/DxfReader.hpp"
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>

namespace okay {

namespace {
namespace dxf {

using namespace okay::detail;
using rc::M4;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxEntities  = 20000000;
constexpr std::size_t kMaxVerts     = 20000000;
constexpr std::size_t kMaxTris      = 20000000;
constexpr std::size_t kMaxBlocks    = 100000;
constexpr std::size_t kMaxInserts   = 100000;
constexpr std::size_t kMaxLayers    = 10000;
constexpr int         kMaxNest      = 8;
constexpr std::size_t kMaxExpanded  = 200000;   // block placements after nesting is expanded
constexpr double      kCrease       = 45.0;

struct Geo {
    std::vector<double> pos;
    std::vector<std::uint32_t> idx;   // 3 per triangle
    std::map<std::array<double, 3>, std::uint32_t> weld;
};
struct Insert { std::string block, layer; double p[3] = {0, 0, 0}, s[3] = {1, 1, 1}, rot = 0; };
struct Container {
    std::map<std::string, Geo> byLayer;
    std::vector<Insert> inserts;
    double base[3] = {0, 0, 0};
};

struct Reader {
    const char* p = nullptr;
    const char* end = nullptr;
    bool havePending = false;
    int pcode = 0;
    std::string_view pval;
    std::string err;
    ImportedModel* out = nullptr;
    std::map<std::string, Container> blocks;
    Container top;
    std::map<std::string, int> skipped;
    std::size_t entities = 0, totalVerts = 0, totalTris = 0, inserts = 0, outTris = 0, expanded = 0;
    int badFaces = 0, badNumbers = 0, extrusion = 0, arrays = 0, colors = 0, missingBlocks = 0, deepInserts = 0, degenerate = 0, truncatedMesh = 0, openMeshes = 0;

    bool Fail(const std::string& m) { if (err.empty()) err = "DXF: " + m; return false; }
    static int ClampInt(double d) { return d >= 2147483647.0 ? 2147483647 : d <= -2147483647.0 ? -2147483647 : (int)d; }   // a double outside int range must not be cast

    static std::string_view Trim(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    }
    bool Line(std::string_view& out_) {
        if (p >= end) return false;
        const char* q = (const char*)std::memchr(p, '\n', (std::size_t)(end - p));
        const char* e = q ? q : end;
        out_ = std::string_view(p, (std::size_t)(e - p));
        p = q ? q + 1 : end;
        return true;
    }
    // next group code / value pair; false at the end of the data. A code that is not a number sets `err`.
    bool Next(int& code, std::string_view& val) {
        if (havePending) { havePending = false; code = pcode; val = pval; return true; }
        while (NextRaw(code, val)) { if (code != 999) return true; }   // 999 is a comment
        return false;
    }
    bool NextRaw(int& code, std::string_view& val) {
        std::string_view a, b;
        if (!Line(a)) return false;
        a = Trim(a);
        if (a.empty()) {   // a blank line where a code should be: tolerate it only at the end of the file
            while (p < end && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) ++p;
            if (p >= end) return false;
            return Fail("a blank line where a group code was expected"), false;
        }
        long v = 0;
        if (a.size() > 6) return Fail("a group code is not a number"), false;
        for (char c : a) { if (c < '0' || c > '9') return Fail("a group code is not a number"), false; v = v * 10 + (c - '0'); }
        if (!Line(b)) return Fail("the file ends after a group code"), false;
        code = (int)v;
        val = Trim(b);
        return true;
    }
    void Push(int code, std::string_view val) { havePending = true; pcode = code; pval = val; }

    bool Num(std::string_view v, double& d) {
        if (!rc::ParseNumber(v.data(), v.data() + v.size(), d)) { ++badNumbers; return false; }
        return true;
    }

    // ---- geometry sinks ----
    std::uint32_t AddPos(Geo& g, const double* v, bool weld) {
        if (weld) {
            auto it = g.weld.find({v[0], v[1], v[2]});
            if (it != g.weld.end()) return it->second;
        }
        const std::uint32_t i = (std::uint32_t)(g.pos.size() / 3);
        g.pos.push_back(v[0]); g.pos.push_back(v[1]); g.pos.push_back(v[2]);
        ++totalVerts;
        if (weld) g.weld.emplace(std::array<double, 3>{v[0], v[1], v[2]}, i);
        return i;
    }
    bool Tri(Geo& g, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        if (++totalTris > kMaxTris) return Fail("too many triangles");
        g.idx.push_back(a); g.idx.push_back(b); g.idx.push_back(c);
        return true;
    }
    Geo* Layer(Container& c, const std::string& name) {
        auto it = c.byLayer.find(name);
        if (it == c.byLayer.end()) { if (c.byLayer.size() >= kMaxLayers) { Fail("too many layers"); return nullptr; } it = c.byLayer.emplace(name, Geo{}).first; }
        return &it->second;
    }

    using Pairs = std::vector<std::pair<int, std::string_view>>;
    bool ReadPairs(Pairs& v) {   // pairs up to (not including) the next code 0
        v.clear();
        int c; std::string_view s;
        while (Next(c, s)) {
            if (c == 0) { Push(c, s); return true; }
            v.emplace_back(c, s);
            if (v.size() > 200000000) return Fail("an entity is too large");
        }
        return err.empty();
    }
    static std::string LayerOf(const Pairs& v) { for (auto& kv : v) if (kv.first == 8) return std::string(kv.second); return "0"; }

    // ---- entities ----
    bool Face3d(const Pairs& v, Container& c) {
        double q[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        bool have[4][3] = {};
        for (auto& kv : v) {
            const int code = kv.first;
            if (code < 10 || code > 33 || (code > 13 && code < 20) || (code > 23 && code < 30)) continue;   // x: 10-13, y: 20-23, z: 30-33
            const int slot = code / 10 - 1, corner = code % 10;
            double d; if (!Num(kv.second, d)) return Fail("an invalid coordinate in a 3DFACE");
            q[corner][slot] = d; have[corner][slot] = true;
        }
        if (!have[3][0] && !have[3][1] && !have[3][2]) std::memcpy(q[3], q[2], sizeof q[3]);   // no fourth corner: a triangle
        Geo* g = Layer(c, LayerOf(v));
        if (!g) return false;
        if (totalVerts + 4 > kMaxVerts) return Fail("too many vertices");
        const std::uint32_t i0 = AddPos(*g, q[0], true), i1 = AddPos(*g, q[1], true), i2 = AddPos(*g, q[2], true);
        if (!Tri(*g, i0, i1, i2)) return false;
        if (q[3][0] != q[2][0] || q[3][1] != q[2][1] || q[3][2] != q[2][2]) {
            const std::uint32_t i3 = AddPos(*g, q[3], true);
            if (!Tri(*g, i0, i2, i3)) return false;
        }
        return true;
    }

    struct PolyVert { double p[3] = {0, 0, 0}; int flags = 0; int f[4] = {0, 0, 0, 0}; };
    bool Polyline(const Pairs& head, Container& c) {
        int flags = 0, m = 0, n = 0;
        for (auto& kv : head) {
            double d;
            if (kv.first == 70 && Num(kv.second, d)) flags = ClampInt(d);
            else if (kv.first == 71 && Num(kv.second, d)) m = ClampInt(d);
            else if (kv.first == 72 && Num(kv.second, d)) n = ClampInt(d);
        }
        const std::string layer = LayerOf(head);
        std::vector<PolyVert> vs;
        int c0; std::string_view s0;
        Pairs pv;
        for (;;) {   // VERTEX records up to SEQEND
            if (!Next(c0, s0)) break;
            if (c0 != 0) continue;   // stray data
            if (s0 == "SEQEND") { Pairs ignore; if (!ReadPairs(ignore)) return false; break; }
            if (s0 != "VERTEX") { Push(c0, s0); break; }   // a polyline that lost its SEQEND
            if (!ReadPairs(pv)) return false;
            PolyVert x;
            for (auto& kv : pv) {
                double d;
                if (kv.first >= 10 && kv.first <= 30 && kv.first % 10 == 0) { if (!Num(kv.second, d)) return Fail("an invalid coordinate in a VERTEX"); x.p[kv.first / 10 - 1] = d; }
                else if (kv.first == 70) { if (Num(kv.second, d)) x.flags = ClampInt(d); }
                else if (kv.first >= 71 && kv.first <= 74) { if (Num(kv.second, d)) x.f[kv.first - 71] = ClampInt(d); }
            }
            if (vs.size() >= kMaxVerts) return Fail("too many vertices");
            vs.push_back(x);
            ++entities;
        }
        const bool polyface = (flags & 64) != 0, grid = (flags & 16) != 0;
        if (!polyface && !grid) { ++skipped[(flags & 8) ? "POLYLINE(3D line)" : "POLYLINE(2D line)"]; return true; }
        Geo* g = Layer(c, layer);
        if (!g) return false;
        if (polyface) {
            std::vector<std::uint32_t> map;   // polyface vertex number (0-based) -> position index
            std::vector<const PolyVert*> faces;
            for (const PolyVert& x : vs) {
                if ((x.flags & 128) && (x.flags & 64)) { if (totalVerts + 1 > kMaxVerts) return Fail("too many vertices"); map.push_back(AddPos(*g, x.p, false)); }
                else if (x.flags & 128) faces.push_back(&x);
            }
            for (const PolyVert* f : faces) {
                int idx[4]; int cnt = 0;
                for (int k = 0; k < 4; ++k) { if (f->f[k] == 0) break; idx[cnt++] = f->f[k] < 0 ? -f->f[k] : f->f[k]; }
                bool ok = cnt >= 3;
                for (int k = 0; k < cnt && ok; ++k) if (idx[k] < 1 || (std::size_t)idx[k] > map.size()) ok = false;
                if (!ok) { ++badFaces; continue; }
                if (!Tri(*g, map[(std::size_t)idx[0] - 1], map[(std::size_t)idx[1] - 1], map[(std::size_t)idx[2] - 1])) return false;
                if (cnt == 4 && !Tri(*g, map[(std::size_t)idx[0] - 1], map[(std::size_t)idx[2] - 1], map[(std::size_t)idx[3] - 1])) return false;
            }
        } else {
            if (m < 2 || n < 2 || (long long)m * n != (long long)vs.size()) { ++truncatedMesh; return true; }
            std::vector<std::uint32_t> map;
            for (const PolyVert& x : vs) { if (totalVerts + 1 > kMaxVerts) return Fail("too many vertices"); map.push_back(AddPos(*g, x.p, false)); }
            const bool closeM = (flags & 1) != 0, closeN = (flags & 32) != 0;
            for (int i = 0; i < (closeM ? m : m - 1); ++i) for (int j = 0; j < (closeN ? n : n - 1); ++j) {
                const std::size_t a = (std::size_t)(i * n + j), b = (std::size_t)(((i + 1) % m) * n + j), c2 = (std::size_t)(((i + 1) % m) * n + (j + 1) % n), d = (std::size_t)(i * n + (j + 1) % n);
                if (!Tri(*g, map[a], map[b], map[c2]) || !Tri(*g, map[a], map[c2], map[d])) return false;
            }
        }
        return true;
    }

    bool Mesh(const Pairs& v, Container& c) {
        // 92 = vertex count, then (10, 20, 30) per vertex; 93 = size of the face list, then 90 values: n, i1 .. in per face
        std::vector<double> pos;
        std::vector<long long> fl;
        enum { Other, Verts, Faces } state = Other;
        long long nVerts = 0, nFace = 0;
        double cur[3] = {0, 0, 0};
        for (auto& kv : v) {
            double d;
            switch (kv.first) {
            case 92: if (!Num(kv.second, d) || d < 0 || d > (double)kMaxVerts) return Fail("a MESH with an invalid vertex count"); nVerts = (long long)d; state = Verts; break;
            case 93: if (!Num(kv.second, d) || d < 0 || d > 4.0 * kMaxTris) return Fail("a MESH with an invalid face list size"); nFace = (long long)d; state = Faces; break;
            case 94: case 95: case 140: state = Other; break;
            case 10: case 20: case 30:
                if (state == Verts && (long long)(pos.size() / 3) < nVerts) {
                    if (!Num(kv.second, d)) return Fail("an invalid coordinate in a MESH");
                    cur[kv.first / 10 - 1] = d;
                    if (kv.first == 30) { pos.push_back(cur[0]); pos.push_back(cur[1]); pos.push_back(cur[2]); }
                }
                break;
            case 90:
                if (state == Faces && (long long)fl.size() < nFace) { if (!Num(kv.second, d) || d < 0 || d > 2147483647.0) return Fail("an invalid index in a MESH"); fl.push_back((long long)d); }
                break;
            default: break;
            }
        }
        if (pos.empty() || fl.empty()) return true;
        if ((long long)(pos.size() / 3) != nVerts || (long long)fl.size() != nFace) ++truncatedMesh;
        Geo* g = Layer(c, LayerOf(v));
        if (!g) return false;
        if (totalVerts + pos.size() / 3 > kMaxVerts) return Fail("too many vertices");
        const std::uint32_t base = (std::uint32_t)(g->pos.size() / 3);
        g->pos.insert(g->pos.end(), pos.begin(), pos.end());
        totalVerts += pos.size() / 3;
        std::vector<Vec3> poly;
        std::vector<std::array<int, 3>> tris;
        for (std::size_t i = 0; i < fl.size();) {
            const long long cnt = fl[i++];
            if (cnt < 3 || (std::size_t)cnt > fl.size() - i) { ++badFaces; break; }
            bool ok = true;
            poly.clear();
            for (long long k = 0; k < cnt; ++k) {
                const long long ix = fl[i + (std::size_t)k];
                if (ix >= (long long)(pos.size() / 3)) { ok = false; break; }
                poly.push_back(Vec3{(float)pos[(std::size_t)ix * 3], (float)pos[(std::size_t)ix * 3 + 1], (float)pos[(std::size_t)ix * 3 + 2]});
            }
            if (!ok) { ++badFaces; i += (std::size_t)cnt; continue; }
            tris.clear();
            detail::TriangulatePolygon(poly, tris);
            for (const auto& t : tris) if (!Tri(*g, base + (std::uint32_t)fl[i + (std::size_t)t[0]], base + (std::uint32_t)fl[i + (std::size_t)t[1]], base + (std::uint32_t)fl[i + (std::size_t)t[2]])) return false;
            i += (std::size_t)cnt;
        }
        return true;
    }

    bool InsertEnt(const Pairs& v, Container& c) {
        Insert ins; ins.layer = LayerOf(v);
        double ext[3] = {0, 0, 1};
        for (auto& kv : v) {
            double d;
            switch (kv.first) {
            case 2: ins.block = std::string(kv.second); break;
            case 10: case 20: case 30: if (!Num(kv.second, d)) return Fail("an invalid INSERT position"); ins.p[kv.first / 10 - 1] = d; break;
            case 41: case 42: case 43: if (!Num(kv.second, d)) return Fail("an invalid INSERT scale"); ins.s[kv.first - 41] = d; break;
            case 50: if (!Num(kv.second, d)) return Fail("an invalid INSERT rotation"); ins.rot = d; break;
            case 70: case 71: if (Num(kv.second, d) && d > 1) ++arrays; break;
            case 210: case 220: case 230: if (Num(kv.second, d)) ext[(kv.first - 210) / 10] = d; break;
            default: break;
            }
        }
        if (ext[0] != 0 || ext[1] != 0 || ext[2] != 1) ++extrusion;
        if (++inserts > kMaxInserts) return Fail("too many INSERT entities");
        c.inserts.push_back(ins);
        return true;
    }

    bool Entity(const std::string& type, Container& c) {
        if (++entities > kMaxEntities) return Fail("too many entities");
        Pairs v;
        if (!ReadPairs(v)) return false;
        for (auto& kv : v) if (kv.first == 62 || kv.first == 420) { ++colors; break; }
        if (type == "3DFACE") return Face3d(v, c);
        if (type == "POLYLINE") return Polyline(v, c);
        if (type == "MESH") return Mesh(v, c);
        if (type == "INSERT") return InsertEnt(v, c);
        ++skipped[type];
        return true;
    }

    // entities up to the 0-code named `stop` (ENDSEC or ENDBLK)
    bool Entities(Container& c, const char* stop) {
        int code; std::string_view val;
        while (Next(code, val)) {
            if (code != 0) continue;
            if (val == stop) return true;
            if (val == "ENDSEC" || val == "EOF") { Push(code, val); return true; }
            if (!Entity(std::string(val), c)) return false;
        }
        return err.empty();
    }

    bool Parse(const std::string& text) {
        p = text.data(); end = p + text.size();
        if (text.compare(0, 18, "AutoCAD Binary DXF") == 0) return Fail("binary DXF files are not supported (save as ASCII DXF)");
        int code; std::string_view val;
        bool sawSection = false;
        while (Next(code, val)) {
            if (code == 0 && val == "EOF") break;
            if (code != 0 || val != "SECTION") continue;
            sawSection = true;
            int c2; std::string_view name;
            if (!Next(c2, name) || c2 != 2) return Fail("a SECTION without a name");
            if (name == "ENTITIES") { if (!Entities(top, "ENDSEC")) return false; }
            else if (name == "BLOCKS") {
                int c3; std::string_view v3;
                while (Next(c3, v3)) {
                    if (c3 != 0) continue;
                    if (v3 == "ENDSEC") break;
                    if (v3 != "BLOCK") { Push(c3, v3); Container junk; if (!Entities(junk, "ENDSEC")) return false; Push(0, "ENDSEC"); continue; }
                    Pairs hv;
                    if (!ReadPairs(hv)) return false;
                    std::string bname; Container cont;
                    for (auto& kv : hv) { double d; if (kv.first == 2 && bname.empty()) bname = std::string(kv.second); else if (kv.first >= 10 && kv.first <= 30 && kv.first % 10 == 0 && Num(kv.second, d)) cont.base[kv.first / 10 - 1] = d; }
                    if (blocks.size() >= kMaxBlocks) return Fail("too many blocks");
                    if (!Entities(cont, "ENDBLK")) return false;
                    blocks[bname] = std::move(cont);
                }
            } else {
                int c3; std::string_view v3;
                while (Next(c3, v3)) if (c3 == 0 && v3 == "ENDSEC") break;
            }
            if (!err.empty()) return false;
        }
        if (!err.empty()) return false;
        if (!sawSection) return Fail("this is not an ASCII DXF file (no SECTION found)");
        return true;
    }

    // ---- expansion into world geometry ----
    std::map<std::string, rc::Soup> soups;
    bool AddGeo(const Geo& g, const std::string& layer, const M4& xf, double* base) {
        if (g.idx.empty()) return true;
        const bool mirror = rc::Det3(xf) < 0;
        rc::Soup& s = soups[layer];
        if (soups.size() > kMaxLayers) return Fail("too many layers");
        const std::uint32_t off = (std::uint32_t)s.pos.size();
        const std::size_t np = g.pos.size() / 3;
        for (std::size_t i = 0; i < np; ++i) {
            double x, y, z;
            rc::Apply(xf, g.pos[i * 3] - base[0], g.pos[i * 3 + 1] - base[1], g.pos[i * 3 + 2] - base[2], x, y, z);
            if (!std::isfinite(x + y + z) || std::fabs(x) > rc::kMaxCoord || std::fabs(y) > rc::kMaxCoord || std::fabs(z) > rc::kMaxCoord) return Fail("coordinates overflow");
            s.pos.push_back(Vec3{(float)x, (float)y, (float)z});
        }
        outTris += g.idx.size() / 3;
        if (outTris > kMaxTris) return Fail("too many triangles");
        for (std::size_t t = 0; t + 2 < g.idx.size(); t += 3) {
            s.ci.push_back(off + g.idx[t]); s.ci.push_back(off + g.idx[t + (mirror ? 2 : 1)]); s.ci.push_back(off + g.idx[t + (mirror ? 1 : 2)]);
        }
        return true;
    }
    bool Expand(const Container& c, const M4& xf, const std::string& inheritLayer, int depth, double* base) {
        for (const auto& kv : c.byLayer) if (!AddGeo(kv.second, kv.first == "0" && !inheritLayer.empty() ? inheritLayer : kv.first, xf, base)) return false;
        for (const Insert& in : c.inserts) {
            auto b = blocks.find(in.block);
            if (b == blocks.end()) { ++missingBlocks; continue; }
            if (depth + 1 > kMaxNest) { ++deepInserts; continue; }
            if (++expanded > kMaxExpanded) return Fail("too many block instances (the INSERTs expand to more than 200,000 placements)");
            const double kRad = 3.14159265358979323846 / 180.0;
            M4 m = rc::Mul(rc::Translate(in.p[0], in.p[1], in.p[2]), rc::Mul(rc::AxisAngle(0, 0, 1, in.rot * kRad), rc::ScaleM(in.s[0], in.s[1], in.s[2])));
            if (!rc::Finite(m)) return Fail("an INSERT transform overflows");
            double nb[3] = {b->second.base[0], b->second.base[1], b->second.base[2]};
            if (!Expand(b->second, rc::Mul(xf, m), in.layer, depth + 1, nb)) return false;
        }
        return true;
    }

    bool Run(const std::string& text, ImportedModel& model, bool flatten) {
        out = &model;
        if (text.size() > kMaxFileBytes) return Fail("file too large");
        if (!Parse(text)) return false;
        double zero[3] = {0, 0, 0};
        if (!Expand(top, M4{}, "", 0, zero)) return false;
        ImportedNode root; root.name = "RootNode";
        model.nodes.push_back(root);
        for (auto& kv : soups) {
            ImportedPart part;
            rc::BuildStats bs;
            if (!rc::BuildMesh(kv.second, kCrease, part.mesh, &bs)) return Fail("too many output vertices");
            degenerate += bs.degenerate;
            if (part.mesh.triangles.empty()) continue;
            ImportedNode nd; nd.name = kv.first; nd.parent = 0;
            part.node = (int)model.nodes.size();
            model.nodes.push_back(nd);
            if (!rc::PartInRange(part)) return Fail("coordinates overflow");
            model.parts.push_back(std::move(part));
        }
        if (model.parts.empty()) {
            std::string why = "the file has no surface geometry (3DFACE, polyface or polygon mesh POLYLINE, MESH)";
            if (!skipped.empty()) { why += "; it has"; int c = 0; for (auto& kv : skipped) { if (c++ >= 6) break; why += " " + kv.first; } }
            return Fail(why);
        }
        rc::ApplyUpAxis(model, true, flatten);
        auto note = [&](const std::string& s) { model.notes.push_back("DXF: " + s); };
        if (!skipped.empty()) {
            std::string l; int c = 0;
            for (auto& kv : skipped) { if (c++ >= 14) { l += ", ..."; break; } if (!l.empty()) l += ", "; l += kv.first + " x" + std::to_string(kv.second); }
            note("entities not read: " + l);
        }
        if (colors) note(std::to_string(colors) + " entity colour(s) not read (layers become nodes, there are no materials)");
        if (arrays) note(std::to_string(arrays) + " INSERT array(s): only the first copy is placed");
        if (extrusion) note(std::to_string(extrusion) + " INSERT(s) with an extrusion direction other than +Z: the direction is ignored");
        if (missingBlocks) note(std::to_string(missingBlocks) + " INSERT(s) of a block that is not defined skipped");
        if (deepInserts) note(std::to_string(deepInserts) + " INSERT(s) nested deeper than " + std::to_string(kMaxNest) + " skipped");
        if (badFaces) note(std::to_string(badFaces) + " face(s) with fewer than 3 corners or an out-of-range vertex number skipped");
        if (truncatedMesh) note(std::to_string(truncatedMesh) + " mesh(es) whose vertex or face counts do not match the data");
        if (badNumbers) note(std::to_string(badNumbers) + " value(s) that are not numbers were treated as missing");
        if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        return true;
    }
};

} // namespace dxf
} // namespace

bool ReadDxfBuffer(const std::string& text, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    dxf::Reader r;
    if (!r.Run(text, out, flatten)) {
        const std::string m = r.err.empty() ? "DXF: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool ReadDxfFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    bool big = false;
    std::string bytes = detail::rc::ReadAllBounded(fp, dxf::kMaxFileBytes, &big);
    std::fclose(fp);
    if (big) { if (err) *err = "file too large"; return false; }
    return ReadDxfBuffer(bytes, out, err, flatten);
}

} // namespace okay
