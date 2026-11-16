#include "okay/Modeling/Guided.hpp"
#include <chrono>
#include "okay/Modeling/PolyOps.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay::modeling::guided {

// =====================================================================================================
// Shape helpers
// =====================================================================================================
namespace shapes {

static thread_local float g_detail = 1.0f;
int Detail() { return (int)std::lround(g_detail); }
float DetailF() { return g_detail; }
int Scale(int base, int minimum) {
    const float mult = 0.5f * std::exp2(std::min(std::max(g_detail, 0.0f), 3.0f));   // 0.5, 1, 2, 4 at the whole levels
    return std::max(minimum, (int)std::lround((float)base * mult));
}

static bool RightHanded(const Vec3& a, const Vec3& b, const Vec3& c) { return Vec3::Dot(Vec3::Cross(a, b), c) >= 0.0f; }

void Append(PolyMesh& dst, const PolyMesh& src) {
    const int base = (int)dst.verts.size();
    dst.verts.insert(dst.verts.end(), src.verts.begin(), src.verts.end());
    for (PolyFace f : src.faces) {
        for (int& v : f.verts) v += base;
        dst.faces.push_back(std::move(f));
    }
    for (const auto& s : src.seams) dst.SetSeam(s.first + base, s.second + base, true);
}

void Paint(PolyMesh& m, int material, const Color& color) {
    for (PolyFace& f : m.faces) { f.material = material; f.hasColor = true; f.color = color; }
}

void OrientOutward(PolyMesh& m, const Vec3& centre) {
    for (int i = 0; i < (int)m.faces.size(); ++i) {
        PolyFace& f = m.faces[i];
        Vec3 c{0, 0, 0};
        for (int v : f.verts) c = c + m.verts[v];
        c = c * (1.0f / (float)f.verts.size());
        if (Vec3::Dot(m.FaceNormal(i), c - centre) < 0.0f) {
            std::reverse(f.verts.begin(), f.verts.end());
            std::reverse(f.uvs.begin(), f.uvs.end());
        }
    }
}

// A closed solid: `profile` (counter-clockwise in the a,b plane) pushed along `nrm` from -depth/2 to +depth/2.
// Positions are o + a*pa + b*pb + nrm*c.
PolyMesh ExtrudeAxes(const Vec3& o, const Vec3& a, const Vec3& b, const Vec3& nrm, const std::vector<Vec2>& prof, float depth) {
    PolyMesh m;
    const int n = (int)prof.size();
    if (n < 3) return m;
    const bool right = RightHanded(a, b, nrm);
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < n; ++i) m.verts.push_back(o + a * prof[i].x + b * prof[i].y + nrm * ((side ? 0.5f : -0.5f) * depth));
    auto add = [&](std::vector<int> v) {
        if (!right) std::reverse(v.begin(), v.end());
        PolyFace f; f.verts = std::move(v);
        m.faces.push_back(std::move(f));
    };
    std::vector<int> front, back;
    for (int i = 0; i < n; ++i) { front.push_back(n + i); back.push_back(n - 1 - i); }
    add(front);
    add(back);
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        add({i, j, n + j, n + i});
    }
    return m;
}

PolyMesh ExtrudeProfile(const Frame& f, const std::vector<Vec2>& profile) {
    // Profile corners are in 0..1 of (x across the frame, y up): map them to the frame's box.
    std::vector<Vec2> p;
    for (const Vec2& q : profile) p.push_back({(q.x - 0.5f) * f.sx, q.y * f.sy});
    return ExtrudeAxes(f.origin, f.t, f.u, f.n, p, f.sz);
}

PolyMesh Box(const Frame& f) {
    if (g_detail < 1.5f) return ExtrudeProfile(f, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    // High: the four long edges are chamfered; Ultra: rounded with three steps per corner.
    const float c = std::min(0.03f, 0.3f * std::min(f.sx, f.sy));
    const float ux = c / f.sx, uy = c / f.sy;
    const int k = g_detail < 2.5f ? 1 : 3;
    const float cxs[4] = {1 - ux, 1 - ux, ux, ux}, cys[4] = {uy, 1 - uy, 1 - uy, uy};
    std::vector<Vec2> p;
    for (int corner = 0; corner < 4; ++corner)
        for (int i = 0; i <= k; ++i) {
            const float a = (-90.0f + 90.0f * (float)corner + 90.0f * (float)i / (float)k) * 3.14159265f / 180.0f;
            p.push_back({cxs[corner] + std::cos(a) * ux, cys[corner] + std::sin(a) * uy});
        }
    return ExtrudeProfile(f, p);
}

PolyMesh Prism(const Frame& f, int sides) {
    sides = Scale(sides, 3);
    std::vector<Vec2> p;
    for (int i = 0; i < sides; ++i) {
        const float a = 6.2831853f * (float)i / (float)sides;
        p.push_back({std::cos(a) * 0.5f * f.sz, std::sin(a) * 0.5f * f.sx});   // (n, t) plane
    }
    return ExtrudeAxes(f.origin + f.u * (0.5f * f.sy), f.n, f.t, f.u, p, f.sy);
}

PolyMesh Pyramid(const Frame& f, int sides) {
    sides = sides > 4 ? Scale(sides, 4) : std::max(3, sides);
    PolyMesh m;
    for (int i = 0; i < sides; ++i) {
        const float a = 6.2831853f * (float)i / (float)sides;
        m.verts.push_back(f.origin + f.n * (std::cos(a) * 0.5f * f.sz) + f.t * (std::sin(a) * 0.5f * f.sx));
    }
    m.verts.push_back(f.origin + f.u * f.sy);
    PolyFace base;
    for (int i = 0; i < sides; ++i) base.verts.push_back(i);
    m.faces.push_back(base);
    for (int i = 0; i < sides; ++i) { PolyFace t; t.verts = {i, (i + 1) % sides, sides}; m.faces.push_back(t); }
    OrientOutward(m, f.origin + f.u * (0.25f * f.sy));
    return m;
}

PolyMesh Ball(const Frame& f, int rings, int sectors) {
    rings = Scale(rings, 2);
    sectors = Scale(sectors, 4);
    PolyMesh m;
    const Vec3 c = f.origin + f.u * (0.5f * f.sy);
    m.verts.push_back(c + f.u * (0.5f * f.sy));   // top
    for (int r = 1; r < rings; ++r) {
        const float pol = 3.14159265f * (float)r / (float)rings;
        for (int s = 0; s < sectors; ++s) {
            const float az = 6.2831853f * (float)s / (float)sectors;
            m.verts.push_back(c + f.u * (std::cos(pol) * 0.5f * f.sy) + f.t * (std::sin(pol) * std::sin(az) * 0.5f * f.sx) +
                              f.n * (std::sin(pol) * std::cos(az) * 0.5f * f.sz));
        }
    }
    m.verts.push_back(c - f.u * (0.5f * f.sy));   // bottom
    const int bottom = (int)m.verts.size() - 1;
    auto ring = [&](int r, int s) { return 1 + (r - 1) * sectors + (s % sectors); };
    for (int s = 0; s < sectors; ++s) { PolyFace t; t.verts = {0, ring(1, s), ring(1, s + 1)}; m.faces.push_back(t); }
    for (int r = 1; r + 1 < rings; ++r)
        for (int s = 0; s < sectors; ++s) { PolyFace q; q.verts = {ring(r, s), ring(r + 1, s), ring(r + 1, s + 1), ring(r, s + 1)}; m.faces.push_back(q); }
    for (int s = 0; s < sectors; ++s) { PolyFace t; t.verts = {bottom, ring(rings - 1, s + 1), ring(rings - 1, s)}; m.faces.push_back(t); }
    OrientOutward(m, c);
    return m;
}

} // namespace shapes

// =====================================================================================================
// Small helpers
// =====================================================================================================
namespace {

std::string Num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.17g", v); return b; }
std::string Flt(float v) { char b[40]; std::snprintf(b, sizeof b, "%.9g", (double)v); return b; }
std::string Clean(const std::string& s) {
    std::string o;
    for (char c : s) o += (c == '\n' || c == '\r') ? ' ' : c;
    return o;
}
std::string Rest(std::istringstream& ls) {
    std::string r;
    std::getline(ls, r);
    while (!r.empty() && (r.front() == ' ' || r.front() == '\t')) r.erase(r.begin());
    return r;
}
bool Fail(std::string* e, int line, const std::string& why) {
    if (e) *e = "line " + std::to_string(line) + ": " + why;
    return false;
}

// Planar UVs for faces that have none (projected on the face's dominant axis, one unit per metre).
void AutoUV(PolyMesh& m) {
    for (int i = 0; i < (int)m.faces.size(); ++i) {
        PolyFace& f = m.faces[i];
        if (f.uvs.size() == f.verts.size() && !f.uvs.empty()) continue;
        const Vec3 n = m.FaceNormal(i);
        const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        f.uvs.clear();
        for (int v : f.verts) {
            const Vec3& p = m.verts[v];
            if (ay >= ax && ay >= az) f.uvs.push_back({p.x, p.z});
            else if (ax >= az) f.uvs.push_back({p.z, p.y});
            else f.uvs.push_back({p.x, p.y});
        }
    }
}

} // namespace

// =====================================================================================================
// User parts
// =====================================================================================================
std::string SerializeUserPart(const UserPart& p) {
    std::ostringstream o;
    o << "okaypart 1\nid " << Clean(p.id) << "\nname " << Clean(p.name) << "\nslot " << Clean(p.slot) << "\n" << p.mesh.Serialize();
    return o.str();
}

bool ParseUserPart(const std::string& text, UserPart& out, std::string* error) {
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    UserPart p;
    bool header = false;
    std::string meshText;
    bool inMesh = false;
    while (std::getline(in, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (inMesh) { meshText += line + "\n"; continue; }
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag[0] == '#') continue;
        if (!header) {
            int v = 0;
            if (tag != "okaypart" || !(ls >> v)) return Fail(error, ln, "not an .okaypart file (it should start with \"okaypart 1\")");
            if (v != 1) return Fail(error, ln, "part version " + std::to_string(v) + " is newer than this editor reads (1)");
            header = true;
        } else if (tag == "id") p.id = Rest(ls);
        else if (tag == "name") p.name = Rest(ls);
        else if (tag == "slot") p.slot = Rest(ls);
        else if (tag == "okaymesh") { inMesh = true; meshText = line + "\n"; }
        else return Fail(error, ln, "unknown record '" + tag + "'");
    }
    if (!header) return Fail(error, ln, "empty file");
    if (!inMesh) return Fail(error, ln, "the part has no mesh");
    std::string err;
    if (!PolyMesh::Parse(meshText, p.mesh, &err)) { if (error) *error = "mesh: " + err; return false; }
    if (p.id.empty()) return Fail(error, ln, "the part has no id");
    out = std::move(p);
    return true;
}

bool MakeUserPart(const PolyMesh& mesh, const std::vector<int>& faces, const std::string& name, const std::string& slot,
                  UserPart& out, std::string* error) {
    std::vector<int> used;
    for (int f : faces) if (f >= 0 && f < (int)mesh.faces.size() && mesh.faces[f].verts.size() >= 3) used.push_back(f);
    if (used.empty()) { if (error) *error = "select some faces first"; return false; }
    std::map<int, int> remap;
    UserPart p;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (int f : used)
        for (int v : mesh.faces[f].verts) {
            const Vec3& q = mesh.verts[v];
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
        }
    const Vec3 sz{std::max(hi.x - lo.x, 1e-4f), std::max(hi.y - lo.y, 1e-4f), std::max(hi.z - lo.z, 1e-4f)};
    for (int f : used) {
        PolyFace nf = mesh.faces[f];
        for (int& v : nf.verts) {
            auto it = remap.find(v);
            if (it == remap.end()) {
                const Vec3& q = mesh.verts[v];
                it = remap.emplace(v, (int)p.mesh.verts.size()).first;
                p.mesh.verts.push_back({(q.x - lo.x) / sz.x - 0.5f, (q.y - lo.y) / sz.y, (q.z - lo.z) / sz.z - 0.5f});
            }
            v = it->second;
        }
        nf.uvs.clear();   // UVs are made fresh where the part is used
        p.mesh.faces.push_back(std::move(nf));
    }
    p.name = name.empty() ? "My part" : Clean(name);
    p.slot = slot;
    // A stable id from the name and the shape, so saving twice does not make a second copy.
    std::string id;
    for (char c : p.name) id += std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_';
    unsigned h = 2166136261u;
    for (const Vec3& v : p.mesh.verts) {
        const int q[3] = {(int)std::lround(v.x * 1000), (int)std::lround(v.y * 1000), (int)std::lround(v.z * 1000)};
        for (int k = 0; k < 3; ++k) { h ^= (unsigned)q[k]; h *= 16777619u; }
    }
    char hb[16]; std::snprintf(hb, sizeof hb, "%06x", h & 0xffffff);
    p.id = id + "_" + hb;
    out = std::move(p);
    return true;
}

PolyMesh FitUserPart(const UserPart& p, const Frame& f) {
    PolyMesh m = p.mesh;
    for (Vec3& v : m.verts) v = f.Point(v.x * f.sx, v.y * f.sy, v.z * f.sz);
    if (!shapes::RightHanded(f.t, f.u, f.n))
        for (PolyFace& fc : m.faces) { std::reverse(fc.verts.begin(), fc.verts.end()); std::reverse(fc.uvs.begin(), fc.uvs.end()); }
    return m;
}

// =====================================================================================================
// Recipe
// =====================================================================================================
std::string Recipe::DesignFor(const std::string& slot, const std::string& partId) const {
    auto p = partDesign.find(partId);
    if (p != partDesign.end()) return p->second;
    auto s = slotDesign.find(slot);
    return s == slotDesign.end() ? std::string() : s->second;
}

std::string Recipe::Serialize() const {
    std::ostringstream o;
    o << "okayguided 1\ntemplate " << templateId << " " << templateVersion << "\n";
    for (const auto& p : params) o << "param " << p.first << " " << Num(p.second) << "\n";
    for (const auto& s : slotDesign) o << "slot " << s.first << " " << s.second << "\n";
    for (const auto& s : partDesign) o << "partdesign " << s.first << " " << s.second << "\n";
    for (const auto& c : colors) o << "color " << c.first << " " << Flt(c.second.r) << " " << Flt(c.second.g) << " " << Flt(c.second.b) << " " << Flt(c.second.a) << "\n";
    for (const auto& m : materials) o << "material " << m.first << " " << m.second << "\n";
    for (const Vec3& p : path) o << "path " << Flt(p.x) << " " << Flt(p.y) << " " << Flt(p.z) << "\n";
    for (const auto& d : detached) o << "detach " << d << "\n";
    for (const auto& u : userParts) o << "begin userpart\n" << SerializeUserPart(u.second) << "end userpart\n";
    return o.str();
}

bool Recipe::Parse(const std::string& text, Recipe& out, std::string* error) {
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    Recipe r;
    bool header = false;
    bool inPart = false;
    std::string partText;
    while (std::getline(in, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (inPart) {
            if (line == "end userpart") {
                UserPart up; std::string err;
                if (!ParseUserPart(partText, up, &err)) return Fail(error, ln, "embedded part: " + err);
                r.userParts[up.id] = up;
                inPart = false;
            } else partText += line + "\n";
            continue;
        }
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag[0] == '#') continue;
        if (!header) {
            int v = 0;
            if (tag != "okayguided" || !(ls >> v)) return Fail(error, ln, "not an .okayguided file (it should start with \"okayguided 1\")");
            if (v != 1) return Fail(error, ln, "version " + std::to_string(v) + " is newer than this editor reads (1)");
            header = true;
        } else if (tag == "template") {
            if (!(ls >> r.templateId >> r.templateVersion)) return Fail(error, ln, "template needs an id and a version");
        } else if (tag == "param") {
            std::string id; double v = 0;
            if (!(ls >> id >> v)) return Fail(error, ln, "param needs a name and a number");
            r.params[id] = v;
        } else if (tag == "slot" || tag == "partdesign") {
            std::string a, b;
            if (!(ls >> a >> b)) return Fail(error, ln, tag + " needs two names");
            (tag == "slot" ? r.slotDesign : r.partDesign)[a] = b;
        } else if (tag == "color") {
            std::string s; Color c;
            if (!(ls >> s >> c.r >> c.g >> c.b >> c.a)) return Fail(error, ln, "color needs a slot and r g b a");
            r.colors[s] = c;
        } else if (tag == "material") {
            std::string s; int m = 0;
            if (!(ls >> s >> m)) return Fail(error, ln, "material needs a slot and a number");
            r.materials[s] = m;
        } else if (tag == "path") {
            Vec3 p;
            if (!(ls >> p.x >> p.y >> p.z)) return Fail(error, ln, "path needs x y z");
            r.path.push_back(p);
        } else if (tag == "detach") {
            std::string d;
            if (!(ls >> d)) return Fail(error, ln, "detach needs a part id");
            r.detached.insert(d);
        } else if (tag == "begin") {
            std::string w; ls >> w;
            if (w != "userpart") return Fail(error, ln, "unknown block '" + w + "'");
            inPart = true; partText.clear();
        } else return Fail(error, ln, "unknown record '" + tag + "'");
    }
    if (!header) return Fail(error, ln, "empty file");
    if (inPart) return Fail(error, ln, "an embedded part is not closed");
    if (r.templateId.empty()) return Fail(error, ln, "no template line");
    out = std::move(r);
    return true;
}

// =====================================================================================================
// Registry
// =====================================================================================================
Registry& Registry::Instance() { static Registry r; return r; }
const char* const kDetailParam = "detail";
int TriangleCount(const PolyMesh& m) {
    int n = 0;
    for (const PolyFace& f : m.faces) n += std::max(0, (int)f.verts.size() - 2);
    return n;
}
void Registry::Register(const Template& in) {
    Template t = in;
    bool has = false;
    for (const ParamDef& p : t.params) if (p.id == kDetailParam) has = true;
    if (!has) {   // every template gets the shared Detail setting (Medium = the original shapes)
        ParamDef p; p.id = kDetailParam; p.label = "Detail"; p.group = "Detail"; p.kind = ParamDef::Kind::Float; p.def = 1; p.min = 0; p.max = 3;
        p.tip = "0 to 3 (0 to 100 percent): Low is blocky low poly, Medium rounded, High smooth with fine detail parts, Ultra subdivided and finest.";
        t.params.push_back(p);
    }
    for (Template& e : templates_) if (e.id == t.id) { e = t; return; }
    templates_.push_back(t);
}
const Template* Registry::Find(const std::string& id) const {
    for (const Template& t : templates_) if (t.id == id) return &t;
    return nullptr;
}
std::vector<const Template*> Registry::InCategory(const std::string& c) const {
    std::vector<const Template*> v;
    for (const Template& t : templates_) if (t.category == c) v.push_back(&t);
    return v;
}
const std::vector<std::string>& Registry::Categories() {
    static const std::vector<std::string> c = {"Architecture", "Vehicles", "Characters", "Environment", "Props"};
    return c;
}

Recipe Normalize(const Template& t, const Recipe& in) {
    Recipe r = in;
    r.templateId = t.id;
    for (const ParamDef& p : t.params) {
        double v = r.Param(p.id, p.def);
        if (!(v == v)) v = p.def;   // NaN
        v = std::min(std::max(v, p.min), p.max);
        if (p.kind == ParamDef::Kind::Int || p.kind == ParamDef::Kind::Choice) v = std::floor(v + 0.5);
        if (p.kind == ParamDef::Kind::Bool) v = v >= 0.5 ? 1 : 0;
        r.params[p.id] = v;
    }
    for (const SlotDef& s : t.slots) {
        bool known = false;
        auto it = r.slotDesign.find(s.id);
        if (it != r.slotDesign.end()) {
            for (const DesignDef& d : s.designs) if (d.id == it->second) known = true;
            if (it->second.rfind("user:", 0) == 0 && r.userParts.count(std::string(it->second, 5))) known = true;
        }
        if (!known && !s.designs.empty()) r.slotDesign[s.id] = s.designs.front().id;
        if (!r.colors.count(s.id)) r.colors[s.id] = s.defaultColor;
        if (!r.materials.count(s.id)) r.materials[s.id] = s.defaultMaterial;
    }
    return r;
}

Recipe DefaultRecipe(const Template& t, const std::string& startId) {
    Recipe r;
    r.templateId = t.id;
    r.templateVersion = t.version;
    const StartDesign* st = nullptr;
    for (const StartDesign& s : t.starts) if (startId.empty() ? &s == &t.starts.front() : s.id == startId) st = &s;
    if (st) {
        r.params = st->params;
        r.slotDesign = st->slotDesign;
        r.colors = st->colors;
    }
    return Normalize(t, r);
}

namespace {
thread_local GenStats g_stats;
static thread_local std::map<std::string, PolyMesh> g_cache;
static thread_local long g_cacheFaces = 0;
}

std::vector<GeneratedPart> Generate(const Recipe& in, std::string* error) {
    std::vector<GeneratedPart> out;
    RegisterBuiltinTemplates();
    const Template* t = Registry::Instance().Find(in.templateId);
    if (!t || !t->generate) { if (error) *error = "unknown template '" + in.templateId + "'"; return out; }
    const Recipe r = Normalize(*t, in);
    std::vector<GeneratedPart> all;
    const float saved = shapes::g_detail;
    const float dv = (float)r.Param(kDetailParam, 1);
    shapes::g_detail = std::min(std::max(dv == dv ? dv : 1.0f, 0.0f), 3.0f);
    t->generate(r, all);
    // Ultra: every part is subdivided (Catmull-Clark) one or two times, within a face budget per part, for a truly smooth high-poly surface.
    if (shapes::g_detail >= 2.5f) {
        const int levels = shapes::g_detail >= 2.95f ? 2 : 1;
        // Instancing: parts of the same shape (four wheels, a row of pickets) are subdivided once and the result is moved to
        // each place, instead of being subdivided again every time.
        std::map<std::string, std::pair<PolyMesh, Vec3>> shapesDone;   // key -> subdivided mesh and the corner it was built at
        for (GeneratedPart& p : all) {
            int lv = levels;
            long faces = (long)p.mesh.faces.size();
            while (lv > 0 && faces * (long)(1 << (2 * lv)) > 2500) --lv;
            if (lv <= 0 || p.mesh.faces.empty()) continue;
            Vec3 lo{1e30f, 1e30f, 1e30f};
            for (const Vec3& v : p.mesh.verts) lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            std::string key = std::to_string(lv) + ":";
            for (const Vec3& v : p.mesh.verts) { key += std::to_string(std::llround((double)(v.x - lo.x) * 2e3)) + "," + std::to_string(std::llround((double)(v.y - lo.y) * 2e3)) + "," + std::to_string(std::llround((double)(v.z - lo.z) * 2e3)) + ";"; }
            for (const PolyFace& f : p.mesh.faces) {
                key += '|';
                for (int vi : f.verts) key += std::to_string(vi) + ",";
                key += std::to_string(f.material) + (f.hasColor ? "c" + std::to_string(f.color.r) + "," + std::to_string(f.color.g) + "," + std::to_string(f.color.b) : std::string());
            }
            auto it = shapesDone.find(key);
            if (it != shapesDone.end()) {
                p.mesh = it->second.first;
                const Vec3 shift = lo - it->second.second;
                for (Vec3& v : p.mesh.verts) v = v + shift;
                ++g_stats.instanced;
            } else {
                p.mesh = CatmullClark(p.mesh, lv);
                shapesDone[key] = {p.mesh, lo};
            }
        }
    }
    shapes::g_detail = saved;
    for (GeneratedPart& p : all) {
        if (r.detached.count(p.id) || p.mesh.faces.empty()) continue;
        AutoUV(p.mesh);
        const auto c = r.colors.find(p.slot);
        const auto m = r.materials.find(p.slot);
        shapes::Paint(p.mesh, m == r.materials.end() ? 0 : m->second, c == r.colors.end() ? Color{0.8f, 0.8f, 0.8f, 1} : c->second);
        out.push_back(std::move(p));
    }
    return out;
}

PolyMesh Assemble(const std::vector<GeneratedPart>& parts) {
    PolyMesh m;
    for (const GeneratedPart& p : parts) shapes::Append(m, p.mesh);
    return m;
}

PolyMesh Create(const Recipe& r, std::string* error) {
    // Cached by the recipe's settings: the same recipe again (a panel redraw, a triangle count, an undo back to a known state)
    // is a copy, not a rebuild. Parts of the same shape inside one build are instanced (see Generate).
    const std::string key = r.Serialize();
    auto it = g_cache.find(key);
    if (it != g_cache.end()) { ++g_stats.hits; return it->second; }
    ++g_stats.misses;
    const auto t0 = std::chrono::steady_clock::now();
    PolyMesh m = Assemble(Generate(r, error));
    g_stats.lastBuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!(error && !error->empty()) && !m.faces.empty()) {
        if (g_cacheFaces + (long)m.faces.size() > 1500000) ClearCache();
        g_cacheFaces += (long)m.faces.size();
        g_cache[key] = m;
    }
    return m;
}
const GenStats& Stats() { return g_stats; }
void ClearCache() { g_cache.clear(); g_cacheFaces = 0; }

bool DetachPart(Recipe& r, const std::string& partId) {
    for (const GeneratedPart& p : Generate(r))
        if (p.id == partId) { r.detached.insert(partId); return true; }
    return false;
}

// =====================================================================================================
// Analysis
// =====================================================================================================
namespace {

using Key3 = std::array<long long, 3>;
Key3 QPos(const Vec3& v) {
    return {std::llround((double)v.x * 1e4), std::llround((double)v.y * 1e4), std::llround((double)v.z * 1e4)};
}

// Corner positions rotated so the smallest comes first; `rot` receives how far it was rotated.
std::vector<Key3> FaceSig(const PolyMesh& m, const PolyFace& f, int* rot) {
    std::vector<Key3> k;
    for (int v : f.verts) k.push_back(QPos(m.verts[v]));
    int best = 0;
    for (int i = 1; i < (int)k.size(); ++i) if (k[i] < k[best]) best = i;
    std::rotate(k.begin(), k.begin() + best, k.end());
    if (rot) *rot = best;
    return k;
}

bool SameAttrs(const PolyMesh& a, const PolyFace& fa, int ra, const PolyMesh& b, const PolyFace& fb, int rb) {
    (void)a; (void)b;
    const int n = (int)fa.verts.size();
    if (fa.uvs.size() == (size_t)n && fb.uvs.size() == (size_t)n)
        for (int i = 0; i < n; ++i) {
            const Vec2& u = fa.uvs[(ra + i) % n];
            const Vec2& w = fb.uvs[(rb + i) % n];
            if (std::fabs(u.x - w.x) > 1e-4f || std::fabs(u.y - w.y) > 1e-4f) return false;
        }
    if (fa.hasColor && fb.hasColor &&
        (std::fabs(fa.color.r - fb.color.r) > 1e-4f || std::fabs(fa.color.g - fb.color.g) > 1e-4f ||
         std::fabs(fa.color.b - fb.color.b) > 1e-4f || std::fabs(fa.color.a - fb.color.a) > 1e-4f)) return false;
    return true;
}

struct DSU {
    std::vector<int> p;
    explicit DSU(int n) : p(n) { for (int i = 0; i < n; ++i) p[i] = i; }
    int Find(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
    void Union(int a, int b) { a = Find(a); b = Find(b); if (a != b) p[a] = b; }
};

// Rigid-shape key of a set of faces: sorted (corner count, perimeter, area).
std::string ShapeKey(const PolyMesh& m, const std::vector<int>& faces) {
    std::vector<std::array<long long, 3>> v;
    for (int fi : faces) {
        const PolyFace& f = m.faces[fi];
        double per = 0;
        for (size_t i = 0; i < f.verts.size(); ++i) per += (m.verts[f.verts[i]] - m.verts[f.verts[(i + 1) % f.verts.size()]]).Magnitude();
        v.push_back({(long long)f.verts.size(), std::llround(per * 1e3), 0LL});
        // area via Newell (normal magnitude is not area; compute directly)
        double ax = 0, ay = 0, az = 0;
        for (size_t i = 0; i < f.verts.size(); ++i) {
            const Vec3& p = m.verts[f.verts[i]];
            const Vec3& q = m.verts[f.verts[(i + 1) % f.verts.size()]];
            ax += (double)(p.y - q.y) * (p.z + q.z);
            ay += (double)(p.z - q.z) * (p.x + q.x);
            az += (double)(p.x - q.x) * (p.y + q.y);
        }
        v.back()[2] = std::llround(0.5 * std::sqrt(ax * ax + ay * ay + az * az) * 1e3);
    }
    std::sort(v.begin(), v.end());
    std::string s;
    for (const auto& t : v) s += std::to_string(t[0]) + ":" + std::to_string(t[1]) + ":" + std::to_string(t[2]) + ";";
    return s;
}

Vec3 Centroid(const PolyMesh& m, const std::vector<int>& faces) {
    Vec3 c{0, 0, 0};
    int n = 0;
    for (int fi : faces) for (int v : m.faces[fi].verts) { c = c + m.verts[v]; ++n; }
    return n ? c * (1.0f / (float)n) : c;
}

struct Detail {
    Analysis a;
    std::vector<GeneratedPart> baseline;
    std::vector<int> faceComp;                 // per current face
    int comps = 0;
    std::vector<std::set<int>> compParts;      // per component: baseline part indices that own it
    std::vector<std::vector<int>> compFaces;
};

Detail AnalyzeImpl(const PolyMesh& cur, const Recipe& stored) {
    Detail d;
    Analysis& A = d.a;
    RegisterBuiltinTemplates();
    const Template* t = Registry::Instance().Find(stored.templateId);
    if (!t) { A.error = "unknown template '" + stored.templateId + "'"; return d; }
    if (stored.templateVersion != t->version) {
        A.error = "this model was made with version " + std::to_string(stored.templateVersion) + " of " + t->name +
                  "; this editor has version " + std::to_string(t->version) + ", so its parts cannot be checked";
        return d;
    }
    std::string gerr;
    d.baseline = Generate(stored, &gerr);
    const int np = (int)d.baseline.size();
    const int nf = (int)cur.faces.size();

    // Match faces by position signature.
    std::map<std::vector<Key3>, std::vector<std::pair<int, int>>> sigs;   // sig -> (part, face)
    std::vector<int> baseCount(np, 0);
    for (int p = 0; p < np; ++p) {
        const PolyMesh& pm = d.baseline[p].mesh;
        baseCount[p] = (int)pm.faces.size();
        for (int f = 0; f < (int)pm.faces.size(); ++f) sigs[FaceSig(pm, pm.faces[f], nullptr)].push_back({p, f});
    }
    std::vector<int> faceMatch(nf, -1);       // part index
    std::vector<char> pristine(nf, 0);
    std::vector<int> matched(np, 0), pristineCount(np, 0);
    std::map<std::pair<int, int>, bool> used;
    for (int f = 0; f < nf; ++f) {
        int rc = 0;
        const std::vector<Key3> sig = FaceSig(cur, cur.faces[f], &rc);
        auto it = sigs.find(sig);
        if (it == sigs.end()) continue;
        for (const auto& pf : it->second) {
            if (used[pf]) continue;
            used[pf] = true;
            faceMatch[f] = pf.first;
            const PolyMesh& pm = d.baseline[pf.first].mesh;
            int rb = 0;
            FaceSig(pm, pm.faces[pf.second], &rb);
            pristine[f] = SameAttrs(cur, cur.faces[f], rc, pm, pm.faces[pf.second], rb) ? 1 : 0;
            ++matched[pf.first];
            if (pristine[f]) ++pristineCount[pf.first];
            break;
        }
    }

    // Pieces: faces that share a corner position are one piece.
    DSU dsu(std::max(nf, 1));
    std::map<Key3, int> firstFace;
    for (int f = 0; f < nf; ++f)
        for (int v : cur.faces[f].verts) {
            auto r = firstFace.emplace(QPos(cur.verts[v]), f);
            if (!r.second) dsu.Union(f, r.first->second);
        }
    std::map<int, int> compId;
    d.faceComp.assign(nf, -1);
    for (int f = 0; f < nf; ++f) {
        const int root = dsu.Find(f);
        auto it = compId.emplace(root, (int)compId.size()).first;
        d.faceComp[f] = it->second;
    }
    d.comps = (int)compId.size();
    d.compFaces.assign(d.comps, {});
    d.compParts.assign(d.comps, {});
    for (int f = 0; f < nf; ++f) d.compFaces[d.faceComp[f]].push_back(f);
    std::vector<char> compClean(d.comps, 1);
    std::vector<std::set<int>> compsOf(np);
    for (int f = 0; f < nf; ++f) {
        if (!pristine[f]) compClean[d.faceComp[f]] = 0;
        if (faceMatch[f] >= 0) compsOf[faceMatch[f]].insert(d.faceComp[f]);
    }

    A.parts.assign(np, PartInfo());
    std::vector<int> owner(np, -2);   // for moved parts: nothing; used below
    std::vector<char> compClaimed(d.comps, 0);
    for (int p = 0; p < np; ++p) {
        PartInfo& pi = A.parts[p];
        pi.id = d.baseline[p].id;
        pi.slot = d.baseline[p].slot;
        bool clean = true;
        for (int c : compsOf[p]) { if (!compClean[c]) clean = false; }
        if (matched[p] == 0) {
            pi.state = PartState::Deleted;
            pi.why = "all of its faces are gone or have moved";
        } else if (pristineCount[p] == baseCount[p] && matched[p] == baseCount[p] && clean) {
            pi.state = PartState::Intact;
        } else {
            pi.state = PartState::Edited;
            const int missing = baseCount[p] - pristineCount[p];
            pi.why = missing > 0 ? std::to_string(missing) + " of its " + std::to_string(baseCount[p]) + " faces changed or removed"
                                 : "other geometry is attached to it";
        }
        for (int c : compsOf[p]) d.compParts[c].insert(p);
    }

    // Moved whole parts: pieces with no matched face that have the same rigid shape as a part with none left. A part made
    // of one shell may have been turned as well; a part of several shells (a window: frame, bars, glass) is recognised
    // when all its shells were moved by the same offset.
    struct Shell { std::string key; Vec3 centre; };
    std::vector<std::string> compKey(d.comps);
    std::vector<Vec3> compCentre(d.comps);
    std::vector<char> free(d.comps, 0);
    for (int c = 0; c < d.comps; ++c) {
        bool anyMatch = false;
        for (int f : d.compFaces[c]) if (faceMatch[f] >= 0) anyMatch = true;
        if (anyMatch) continue;
        free[c] = 1;
        compKey[c] = ShapeKey(cur, d.compFaces[c]);
        compCentre[c] = Centroid(cur, d.compFaces[c]);
    }
    for (int p = 0; p < np; ++p) {
        if (A.parts[p].state != PartState::Deleted) continue;
        const PolyMesh& pm = d.baseline[p].mesh;
        DSU bd((int)pm.faces.size());
        std::map<Key3, int> ff;
        for (int f = 0; f < (int)pm.faces.size(); ++f)
            for (int v : pm.faces[f].verts) { auto r = ff.emplace(QPos(pm.verts[v]), f); if (!r.second) bd.Union(f, r.first->second); }
        std::map<int, std::vector<int>> groups;
        for (int f = 0; f < (int)pm.faces.size(); ++f) groups[bd.Find(f)].push_back(f);
        std::vector<Shell> shells;
        for (const auto& g : groups) shells.push_back({ShapeKey(pm, g.second), Centroid(pm, g.second)});
        std::vector<int> claim;
        if (shells.size() == 1) {
            int best = -1; float bdist = 1e30f;
            for (int c = 0; c < d.comps; ++c)
                if (free[c] && compKey[c] == shells[0].key) {
                    const float dist = (compCentre[c] - shells[0].centre).Magnitude();
                    if (dist < bdist) { bdist = dist; best = c; }
                }
            if (best >= 0) claim.push_back(best);
        } else {
            for (int c0 = 0; c0 < d.comps && claim.empty(); ++c0) {
                if (!free[c0] || compKey[c0] != shells[0].key) continue;
                const Vec3 off = compCentre[c0] - shells[0].centre;
                std::vector<int> got = {c0};
                for (size_t k = 1; k < shells.size(); ++k) {
                    int found = -1;
                    for (int c = 0; c < d.comps && found < 0; ++c)
                        if (free[c] && compKey[c] == shells[k].key && std::find(got.begin(), got.end(), c) == got.end() &&
                            (compCentre[c] - shells[k].centre - off).Magnitude() < 0.01f) found = c;
                    if (found < 0) break;
                    got.push_back(found);
                }
                if (got.size() == shells.size()) claim = got;
            }
        }
        if (claim.empty()) continue;
        for (int c : claim) { free[c] = 0; d.compParts[c].insert(p); }
        A.parts[p].state = PartState::Edited;
        A.parts[p].why = "it was moved (its shape is unchanged)";
    }

    A.faceOwner.assign(nf, "");
    for (int c = 0; c < d.comps; ++c) {
        if (d.compParts[c].empty()) continue;
        for (int f : d.compFaces[c]) {
            const int p = faceMatch[f] >= 0 ? faceMatch[f] : *d.compParts[c].begin();
            A.faceOwner[f] = d.baseline[p].id;
        }
    }
    for (const std::string& o : A.faceOwner) if (o.empty()) ++A.customFaces;
    A.ok = true;
    return d;
}

// Do two parts have the same faces (positions, UVs, colours)?
bool SameGeometry(const PolyMesh& a, const PolyMesh& b) {
    if (a.faces.size() != b.faces.size()) return false;
    std::map<std::vector<Key3>, int> m;
    for (const PolyFace& f : a.faces) ++m[FaceSig(a, f, nullptr)];
    for (const PolyFace& f : b.faces) {
        auto it = m.find(FaceSig(b, f, nullptr));
        if (it == m.end() || it->second == 0) return false;
        --it->second;
    }
    return true;
}

} // namespace

Analysis Analyze(const PolyMesh& current, const Recipe& stored) { return AnalyzeImpl(current, stored).a; }

RegenResult Regenerate(const PolyMesh& current, const Recipe& stored, const Recipe& next, const RegenOptions& opt) {
    RegenResult R;
    R.mesh = current;
    R.recipe = stored;
    Detail d = AnalyzeImpl(current, stored);
    if (!d.a.ok) { R.message = d.a.error + ". Convert it to a regular mesh to keep working on it by hand."; return R; }
    const Template* t = Registry::Instance().Find(stored.templateId);
    Recipe nx = Normalize(*t, next);
    nx.templateVersion = t->version;
    nx.detached = stored.detached;   // the stored list is the base; Keep decisions add to it below
    std::string err;
    std::vector<GeneratedPart> fresh = Generate(nx, &err);
    std::map<std::string, const GeneratedPart*> freshById;
    for (const GeneratedPart& p : fresh) freshById[p.id] = &p;
    std::map<std::string, int> baseIdx;
    for (int i = 0; i < (int)d.baseline.size(); ++i) baseIdx[d.baseline[i].id] = i;

    // Which edited / deleted parts does the change actually touch?
    const int np = (int)d.baseline.size();
    std::vector<Decision> dec(np, Decision::Ask);
    std::vector<char> affected(np, 0);
    for (int p = 0; p < np; ++p) {
        const PartInfo& pi = d.a.parts[p];
        if (pi.state == PartState::Intact) continue;
        auto f = freshById.find(pi.id);
        if (pi.state == PartState::Deleted && f == freshById.end()) continue;               // stays gone
        if (f != freshById.end() && SameGeometry(f->second->mesh, d.baseline[p].mesh)) continue;   // the change does not touch it
        affected[p] = 1;
        auto it = opt.perPart.find(pi.id);
        dec[p] = it != opt.perPart.end() ? it->second : opt.all;
        if (dec[p] == Decision::Ask) { R.conflicts.push_back(pi); }
    }
    if (!R.conflicts.empty()) {
        R.needsDecision = true;
        R.message = std::to_string(R.conflicts.size()) + " part(s) you edited by hand would be changed by this. Nothing was changed. "
                    "Choose: keep your edits (the part stops following the design), discard your edits, or convert to a regular mesh.";
        return R;
    }

    // Decide what happens to each piece of the current mesh.
    std::vector<char> keepComp(d.comps, 0);
    std::set<std::string> preserved;   // part ids whose current geometry stays and is not generated again
    for (int p = 0; p < np; ++p) {
        const PartInfo& pi = d.a.parts[p];
        if (pi.state == PartState::Intact) continue;
        if (!affected[p]) { preserved.insert(pi.id); ++R.kept; }
        else if (dec[p] == Decision::Keep) { nx.detached.insert(pi.id); ++R.kept; }
        else ++R.discarded;
    }
    for (int c = 0; c < d.comps; ++c) {
        if (d.compParts[c].empty()) { keepComp[c] = 1; continue; }   // custom: always kept
        bool anyEdited = false, keep = false;
        for (int p : d.compParts[c]) {
            if (d.a.parts[p].state == PartState::Intact) continue;
            anyEdited = true;
            if (!affected[p] || dec[p] == Decision::Keep) keep = true;   // Keep wins
        }
        keepComp[c] = anyEdited && keep ? 1 : 0;
    }
    // Detached ids from Keep are not generated.
    if (nx.detached != stored.detached) { fresh = Generate(nx, &err); }

    PolyMesh out;
    std::map<int, int> remap;
    auto mapVert = [&](int v) {
        auto it = remap.find(v);
        if (it != remap.end()) return it->second;
        const int id = (int)out.verts.size();
        out.verts.push_back(current.verts[v]);
        remap[v] = id;
        return id;
    };
    std::vector<char> usedByAny(current.verts.size(), 0);
    for (const PolyFace& f : current.faces) for (int v : f.verts) if (v >= 0 && v < (int)usedByAny.size()) usedByAny[v] = 1;
    for (int f = 0; f < (int)current.faces.size(); ++f) {
        if (!keepComp[d.faceComp[f]]) continue;
        PolyFace nf = current.faces[f];
        for (int& v : nf.verts) v = mapVert(v);
        out.faces.push_back(std::move(nf));
    }
    for (int v = 0; v < (int)current.verts.size(); ++v) if (!usedByAny[v]) mapVert(v);   // loose vertices are the user's too
    for (const auto& s : current.seams) {
        auto a = remap.find(s.first), b = remap.find(s.second);
        if (a != remap.end() && b != remap.end()) out.SetSeam(a->second, b->second, true);
    }
    R.customFaces = 0;
    for (int c = 0; c < d.comps; ++c) if (d.compParts[c].empty()) R.customFaces += (int)d.compFaces[c].size();
    for (const GeneratedPart& p : fresh) {
        if (preserved.count(p.id)) continue;
        shapes::Append(out, p.mesh);
        ++R.regenerated;
    }
    R.mesh = std::move(out);
    R.recipe = nx;
    R.ok = true;
    R.message = "Rebuilt " + std::to_string(R.regenerated) + " part(s); kept " + std::to_string(R.kept) + " edited part(s), " +
                std::to_string(R.customFaces) + " of your own faces untouched" + (R.discarded ? ", discarded your edits on " + std::to_string(R.discarded) + " part(s)" : "") + ".";
    return R;
}

// =====================================================================================================
// Validation and variation
// =====================================================================================================
ValidationReport Validate(const std::vector<GeneratedPart>& parts, bool checkVolume) {
    ValidationReport rep;
    rep.parts = (int)parts.size();
    std::vector<Vec3> lo(parts.size()), hi(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
        const PolyMesh& m = parts[i].mesh;
        bool finite = true;
        lo[i] = {1e30f, 1e30f, 1e30f}; hi[i] = {-1e30f, -1e30f, -1e30f};
        for (const Vec3& v : m.verts) {
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) { finite = false; continue; }
            lo[i] = {std::min(lo[i].x, v.x), std::min(lo[i].y, v.y), std::min(lo[i].z, v.z)};
            hi[i] = {std::max(hi[i].x, v.x), std::max(hi[i].y, v.y), std::max(hi[i].z, v.z)};
        }
        if (!finite) { ++rep.nonFinite; rep.problems.push_back(parts[i].id + ": a position is not a number"); continue; }
        const PolyMesh::Report r = m.Check();
        if (r.badIndices) { ++rep.badIndices; rep.problems.push_back(parts[i].id + ": a face uses a missing vertex"); }
        if (r.degenerateFaces) { rep.degenerate += r.degenerateFaces; rep.problems.push_back(parts[i].id + ": " + std::to_string(r.degenerateFaces) + " degenerate faces"); }
        if (!r.Closed()) { ++rep.openParts; rep.problems.push_back(parts[i].id + ": not watertight (" + std::to_string(r.boundaryEdges) + " open edges, " + std::to_string(r.nonManifoldEdges) + " non-manifold)"); }
        if (checkVolume) {
            double vol = 0;
            for (int f = 0; f < (int)m.faces.size(); ++f) {
                const std::vector<int> tri = m.TriangulateFace(f);
                for (size_t k = 0; k + 2 < tri.size(); k += 3) {
                    const Vec3& a = m.verts[m.faces[f].verts[tri[k]]];
                    const Vec3& b = m.verts[m.faces[f].verts[tri[k + 1]]];
                    const Vec3& c = m.verts[m.faces[f].verts[tri[k + 2]]];
                    vol += (double)Vec3::Dot(a, Vec3::Cross(b, c)) / 6.0;
                }
            }
            if (!(vol > 0)) { ++rep.invertedParts; rep.problems.push_back(parts[i].id + ": faces point inward (volume " + std::to_string(vol) + ")"); }
        }
    }
    // connected groups: parts whose boxes touch (a small margin) are one group
    DSU dsu(std::max((int)parts.size(), 1));
    const float mg = 0.03f;
    for (size_t i = 0; i < parts.size(); ++i)
        for (size_t j = i + 1; j < parts.size(); ++j)
            if (lo[i].x <= hi[j].x + mg && lo[j].x <= hi[i].x + mg && lo[i].y <= hi[j].y + mg && lo[j].y <= hi[i].y + mg && lo[i].z <= hi[j].z + mg && lo[j].z <= hi[i].z + mg)
                dsu.Union((int)i, (int)j);
    std::set<int> roots;
    for (size_t i = 0; i < parts.size(); ++i) roots.insert(dsu.Find((int)i));
    rep.components = (int)roots.size();
    return rep;
}

Recipe Randomise(const Template& t, const Recipe& base, uint32_t seed, float strength) {
    Recipe r = Normalize(t, base);
    uint32_t s = seed * 2654435761u + 977u;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xffff) / 65535.0f; };
    for (const ParamDef& p : t.params) {
        if (p.id == kDetailParam || p.id == "length" && t.usesPath) continue;   // the detail level and a path's length are the user's
        if (p.id == "seed") { r.params[p.id] = (double)(seed % 1000); continue; }
        if (p.kind == ParamDef::Kind::Bool || p.kind == ParamDef::Kind::Choice) continue;   // structure stays with the style
        const double span = (p.max - p.min) * strength;
        double v = r.Param(p.id, p.def) + ((double)rnd() * 2.0 - 1.0) * span;
        r.params[p.id] = std::min(std::max(v, p.min), p.max);
    }
    for (const SlotDef& sd : t.slots) {
        if (sd.designs.size() < 2 || rnd() > 0.35f) continue;
        const std::string pick = sd.designs[(size_t)(rnd() * (float)sd.designs.size()) % sd.designs.size()].id;
        r.slotDesign[sd.id] = pick;
    }
    if (t.constrain) t.constrain(r);
    return Normalize(t, r);
}

// =====================================================================================================
// Files
// =====================================================================================================
std::string SidecarPath(const std::string& meshPath) {
    std::filesystem::path p(meshPath);
    p.replace_extension(".okayguided");
    return p.string();
}

static bool WriteText(const std::string& path, const std::string& text, std::string* error) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { if (error) *error = "can't write " + path; return false; }
    f << text;
    if (!f.good()) { if (error) *error = "writing " + path + " failed"; return false; }
    return true;
}
static bool ReadText(const std::string& path, std::string& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

bool SaveRecipe(const std::string& path, const Recipe& r, std::string* error) { return WriteText(path, r.Serialize(), error); }
bool LoadRecipe(const std::string& path, Recipe& r, std::string* error) {
    std::string text, err;
    if (!ReadText(path, text, error)) return false;
    if (!Recipe::Parse(text, r, &err)) { if (error) *error = path + ": " + err; return false; }
    return true;
}

bool ConvertToRegular(const std::string& meshPath, std::string* error) {
    std::error_code ec;
    const std::string side = SidecarPath(meshPath);
    if (!std::filesystem::exists(side, ec)) { if (error) *error = "this model has no guided recipe"; return false; }
    if (!std::filesystem::remove(side, ec) || ec) { if (error) *error = "can't delete " + side; return false; }
    return true;
}

std::string SerializeUserTemplate(const UserTemplate& t) {
    return "okaytemplate 1\nname " + Clean(t.name) + "\ndescription " + Clean(t.description) + "\n" + t.recipe.Serialize();
}
bool ParseUserTemplate(const std::string& text, UserTemplate& out, std::string* error) {
    std::istringstream in(text);
    std::string line, recipeText;
    int ln = 0;
    bool header = false;
    UserTemplate t;
    while (std::getline(in, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header && line.rfind("okayguided", 0) == 0) { recipeText = line + "\n"; while (std::getline(in, line)) recipeText += line + "\n"; break; }
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag[0] == '#') continue;
        if (!header) {
            int v = 0;
            if (tag != "okaytemplate" || !(ls >> v)) return Fail(error, ln, "not an .okaytemplate file (it should start with \"okaytemplate 1\")");
            if (v != 1) return Fail(error, ln, "template version " + std::to_string(v) + " is newer than this editor reads (1)");
            header = true;
        } else if (tag == "name") t.name = Rest(ls);
        else if (tag == "description") t.description = Rest(ls);
        else return Fail(error, ln, "unknown record '" + tag + "'");
    }
    if (!header) return Fail(error, ln, "empty file");
    std::string err;
    if (!Recipe::Parse(recipeText, t.recipe, &err)) { if (error) *error = "recipe: " + err; return false; }
    if (t.name.empty()) t.name = "My template";
    out = std::move(t);
    return true;
}
bool SaveUserTemplate(const std::string& path, const UserTemplate& t, std::string* error) { return WriteText(path, SerializeUserTemplate(t), error); }
bool SaveUserPart(const std::string& path, const UserPart& p, std::string* error) { return WriteText(path, SerializeUserPart(p), error); }

static std::vector<std::string> FilesWithExt(const std::string& dir, const std::string& ext) {
    std::vector<std::string> v;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ext) v.push_back(it->path().string());
    std::sort(v.begin(), v.end());
    return v;
}
std::vector<UserTemplate> LoadUserTemplates(const std::string& dir) {
    std::vector<UserTemplate> out;
    for (const std::string& f : FilesWithExt(dir, ".okaytemplate")) {
        std::string text; UserTemplate t;
        if (ReadText(f, text, nullptr) && ParseUserTemplate(text, t, nullptr)) out.push_back(std::move(t));
    }
    return out;
}
std::vector<UserPart> LoadUserParts(const std::string& dir) {
    std::vector<UserPart> out;
    for (const std::string& f : FilesWithExt(dir, ".okaypart")) {
        std::string text; UserPart p;
        if (ReadText(f, text, nullptr) && ParseUserPart(text, p, nullptr)) out.push_back(std::move(p));
    }
    return out;
}

// =====================================================================================================
// History
// =====================================================================================================
void GuidedHistory::Push(const GuidedState& before) {
    undo_.push_back(before);
    if (undo_.size() > 64) undo_.erase(undo_.begin());
    redo_.clear();
}
bool GuidedHistory::Undo(GuidedState& cur) {
    if (undo_.empty()) return false;
    redo_.push_back(cur);
    cur = std::move(undo_.back());
    undo_.pop_back();
    return true;
}
bool GuidedHistory::Redo(GuidedState& cur) {
    if (redo_.empty()) return false;
    undo_.push_back(cur);
    cur = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

} // namespace okay::modeling::guided
