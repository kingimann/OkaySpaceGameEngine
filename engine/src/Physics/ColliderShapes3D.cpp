// The Cone, Convex Hull, Compound, Plane and Heightfield colliders and the exact Cylinder (see ColliderShapes3D.hpp).
// Written from general convex-geometry definitions; no code from any other engine.
#include "okay/Physics/ColliderShapes3D.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Scene/ReflectedRecord.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace okay {

namespace {
// Limits that keep a typed-in or damaged list from eating memory or time.
constexpr int kMaxHullPoints = 4096;
constexpr int kMaxCompoundParts = 1024;
constexpr std::size_t kMaxHeights = 4u * 1024u * 1024u;   // 2048 x 2048 samples
constexpr int kMaxGrid = 2048;
constexpr std::size_t kMaxHeightfieldTriangles = 500000;
constexpr float kMaxCoord = 1.0e6f;                       // a point further than this is a typo, not a shape

bool UsableNumber(float v) { return std::isfinite(v) && std::abs(v) <= kMaxCoord; }
bool UsablePoint(const Vec3& v) { return UsableNumber(v.x) && UsableNumber(v.y) && UsableNumber(v.z); }

// Keep at most `maxPoints` of the points that spread the cloud out the most (farthest-point sampling).
std::vector<Vec3> ThinPoints(const std::vector<Vec3>& p, int maxPoints) {
    if ((int)p.size() <= maxPoints) return p;
    std::vector<Vec3> keep; keep.push_back(p[0]);
    std::vector<float> dist(p.size(), 1e30f);
    while ((int)keep.size() < maxPoints) {
        int far = 0; float fd = -1.0f;
        for (std::size_t i = 0; i < p.size(); ++i) {
            dist[i] = std::min(dist[i], (p[i] - keep.back()).SqrMagnitude());
            if (dist[i] > fd) { fd = dist[i]; far = (int)i; }
        }
        if (fd <= 0.0f) break;
        keep.push_back(p[(std::size_t)far]);
    }
    return keep;
}
}  // namespace

namespace shape3d {

Frame FrameOf(const Collider3D& c) {
    Frame f;
    f.c = c.WorldCenter();
    f.r = c.transform ? c.transform->Rotation() : Quat::Identity;
    Vec3 s = c.transform ? c.transform->LossyScale() : Vec3::One;
    f.s = {Mathf::Max(Mathf::Abs(s.x), 1e-6f), Mathf::Max(Mathf::Abs(s.y), 1e-6f), Mathf::Max(Mathf::Abs(s.z), 1e-6f)};
    return f;
}
Vec3 ToLocalDir(const Frame& f, const Vec3& w) {
    Vec3 l = f.r.Inverse() * w;
    return {l.x / f.s.x, l.y / f.s.y, l.z / f.s.z};
}
Vec3 ToLocalPoint(const Frame& f, const Vec3& w) { return ToLocalDir(f, w - f.c); }
Vec3 ToWorldPoint(const Frame& f, const Vec3& l) { return f.c + f.r * Vec3{l.x * f.s.x, l.y * f.s.y, l.z * f.s.z}; }
Vec3 ToWorldNormal(const Frame& f, const Vec3& n) {
    Vec3 w = f.r * Vec3{n.x / f.s.x, n.y / f.s.y, n.z / f.s.z};
    float m = w.Magnitude();
    return m > 1e-12f ? w * (1.0f / m) : Vec3{0, 1, 0};
}
Vec3 WorldSupport(const Frame& f, const LocalShape3D& s, const Vec3& dirWorld) {
    Vec3 d = f.r.Inverse() * dirWorld;
    Vec3 p = s.Support({d.x * f.s.x, d.y * f.s.y, d.z * f.s.z});
    return f.c + f.r * Vec3{p.x * f.s.x, p.y * f.s.y, p.z * f.s.z};
}
void WorldBoundsOfShape(const Frame& f, const LocalShape3D& s, Vec3& mn, Vec3& mx) {
    mn = {WorldSupport(f, s, {-1, 0, 0}).x, WorldSupport(f, s, {0, -1, 0}).y, WorldSupport(f, s, {0, 0, -1}).z};
    mx = {WorldSupport(f, s, {1, 0, 0}).x, WorldSupport(f, s, {0, 1, 0}).y, WorldSupport(f, s, {0, 0, 1}).z};
}
bool WorldRay(const Frame& f, const LocalShape3D& s, const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) {
    Vec3 nl;
    if (!s.Ray(ToLocalPoint(f, o), ToLocalDir(f, d), maxT, t, nl)) return false;
    n = ToWorldNormal(f, nl);
    return true;
}
bool WorldMass(const Frame& f, const LocalShape3D& s, Vec3& com, float& vol, Vec3& unitInertia) {
    vol = s.Volume() * f.s.x * f.s.y * f.s.z;
    if (!(vol > 1e-9f)) return false;
    com = ToWorldPoint(f, s.CenterOfMassOffset());
    // Scale the inertia by the average squared stretch (exact for an even scale).
    float k = std::cbrt(f.s.x * f.s.y * f.s.z); k *= k;
    unitInertia = s.UnitInertia() * k;
    return true;
}
LocalShape3D CylinderShape(const CylinderCollider3D& c) {
    LocalShape3D s; s.kind = LocalShape3D::Kind::Cylinder;
    s.radius = Mathf::Max(c.radius, 1e-4f); s.height = Mathf::Max(c.height, 1e-4f); s.axis = c.axis;
    return s;
}

}  // namespace shape3d

using namespace shape3d;

// ---- Cylinder (exact) ----
Vec3 CylinderCollider3D::ConvexSupport(const Vec3& d) const { return WorldSupport(FrameOf(*this), CylinderShape(*this), d); }
bool CylinderCollider3D::MassShape(Vec3& com, float& vol, Vec3& in) const { return WorldMass(FrameOf(*this), CylinderShape(*this), com, vol, in); }
bool CylinderCollider3D::RayShape(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
    return WorldRay(FrameOf(*this), CylinderShape(*this), o, d, maxT, t, n);
}
void CylinderCollider3D::WorldAABB(Vec3& mn, Vec3& mx) const { WorldBoundsOfShape(FrameOf(*this), CylinderShape(*this), mn, mx); }
float CylinderCollider3D::SegmentHalf() const {
    Vec3 s = transform ? transform->LossyScale() : Vec3::One;
    float along = axis == 0 ? Mathf::Abs(s.x) : axis == 2 ? Mathf::Abs(s.z) : Mathf::Abs(s.y);
    return Mathf::Max(0.0f, height * along * 0.5f);
}
void CylinderCollider3D::Segment(Vec3& a, Vec3& b) const {
    Vec3 c = WorldCenter(), dir = WorldAxisDir();
    float h = SegmentHalf();
    a = c - dir * h; b = c + dir * h;
}

// ---- ShapeCollider3D ----
Vec3 ShapeCollider3D::ConvexSupport(const Vec3& d) const { return WorldSupport(FrameOf(*this), Local(), d); }
bool ShapeCollider3D::MassShape(Vec3& com, float& vol, Vec3& in) const { return WorldMass(FrameOf(*this), Local(), com, vol, in); }
bool ShapeCollider3D::RayShape(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
    return WorldRay(FrameOf(*this), Local(), o, d, maxT, t, n);
}
void ShapeCollider3D::WorldAABB(Vec3& mn, Vec3& mx) const { WorldBoundsOfShape(FrameOf(*this), Local(), mn, mx); }

LocalShape3D ConeCollider3D::Local() const {
    LocalShape3D s; s.kind = LocalShape3D::Kind::Cone;
    s.radius = Mathf::Max(radius, 1e-4f); s.height = Mathf::Max(height, 1e-4f); s.axis = axis;
    return s;
}

// ---- Convex Hull ----
std::string PointsToText(const std::vector<Vec3>& p) {
    std::ostringstream o; o << std::setprecision(9);
    for (std::size_t i = 0; i < p.size(); ++i) o << (i ? "; " : "") << p[i].x << ' ' << p[i].y << ' ' << p[i].z;
    return o.str();
}
std::vector<Vec3> PointsFromText(const std::string& s) {
    std::vector<Vec3> out;
    std::string cur;
    auto flush = [&]() {
        std::istringstream in(cur);
        Vec3 v; if (in >> v.x >> v.y >> v.z && UsablePoint(v) && out.size() < 4u * (std::size_t)kMaxHullPoints * 4u) out.push_back(v);
        cur.clear();
    };
    for (char ch : s) { if (ch == ';') flush(); else cur += ch; }
    flush();
    return out;
}

void ConvexHullCollider3D::Refresh() const {
    if (m_key == points && !m_pts.empty()) return;
    m_key = points;
    m_pts = PointsFromText(points);
    if ((int)m_pts.size() > kMaxHullPoints) {                    // too many typed in: wrap them, then keep the spread-out corners
        m_pts = ThinPoints(m_pts, kMaxHullPoints);               // (thin first: the wrap itself is slow on huge clouds)
    }
    if (m_pts.empty()) {
        for (int i = 0; i < 8; ++i) m_pts.push_back({i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f});
    }
    m_shape = LocalShape3D{};
    m_shape.kind = LocalShape3D::Kind::Hull;
    Mesh h = Mesh::ConvexHull(m_pts);
    if (h.vertices.size() >= 4 && h.triangles.size() >= 12) { m_shape.pts = h.vertices; m_shape.tris = h.triangles; }
    else m_shape.pts = m_pts;                                   // flat or too few points: a support-only shape
}
const std::vector<Vec3>& ConvexHullCollider3D::Points() const { Refresh(); return m_pts; }
LocalShape3D ConvexHullCollider3D::Local() const { Refresh(); return m_shape; }

void ConvexHullCollider3D::SetPoints(const std::vector<Vec3>& in, int maxPoints) {
    std::vector<Vec3> p;
    for (const Vec3& v : in) if (UsablePoint(v)) p.push_back(v);
    p = ThinPoints(p, kMaxHullPoints);                          // (thin first: the wrap itself is slow on huge clouds)
    Mesh h = Mesh::ConvexHull(p);
    if (h.vertices.size() >= 4) p = h.vertices;                 // only the corners matter
    maxPoints = std::min(std::max(maxPoints, 4), kMaxHullPoints);
    p = ThinPoints(p, maxPoints);
    points = PointsToText(p);
    m_key.clear();
}
bool ConvexHullCollider3D::SetFromMesh(const Mesh& m, int maxPoints) {
    std::vector<Vec3> good;
    for (const Vec3& v : m.vertices) if (UsablePoint(v)) good.push_back(v);
    good = ThinPoints(good, kMaxHullPoints);
    Mesh h = Mesh::ConvexHull(good);
    if (h.vertices.size() < 4) return false;
    SetPoints(h.vertices, maxPoints);
    return true;
}

// ---- Plane ----
void PlaneCollider3D::WorldAABB(Vec3& mn, Vec3& mx) const {
    Vec3 c = WorldCenter(), n = WorldNormal();
    const float R = 3000.0f;
    mn = {c.x - R, c.y - R, c.z - R}; mx = {c.x + R, c.y + R, c.z + R};
    if (n.x > 0.999f) mx.x = c.x; else if (n.x < -0.999f) mn.x = c.x;
    if (n.y > 0.999f) mx.y = c.y; else if (n.y < -0.999f) mn.y = c.y;
    if (n.z > 0.999f) mx.z = c.z; else if (n.z < -0.999f) mn.z = c.z;
}
bool PlaneCollider3D::RayShape(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
    Vec3 nn = WorldNormal();
    float side = Vec3::Dot(o - WorldCenter(), nn), dn = Vec3::Dot(d, nn);
    if (side <= 0.0f || dn >= -1e-12f) return false;            // below it, or moving away: no entry
    float s = -side / dn;
    if (s < 0.0f || s > maxT) return false;
    t = s; n = nn; return true;
}

// ---- Compound ----
namespace {
const char* KindName(LocalShape3D::Kind k) {
    switch (k) {
    case LocalShape3D::Kind::Box: return "box";
    case LocalShape3D::Kind::Sphere: return "sphere";
    case LocalShape3D::Kind::Capsule: return "capsule";
    case LocalShape3D::Kind::Cylinder: return "cylinder";
    case LocalShape3D::Kind::Cone: return "cone";
    default: return "box";
    }
}
std::string PartText(const char* kind, const Vec3& p, const Vec3& e, float a, float b, float c, int axis) {
    std::ostringstream o; o << std::setprecision(9);
    o << kind << ' ' << p.x << ' ' << p.y << ' ' << p.z << ' ' << e.x << ' ' << e.y << ' ' << e.z << ' ' << a << ' ' << b << ' ' << c << ' ' << axis;
    return o.str();
}
}  // namespace

void CompoundCollider3D::Refresh() const {
    if (m_key == parts) return;
    m_key = parts;
    m_parts.clear();
    std::string cur;
    auto flush = [&]() {
        std::istringstream in(cur); cur.clear();
        if ((int)m_parts.size() >= kMaxCompoundParts) return;
        std::string kind; Part p; float a = 0.5f, b = 1.0f, c = 1.0f; int axis = 1;
        if (!(in >> kind >> p.pos.x >> p.pos.y >> p.pos.z >> p.euler.x >> p.euler.y >> p.euler.z)) return;
        in >> a >> b >> c >> axis;
        if (!UsablePoint(p.pos) || !UsablePoint(p.euler) || !UsableNumber(a) || !UsableNumber(b) || !UsableNumber(c)) return;
        p.rot = Quat::Euler(p.euler);
        LocalShape3D& s = p.shape;
        axis = std::clamp(axis, 0, 2);
        if (kind == "box") { s.kind = LocalShape3D::Kind::Box; s.half = {Mathf::Abs(a) * 0.5f, Mathf::Abs(b) * 0.5f, Mathf::Abs(c) * 0.5f}; if (!(in)) s.half = {0.5f, 0.5f, 0.5f}; }
        else if (kind == "sphere") { s.kind = LocalShape3D::Kind::Sphere; s.radius = Mathf::Max(Mathf::Abs(a), 1e-4f); }
        else if (kind == "capsule" || kind == "cylinder" || kind == "cone") {
            s.kind = kind == "capsule" ? LocalShape3D::Kind::Capsule : kind == "cylinder" ? LocalShape3D::Kind::Cylinder : LocalShape3D::Kind::Cone;
            s.radius = Mathf::Max(Mathf::Abs(a), 1e-4f); s.height = Mathf::Max(Mathf::Abs(b), 1e-4f); s.axis = axis;
        } else return;
        m_parts.push_back(p);
    };
    for (char ch : parts) { if (ch == ';') flush(); else cur += ch; }
    flush();
}
const std::vector<CompoundCollider3D::Part>& CompoundCollider3D::Parts() const { Refresh(); return m_parts; }
static void AppendPart(std::string& parts, const std::string& t) { if (!parts.empty()) parts += "; "; parts += t; }
void CompoundCollider3D::AddBox(const Vec3& p, const Vec3& size, const Vec3& e) { AppendPart(parts, PartText("box", p, e, size.x, size.y, size.z, 1)); }
void CompoundCollider3D::AddSphere(const Vec3& p, float r) { AppendPart(parts, PartText("sphere", p, Vec3::Zero, r, 0, 0, 1)); }
void CompoundCollider3D::AddCapsule(const Vec3& p, float r, float h, int ax, const Vec3& e) { AppendPart(parts, PartText("capsule", p, e, r, h, 0, ax)); }
void CompoundCollider3D::AddCylinder(const Vec3& p, float r, float h, int ax, const Vec3& e) { AppendPart(parts, PartText("cylinder", p, e, r, h, 0, ax)); }
void CompoundCollider3D::AddCone(const Vec3& p, float r, float h, int ax, const Vec3& e) { AppendPart(parts, PartText("cone", p, e, r, h, 0, ax)); }

Frame CompoundCollider3D::PartFrame(int i) const {
    const Part& p = Parts()[(std::size_t)i];
    Frame f = FrameOf(*this);
    Frame out;
    out.c = f.c + f.r * Vec3{p.pos.x * f.s.x, p.pos.y * f.s.y, p.pos.z * f.s.z};
    out.r = f.r * p.rot;
    out.s = f.s;   // (exact for an even scale; a stretched compound squashes each part along its own axes)
    return out;
}
Vec3 CompoundCollider3D::PartSupport(int i, const Vec3& d) const { return WorldSupport(PartFrame(i), Parts()[(std::size_t)i].shape, d); }
void CompoundCollider3D::PartBounds(int i, Vec3& mn, Vec3& mx) const { WorldBoundsOfShape(PartFrame(i), Parts()[(std::size_t)i].shape, mn, mx); }
void CompoundCollider3D::WorldAABB(Vec3& mn, Vec3& mx) const {
    const auto& ps = Parts();
    if (ps.empty()) { Vec3 c = WorldCenter(); mn = c - Vec3{0.01f, 0.01f, 0.01f}; mx = c + Vec3{0.01f, 0.01f, 0.01f}; return; }
    mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < (int)ps.size(); ++i) {
        Vec3 a, b; PartBounds(i, a, b);
        mn = Vec3::Min(mn, a); mx = Vec3::Max(mx, b);
    }
}
bool CompoundCollider3D::RayShape(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
    bool hit = false; float best = maxT;
    for (int i = 0; i < PartCount(); ++i) {
        float tt; Vec3 nn;
        if (WorldRay(PartFrame(i), Parts()[(std::size_t)i].shape, o, d, best, tt, nn) && tt <= best) { hit = true; best = tt; t = tt; n = nn; }
    }
    return hit;
}
bool CompoundCollider3D::MassShape(Vec3& com, float& vol, Vec3& unitInertia) const {
    const auto& ps = Parts();
    struct One { Vec3 c; float v; Vec3 diag; Quat r; };
    std::vector<One> ones;
    double V = 0; Vec3 csum = Vec3::Zero;
    for (int i = 0; i < (int)ps.size(); ++i) {
        One o; Vec3 d;
        Frame pf = PartFrame(i);
        if (!WorldMass(pf, ps[(std::size_t)i].shape, o.c, o.v, d)) continue;
        o.diag = d; o.r = pf.r;
        ones.push_back(o); V += o.v; csum = csum + o.c * o.v;
    }
    if (ones.empty() || V < 1e-9) return false;
    com = csum * (float)(1.0 / V); vol = (float)V;
    Quat q = transform ? transform->Rotation() : Quat::Identity, qi = q.Inverse();
    double I[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (const One& o : ones) {
        double w = o.v / V;
        // Rotate the part's diagonal inertia into the object's axes: R diag R^T, R = qi * part rotation.
        Quat rel = qi * o.r;
        Vec3 ex = rel * Vec3{1, 0, 0}, ey = rel * Vec3{0, 1, 0}, ez = rel * Vec3{0, 0, 1};
        const Vec3 col[3] = {ex, ey, ez}; const float dg[3] = {o.diag.x, o.diag.y, o.diag.z};
        auto at = [](const Vec3& v, int k) { return k == 0 ? v.x : k == 1 ? v.y : v.z; };
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) {
            double sum = 0; for (int k = 0; k < 3; ++k) sum += (double)dg[k] * at(col[k], r) * at(col[k], c);
            I[r][c] += w * sum;
        }
        Vec3 off = qi * (o.c - com);
        double dd = (double)off.SqrMagnitude();
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
            I[r][c] += w * ((r == c ? dd : 0.0) - (double)at(off, r) * at(off, c));
    }
    unitInertia = {(float)I[0][0], (float)I[1][1], (float)I[2][2]};
    return true;
}

// ---- Heightfield ----
void HeightfieldCollider3D::Refresh() const {
    if (m_key == heights) return;
    m_key = heights;
    m_h.clear();
    std::istringstream in(heights);
    float v;
    while (m_h.size() < kMaxHeights && in >> v) m_h.push_back(std::isfinite(v) ? v : 0.0f);
    m_lo = 0.0f; m_hi = 0.0f;
    if (!m_h.empty()) { m_lo = *std::min_element(m_h.begin(), m_h.end()); m_hi = *std::max_element(m_h.begin(), m_h.end()); }
}
float HeightfieldCollider3D::HeightAt(int c, int r) const {
    Refresh();
    const int C = Cols(), R = Rows();
    c = std::clamp(c, 0, C - 1); r = std::clamp(r, 0, R - 1);
    std::size_t i = (std::size_t)r * (std::size_t)C + (std::size_t)c;
    float k = std::isfinite(heightScale) ? heightScale : 0.0f;
    return i < m_h.size() ? m_h[i] * k : 0.0f;
}
void HeightfieldCollider3D::SetHeights(int cols, int rws, const std::vector<float>& h) {
    columns = std::max(cols, 2); rows = std::max(rws, 2);
    std::ostringstream o; o << std::setprecision(9);
    for (std::size_t i = 0; i < h.size(); ++i) o << (i ? " " : "") << h[i];
    heights = o.str(); m_key.clear();
}
bool HeightfieldCollider3D::SetFromTerrain(const Terrain& t) {
    int dim = t.Dim();
    if (dim < 2 || (int)t.heights.size() < dim * dim) return false;
    cellX = cellZ = t.CellSize(); heightScale = 1.0f;
    SetHeights(dim, dim, t.heights);
    return true;
}
float HeightfieldCollider3D::Depth() const {
    Refresh();
    return Mathf::Max(2.0f, (m_hi - m_lo) * Mathf::Abs(heightScale) + 2.0f * Mathf::Max(CX(), CZ()));
}
bool HeightfieldCollider3D::LocalHeight(float x, float z, float& y) const {
    const int C = Cols(), R = Rows();
    float fx = x / CX() + (C - 1) * 0.5f, fz = z / CZ() + (R - 1) * 0.5f;
    if (!(fx >= 0.0f) || !(fz >= 0.0f) || !(fx <= C - 1) || !(fz <= R - 1)) return false;   // (also catches NaN)
    int i = std::min((int)fx, C - 2), j = std::min((int)fz, R - 2);
    float u = fx - i, v = fz - j;
    float h00 = HeightAt(i, j), h10 = HeightAt(i + 1, j), h01 = HeightAt(i, j + 1), h11 = HeightAt(i + 1, j + 1);
    // Cell triangles: (00,10,01) and (10,11,01), split along the 10-01 diagonal.
    y = (u + v <= 1.0f) ? h00 + (h10 - h00) * u + (h01 - h00) * v : h11 + (h01 - h11) * (1.0f - u) + (h10 - h11) * (1.0f - v);
    return true;
}
void HeightfieldCollider3D::CollectTriangles(const Vec3& mn, const Vec3& mx, std::vector<std::array<Vec3, 3>>& out) const {
    const Frame f = FrameOf(*this);
    const int C = Cols(), R = Rows();
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (int k = 0; k < 8; ++k) {
        Vec3 l = ToLocalPoint(f, {k & 1 ? mx.x : mn.x, k & 2 ? mx.y : mn.y, k & 4 ? mx.z : mn.z});
        lo = Vec3::Min(lo, l); hi = Vec3::Max(hi, l);
    }
    int i0 = std::max(0, (int)std::floor(lo.x / CX() + (C - 1) * 0.5f)), i1 = std::min(C - 2, (int)std::floor(hi.x / CX() + (C - 1) * 0.5f));
    int j0 = std::max(0, (int)std::floor(lo.z / CZ() + (R - 1) * 0.5f)), j1 = std::min(R - 2, (int)std::floor(hi.z / CZ() + (R - 1) * 0.5f));
    auto P = [&](int i, int j) { return ToWorldPoint(f, {(i - (C - 1) * 0.5f) * CX(), HeightAt(i, j), (j - (R - 1) * 0.5f) * CZ()}); };
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            if (out.size() >= kMaxHeightfieldTriangles) return;   // a box over a huge grid: stop, never fill memory
            out.push_back({P(i, j), P(i + 1, j), P(i, j + 1)});
            out.push_back({P(i + 1, j), P(i + 1, j + 1), P(i, j + 1)});
        }
}
void HeightfieldCollider3D::WorldAABB(Vec3& mn, Vec3& mx) const {
    Refresh();
    const Frame f = FrameOf(*this);
    const int C = Cols(), R = Rows();
    float x1 = (C - 1) * 0.5f * CX(), z1 = (R - 1) * 0.5f * CZ();
    float y0 = std::min(m_lo, m_hi) * heightScale, y1 = std::max(m_lo, m_hi) * heightScale;
    if (heightScale < 0) std::swap(y0, y1);
    y0 -= Depth();
    mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
    for (int k = 0; k < 8; ++k) {
        Vec3 w = ToWorldPoint(f, {k & 1 ? x1 : -x1, k & 2 ? y1 : y0, k & 4 ? z1 : -z1});
        mn = Vec3::Min(mn, w); mx = Vec3::Max(mx, w);
    }
}
bool HeightfieldCollider3D::RayShape(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
    Refresh();
    const Frame f = FrameOf(*this);
    Vec3 ol = ToLocalPoint(f, o), dl = ToLocalDir(f, d);
    const int C = Cols(), R = Rows();
    float x1 = (C - 1) * 0.5f * CX(), z1 = (R - 1) * 0.5f * CZ();
    // Clip the ray to the grid's footprint, then walk it in steps of half a cell looking for the surface.
    float t0 = 0.0f, t1 = maxT;
    const float oo[2] = {ol.x, ol.z}, dd[2] = {dl.x, dl.z}, hh[2] = {x1, z1};
    for (int a = 0; a < 2; ++a) {
        if (Mathf::Abs(dd[a]) < 1e-12f) { if (oo[a] < -hh[a] || oo[a] > hh[a]) return false; continue; }
        float s0 = (-hh[a] - oo[a]) / dd[a], s1 = (hh[a] - oo[a]) / dd[a];
        if (s0 > s1) std::swap(s0, s1);
        t0 = std::max(t0, s0); t1 = std::min(t1, s1);
        if (t0 > t1) return false;
    }
    float hy;
    auto above = [&](float s) { Vec3 p = ol + dl * s; float y; if (!LocalHeight(p.x, p.z, y)) return true; hy = y; return p.y >= y; };
    if (!above(t0)) return false;                                        // starts below the surface: inside the solid
    float lenXZ = std::sqrt(dl.x * dl.x + dl.z * dl.z);
    float step = lenXZ > 1e-9f ? 0.5f * std::min(CX(), CZ()) / lenXZ : (t1 - t0);
    step = std::max(step, (t1 - t0) / 20000.0f);
    float prev = t0;
    for (float s = t0 + step;; s += step) {
        bool last = s >= t1; if (last) s = t1;
        if (!above(s)) {
            float a = prev, b = s;
            for (int it = 0; it < 24; ++it) { float m = 0.5f * (a + b); if (above(m)) a = m; else b = m; }
            t = b;
            Vec3 p = ol + dl * t;
            float e = 0.25f * std::min(CX(), CZ()), hxm, hxp, hzm, hzp;
            float cy;
            LocalHeight(p.x, p.z, cy);
            if (!LocalHeight(p.x - e, p.z, hxm)) hxm = cy;
            if (!LocalHeight(p.x + e, p.z, hxp)) hxp = cy;
            if (!LocalHeight(p.x, p.z - e, hzm)) hzm = cy;
            if (!LocalHeight(p.x, p.z + e, hzp)) hzp = cy;
            Vec3 nl{-(hxp - hxm) / (2 * e), 1.0f, -(hzp - hzm) / (2 * e)};
            n = ToWorldNormal(f, nl);
            return t <= maxT;
        }
        if (last) break;
        prev = s;
    }
    return false;
}

bool IsGenericShape(const Collider3D* c) {
    switch (c->shape()) {
    case Collider3D::Shape::Cylinder: case Collider3D::Shape::Cone: case Collider3D::Shape::ConvexHull:
    case Collider3D::Shape::Compound: case Collider3D::Shape::Heightfield: case Collider3D::Shape::Plane:
        return true;
    default: return false;
    }
}

// ---- Scene file records (written from the Inspector rows; a collider writes only itself) ----
namespace detail {
void RegisterColliderShapeSerializers() {
    reflected::RegisterReflectedRecord<ConeCollider3D>("ConeCollider3D", "conecollider3d");
    reflected::RegisterReflectedRecord<ConvexHullCollider3D>("ConvexHullCollider3D", "convexhullcollider3d");
    reflected::RegisterReflectedRecord<PlaneCollider3D>("PlaneCollider3D", "planecollider3d");
    reflected::RegisterReflectedRecord<CompoundCollider3D>("CompoundCollider3D", "compoundcollider3d");
    reflected::RegisterReflectedRecord<HeightfieldCollider3D>("HeightfieldCollider3D", "heightfieldcollider3d");
}
} // namespace detail

} // namespace okay
