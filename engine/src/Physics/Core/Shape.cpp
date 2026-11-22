// Convex shapes of the physics core (see Shape.hpp for the sources each part was written from).
#include "okay/Physics/Core/Shape.hpp"
#include "okay/Physics/Core/DynamicTree.hpp"

#include <algorithm>
#include <cmath>

namespace okay {
namespace phys {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kRimPoints = 12;          // a cylinder or cone cap is clipped as a 12-sided polygon
constexpr float kFaceCos = 0.995f;      // a face "faces" d when its normal is within about 5.7 degrees of d
constexpr float kEdgeSin = 0.1f;        // an edge or the side of a round shape faces d within about 5.7 degrees

float Sgn(float v) { return v < 0.0f ? -1.0f : 1.0f; }

// The covariance of the canonical tetrahedron (0, e1, e2, e3) with unit density (Blow and Binstock 2004).
const float kCanon[3][3] = {{2.0f / 120.0f, 1.0f / 120.0f, 1.0f / 120.0f},
                            {1.0f / 120.0f, 2.0f / 120.0f, 1.0f / 120.0f},
                            {1.0f / 120.0f, 1.0f / 120.0f, 2.0f / 120.0f}};

M33 InertiaFromCovariance(const M33& c) {
    const float tr = c.m[0][0] + c.m[1][1] + c.m[2][2];
    M33 r = c * -1.0f;
    r.m[0][0] += tr; r.m[1][1] += tr; r.m[2][2] += tr;
    return r;
}
} // namespace

void SortPolygon(V3* pts, int count, const V3& n) {
    if (count < 3) return;
    V3 c;
    for (int i = 0; i < count; ++i) c += pts[i];
    c *= 1.0f / (float)count;
    V3 t1, t2;
    Basis(n, t1, t2);   // (n, t1, t2) right-handed: angle from t1 toward t2 is counter-clockwise seen from n
    float ang[32];
    int idx[32];
    for (int i = 0; i < count; ++i) {
        const V3 d = pts[i] - c;
        ang[i] = std::atan2(Dot(d, t2), Dot(d, t1));
        idx[i] = i;
    }
    std::sort(idx, idx + count, [&](int a, int b) { return ang[a] < ang[b] || (ang[a] == ang[b] && a < b); });
    V3 tmp[32];
    for (int i = 0; i < count; ++i) tmp[i] = pts[idx[i]];
    for (int i = 0; i < count; ++i) pts[i] = tmp[i];
}

// ---- triangle meshes ---------------------------------------------------------------------------------------------------
struct TriMeshSource::Tree { DynamicTree tree; };
TriMeshSource::TriMeshSource(std::vector<Tri3> tris) : m_tris(std::move(tris)), m_tree(new Tree) {
    m_tree->tree.margin = 0.0f;
    m_bounds = Aabb{V3{1e30f, 1e30f, 1e30f}, V3{-1e30f, -1e30f, -1e30f}};
    for (std::size_t i = 0; i < m_tris.size(); ++i) {
        const Tri3& t = m_tris[i];
        const Aabb b{Min(Min(t.a, t.b), t.c), Max(Max(t.a, t.b), t.c)};
        m_bounds = Aabb::Union(m_bounds, b);
        m_tree->tree.Create(b, (std::uint64_t)i);
    }
    if (m_tris.empty()) m_bounds = Aabb{};
}
TriMeshSource::~TriMeshSource() = default;
void TriMeshSource::Collect(const Aabb& box, std::vector<Tri3>& out) const {
    m_tree->tree.Query(box, [&](int p) { out.push_back(m_tris[(std::size_t)m_tree->tree.User(p)]); return true; });
}

ScaledTriangleSource::ScaledTriangleSource(std::shared_ptr<const TriangleSource> base, const V3& scale)
    : m_base(std::move(base)), m_scale(scale) {
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(m_scale[k]) < 1e-6f) m_scale[k] = m_scale[k] < 0.0f ? -1e-6f : 1e-6f;
        m_inv[k] = 1.0f / m_scale[k];
    }
    const Aabb b = m_base ? m_base->Bounds() : Aabb{};
    const V3 p = Mul(b.lo, m_scale), q = Mul(b.hi, m_scale);
    m_bounds = Aabb{Min(p, q), Max(p, q)};
}
void ScaledTriangleSource::Collect(const Aabb& box, std::vector<Tri3>& out) const {
    if (!m_base) return;
    const V3 p = Mul(box.lo, m_inv), q = Mul(box.hi, m_inv);
    const std::size_t first = out.size();
    m_base->Collect(Aabb{Min(p, q), Max(p, q)}, out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].a = Mul(out[i].a, m_scale); out[i].b = Mul(out[i].b, m_scale); out[i].c = Mul(out[i].c, m_scale);
    }
}

HeightGridSource::HeightGridSource(int cols, int rows, float x0, float z0, float cx, float cz, std::vector<float> heights)
    : m_cols(std::max(cols, 2)), m_rows(std::max(rows, 2)), m_x0(x0), m_z0(z0), m_cx(std::max(cx, 1e-6f)), m_cz(std::max(cz, 1e-6f)),
      m_h(std::move(heights)) {
    m_h.resize((std::size_t)m_cols * (std::size_t)m_rows, 0.0f);
    float lo = 1e30f, hi = -1e30f;
    for (float& h : m_h) { if (!std::isfinite(h)) h = 0.0f; lo = std::min(lo, h); hi = std::max(hi, h); }
    m_bounds = Aabb{V3{m_x0, lo, m_z0}, V3{m_x0 + (float)(m_cols - 1) * m_cx, hi, m_z0 + (float)(m_rows - 1) * m_cz}};
}
void HeightGridSource::Collect(const Aabb& box, std::vector<Tri3>& out) const {
    if (!box.Overlaps(m_bounds)) return;
    const int i0 = std::max(0, (int)std::floor((box.lo.x - m_x0) / m_cx)), i1 = std::min(m_cols - 2, (int)std::floor((box.hi.x - m_x0) / m_cx));
    const int j0 = std::max(0, (int)std::floor((box.lo.z - m_z0) / m_cz)), j1 = std::min(m_rows - 2, (int)std::floor((box.hi.z - m_z0) / m_cz));
    std::size_t made = 0;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            if (made >= kMaxTriangles) return;
            const V3 a = P(i, j), b = P(i + 1, j), c = P(i, j + 1), d = P(i + 1, j + 1);
            const float lo = std::min({a.y, b.y, c.y, d.y}), hi = std::max({a.y, b.y, c.y, d.y});
            if (hi < box.lo.y || lo > box.hi.y) continue;
            out.push_back({a, b, c});
            out.push_back({b, d, c});
            made += 2;
        }
}

// ---- convex hull -------------------------------------------------------------------------------------------------------
std::shared_ptr<HullData> HullData::Build(const std::vector<V3>& input) {
    std::vector<V3> pts;
    pts.reserve(std::min(input.size(), kMaxInput));
    for (const V3& p : input) {
        if (pts.size() >= kMaxInput) break;
        if (Finite(p)) pts.push_back(p);
    }
    auto out = std::make_shared<HullData>();
    V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const V3& p : pts) { lo = Min(lo, p); hi = Max(hi, p); }
    if (pts.empty()) { lo = V3{-0.5f, -0.5f, -0.5f}; hi = V3{0.5f, 0.5f, 0.5f}; }
    const float scale = std::max(Len(hi - lo), 1e-4f);
    const float eps = 1e-5f * scale;
    const float thick = std::max(1e-3f, 0.01f * scale);

    auto boxAround = [&]() {   // fewer than 4 useful points, or all on a line: a thin box around them
        std::vector<V3> b;
        const Aabb a = Aabb{lo, hi}.Grown(thick * 0.5f);
        for (int i = 0; i < 8; ++i) b.push_back(V3{(i & 1) ? a.hi.x : a.lo.x, (i & 2) ? a.hi.y : a.lo.y, (i & 4) ? a.hi.z : a.lo.z});
        return Build(b);
    };
    if (pts.size() < 4) return boxAround();

    // the starting tetrahedron from extreme points
    std::size_t i0 = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) if (pts[i].x < pts[i0].x) i0 = i;
    std::size_t i1 = i0; float best = -1.0f;
    for (std::size_t i = 0; i < pts.size(); ++i) { const float d = LenSq(pts[i] - pts[i0]); if (d > best) { best = d; i1 = i; } }
    const V3 axis = Normalize(pts[i1] - pts[i0]);
    std::size_t i2 = i0; best = -1.0f;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const V3 v = pts[i] - pts[i0];
        const float d = LenSq(v - axis * Dot(v, axis));
        if (d > best) { best = d; i2 = i; }
    }
    if (best < eps * eps) return boxAround();
    const V3 pn = Normalize(Cross(pts[i1] - pts[i0], pts[i2] - pts[i0]));
    std::size_t i3 = i0; best = -1.0f;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const float d = std::fabs(Dot(pts[i] - pts[i0], pn));
        if (d > best) { best = d; i3 = i; }
    }
    if (best < eps) {   // flat: give it a little thickness on both sides and build again
        std::vector<V3> thickPts;
        for (const V3& p : pts) { thickPts.push_back(p + pn * (thick * 0.5f)); thickPts.push_back(p - pn * (thick * 0.5f)); }
        return Build(thickPts);
    }

    struct F { int a, b, c; V3 n; float d; bool alive; };
    std::vector<F> faces;
    auto makeFace = [&](int a, int b, int c) {
        F f{a, b, c, Normalize(Cross(pts[(std::size_t)b] - pts[(std::size_t)a], pts[(std::size_t)c] - pts[(std::size_t)a])), 0.0f, true};
        f.d = Dot(f.n, pts[(std::size_t)a]);
        return f;
    };
    const V3 inner = (pts[i0] + pts[i1] + pts[i2] + pts[i3]) * 0.25f;
    const int t[4] = {(int)i0, (int)i1, (int)i2, (int)i3};
    const int tf[4][3] = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    for (const auto& k : tf) {
        F f = makeFace(t[k[0]], t[k[1]], t[k[2]]);
        if (Dot(f.n, inner) - f.d > 0.0f) f = makeFace(t[k[0]], t[k[2]], t[k[1]]);   // point it outward
        faces.push_back(f);
    }
    std::vector<int> visible;
    std::vector<std::pair<int, int>> horizon;
    for (std::size_t pi = 0; pi < pts.size(); ++pi) {
        if (pi == i0 || pi == i1 || pi == i2 || pi == i3) continue;
        const V3 p = pts[pi];
        visible.clear();
        for (std::size_t f = 0; f < faces.size(); ++f)
            if (faces[f].alive && Dot(faces[f].n, p) - faces[f].d > eps) visible.push_back((int)f);
        if (visible.empty()) continue;
        horizon.clear();
        for (int vf : visible) {
            const F& f = faces[(std::size_t)vf];
            const int e[3][2] = {{f.a, f.b}, {f.b, f.c}, {f.c, f.a}};
            for (const auto& ed : e) {
                bool shared = false;   // is the reverse edge on another visible face?
                for (int of : visible) {
                    if (of == vf) continue;
                    const F& g = faces[(std::size_t)of];
                    if ((g.a == ed[1] && g.b == ed[0]) || (g.b == ed[1] && g.c == ed[0]) || (g.c == ed[1] && g.a == ed[0])) { shared = true; break; }
                }
                if (!shared) horizon.emplace_back(ed[0], ed[1]);
            }
        }
        for (int vf : visible) faces[(std::size_t)vf].alive = false;
        for (const auto& h : horizon) faces.push_back(makeFace(h.first, h.second, (int)pi));
        if (faces.size() > 4 * kMaxInput) {   // keep the face list short: drop dead faces now and then
            std::vector<F> live;
            for (const F& f : faces) if (f.alive) live.push_back(f);
            faces.swap(live);
        }
    }

    // keep only used vertices, in first-use order
    std::vector<int> remap(pts.size(), -1);
    for (const F& f : faces) {
        if (!f.alive) continue;
        for (int v : {f.a, f.b, f.c}) {
            if (remap[(std::size_t)v] < 0) { remap[(std::size_t)v] = (int)out->verts.size(); out->verts.push_back(pts[(std::size_t)v]); }
        }
    }
    for (const F& f : faces) {
        if (!f.alive) continue;
        Tri tr;
        tr.a = (std::uint16_t)remap[(std::size_t)f.a]; tr.b = (std::uint16_t)remap[(std::size_t)f.b]; tr.c = (std::uint16_t)remap[(std::size_t)f.c];
        tr.n = f.n; tr.d = f.d;
        out->tris.push_back(tr);
    }
    // volume and centroid (tetrahedra from the vertex average)
    V3 c0;
    for (const V3& v : out->verts) c0 += v;
    c0 *= 1.0f / (float)out->verts.size();
    float vol = 0.0f;
    V3 m1;
    for (const Tri& tr : out->tris) {
        const V3 a = out->verts[tr.a] - c0, b = out->verts[tr.b] - c0, c = out->verts[tr.c] - c0;
        const float v6 = Dot(a, Cross(b, c));
        vol += v6;
        m1 += (a + b + c) * (v6 * 0.25f);
    }
    out->volume = vol / 6.0f;
    out->centroid = vol > 0.0f ? c0 + m1 * (1.0f / vol) : c0;
    return out;
}

std::shared_ptr<HullData> HullData::FromTriangles(const std::vector<V3>& verts, const std::vector<int>& tris) {
    if (verts.size() < 4 || tris.size() < 12 || verts.size() > 65535) return Build(verts);
    auto out = std::make_shared<HullData>();
    out->verts = verts;
    V3 c0;
    for (const V3& v : verts) c0 += v;
    c0 *= 1.0f / (float)verts.size();
    for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
        int a = tris[i], b = tris[i + 1], c = tris[i + 2];
        if (a < 0 || b < 0 || c < 0 || (std::size_t)a >= verts.size() || (std::size_t)b >= verts.size() || (std::size_t)c >= verts.size()) return Build(verts);
        V3 n = Cross(verts[(std::size_t)b] - verts[(std::size_t)a], verts[(std::size_t)c] - verts[(std::size_t)a]);
        if (!(LenSq(n) > 1e-24f)) continue;   // a sliver: no plane
        if (Dot(n, verts[(std::size_t)a] - c0) < 0.0f) { std::swap(b, c); n = -n; }
        Tri t;
        t.a = (std::uint16_t)a; t.b = (std::uint16_t)b; t.c = (std::uint16_t)c;
        t.n = Normalize(n);
        t.d = Dot(t.n, verts[(std::size_t)a]);
        out->tris.push_back(t);
    }
    if (out->tris.size() < 4) return Build(verts);
    float vol = 0.0f;
    V3 m1;
    for (const Tri& tr : out->tris) {
        const V3 a = verts[tr.a] - c0, b = verts[tr.b] - c0, c = verts[tr.c] - c0;
        const float v6 = Dot(a, Cross(b, c));
        vol += v6;
        m1 += (a + b + c) * (v6 * 0.25f);
    }
    out->volume = vol / 6.0f;
    out->centroid = vol > 0.0f ? c0 + m1 * (1.0f / vol) : c0;
    return out;
}

// ---- support mappings ---------------------------------------------------------------------------------------------------
V3 Shape::Support(const V3& d) const {
    switch (type) {
        case ShapeType::Sphere: return V3{};
        case ShapeType::Box: return V3{Sgn(d.x) * halfExtents.x, Sgn(d.y) * halfExtents.y, Sgn(d.z) * halfExtents.z};
        case ShapeType::Capsule: return V3{0.0f, Sgn(d.y) * halfHeight, 0.0f};
        case ShapeType::Cylinder: {
            const float s = std::sqrt(d.x * d.x + d.z * d.z);
            const float y = Sgn(d.y) * halfHeight;
            if (s > 1e-12f) return V3{radius * d.x / s, y, radius * d.z / s};
            return V3{0.0f, y, 0.0f};
        }
        case ShapeType::Cone: {
            const float H = 2.0f * halfHeight;
            const float sinA = radius / std::sqrt(radius * radius + H * H);
            const float len = Len(d);
            if (d.y > len * sinA) return V3{0.0f, halfHeight, 0.0f};
            const float s = std::sqrt(d.x * d.x + d.z * d.z);
            if (s > 1e-12f) return V3{radius * d.x / s, -halfHeight, radius * d.z / s};
            return V3{0.0f, -halfHeight, 0.0f};
        }
        case ShapeType::Hull: {
            const auto& v = hull->verts;
            std::size_t bi = 0;
            float bd = Dot(v[0], d);
            for (std::size_t i = 1; i < v.size(); ++i) { const float k = Dot(v[i], d); if (k > bd) { bd = k; bi = i; } }
            return v[bi];
        }
        case ShapeType::Triangle: {
            int bi = 0;
            float bd = Dot(tri[0], d);
            for (int i = 1; i < 3; ++i) { const float k = Dot(tri[i], d); if (k > bd) { bd = k; bi = i; } }
            return tri[bi];
        }
        case ShapeType::Mesh: return mesh ? mesh->Bounds().Center() : V3{};
        case ShapeType::Plane: {
            // not a bounded shape: the support is pushed far out (only used if someone asks; the narrow phase special-cases planes)
            V3 t1, t2;
            Basis(planeNormal, t1, t2);
            const float big = 1e6f;
            return planeNormal * planeOffset + t1 * (Dot(d, t1) >= 0 ? big : -big) + t2 * (Dot(d, t2) >= 0 ? big : -big) -
                   planeNormal * (Dot(d, planeNormal) >= 0 ? 0.0f : big);
        }
    }
    return V3{};
}

int Shape::SupportFeature(const V3& dIn, V3* out, V3& normal) const {
    const V3 d = Normalize(dIn);
    normal = d;
    switch (type) {
        case ShapeType::Sphere: out[0] = V3{}; return 1;
        case ShapeType::Capsule:
            if (std::fabs(d.y) < kEdgeSin) { out[0] = V3{0, -halfHeight, 0}; out[1] = V3{0, halfHeight, 0}; return 2; }
            out[0] = V3{0, Sgn(d.y) * halfHeight, 0};
            return 1;
        case ShapeType::Box: {
            int freeAxes = 0;
            bool fr[3];
            for (int k = 0; k < 3; ++k) { fr[k] = std::fabs(d[k]) < kEdgeSin; freeAxes += fr[k]; }
            if (freeAxes == 3) { fr[0] = fr[1] = fr[2] = false; freeAxes = 0; }
            if (freeAxes >= 2) {   // a face: the axis that is not free
                int ax = 0;
                for (int k = 0; k < 3; ++k) if (!fr[k]) ax = k;
                const int u = (ax + 1) % 3, w = (ax + 2) % 3;
                const float s = Sgn(d[ax]);
                const float su[4] = {-1, 1, 1, -1}, sw[4] = {-1, -1, 1, 1};
                for (int i = 0; i < 4; ++i) {
                    V3 p; p[ax] = s * halfExtents[ax]; p[u] = su[i] * halfExtents[u]; p[w] = sw[i] * halfExtents[w];
                    out[i] = p;
                }
                normal = V3{}; normal[ax] = s;
                SortPolygon(out, 4, normal);
                return 4;
            }
            if (freeAxes == 1) {   // an edge along the free axis
                int ax = 0;
                for (int k = 0; k < 3; ++k) if (fr[k]) ax = k;
                V3 p = Support(d);
                p[ax] = -halfExtents[ax]; out[0] = p;
                p[ax] = halfExtents[ax]; out[1] = p;
                return 2;
            }
            out[0] = Support(d);
            return 1;
        }
        case ShapeType::Cylinder: {
            if (std::fabs(d.y) > kFaceCos) {
                const float y = Sgn(d.y) * halfHeight;
                for (int i = 0; i < kRimPoints; ++i) {
                    const float a = 2.0f * kPi * (float)i / (float)kRimPoints;
                    out[i] = V3{radius * std::cos(a), y, radius * std::sin(a)};
                }
                normal = V3{0, Sgn(d.y), 0};
                SortPolygon(out, kRimPoints, normal);
                return kRimPoints;
            }
            if (std::fabs(d.y) < kEdgeSin) {
                const float s = std::sqrt(d.x * d.x + d.z * d.z);
                const V3 r{radius * d.x / s, 0.0f, radius * d.z / s};
                out[0] = r + V3{0, -halfHeight, 0};
                out[1] = r + V3{0, halfHeight, 0};
                return 2;
            }
            out[0] = Support(d);
            return 1;
        }
        case ShapeType::Cone: {
            if (d.y < -kFaceCos) {
                for (int i = 0; i < kRimPoints; ++i) {
                    const float a = 2.0f * kPi * (float)i / (float)kRimPoints;
                    out[i] = V3{radius * std::cos(a), -halfHeight, radius * std::sin(a)};
                }
                normal = V3{0, -1, 0};
                SortPolygon(out, kRimPoints, normal);
                return kRimPoints;
            }
            const float s = std::sqrt(d.x * d.x + d.z * d.z);
            if (s > 1e-6f) {
                const float H = 2.0f * halfHeight;
                const V3 u{d.x / s, 0.0f, d.z / s};
                const V3 side = Normalize(V3{H * u.x, radius, H * u.z});   // outward normal of the slanted side at azimuth u
                if (Dot(side, d) > kFaceCos) {
                    out[0] = V3{0, halfHeight, 0};
                    out[1] = V3{radius * u.x, -halfHeight, radius * u.z};
                    return 2;
                }
            }
            out[0] = Support(d);
            return 1;
        }
        case ShapeType::Hull: {
            const HullData& h = *hull;
            std::size_t bt = 0;
            float bd = -2.0f;
            for (std::size_t i = 0; i < h.tris.size(); ++i) { const float k = Dot(h.tris[i].n, d); if (k > bd) { bd = k; bt = i; } }
            float maxD = -1e30f;
            for (const V3& v : h.verts) maxD = std::max(maxD, Dot(v, d));
            float ext = 0.0f;
            for (const V3& v : h.verts) ext = std::max(ext, Len(v - h.centroid));
            const float tol = 1e-4f * std::max(ext, 1e-3f);
            if (bd > kFaceCos) {
                const HullData::Tri& t = h.tris[bt];
                int n = 0;
                for (const V3& v : h.verts)
                    if (std::fabs(Dot(t.n, v) - t.d) < tol && n < 16) out[n++] = v;
                normal = t.n;
                SortPolygon(out, n, normal);
                return n;
            }
            // an edge or a vertex: the vertices (nearly) as far along d as the support point
            int n = 0;
            const float etol = kEdgeSin * 0.1f * std::max(ext, 1e-3f);
            for (const V3& v : h.verts)
                if (maxD - Dot(v, d) < etol && n < 2) out[n++] = v;
            if (n == 0) { out[0] = Support(d); n = 1; }
            return n;
        }
        case ShapeType::Triangle: {
            V3 n = Normalize(Cross(tri[1] - tri[0], tri[2] - tri[0]));
            const float k = Dot(n, d);
            if (std::fabs(k) > kFaceCos) {
                if (k < 0.0f) n = -n;
                out[0] = tri[0]; out[1] = tri[1]; out[2] = tri[2];
                normal = n;
                SortPolygon(out, 3, normal);
                return 3;
            }
            float maxD = -1e30f;
            for (int i = 0; i < 3; ++i) maxD = std::max(maxD, Dot(tri[i], d));
            const float ext = std::max({Len(tri[1] - tri[0]), Len(tri[2] - tri[1]), Len(tri[0] - tri[2]), 1e-3f});
            int c = 0;
            for (int i = 0; i < 3; ++i)
                if (maxD - Dot(tri[i], d) < kEdgeSin * 0.1f * ext && c < 2) out[c++] = tri[i];
            return c;
        }
        case ShapeType::Plane:
        case ShapeType::Mesh: out[0] = Support(d); return 1;
    }
    out[0] = V3{};
    return 1;
}

Aabb Shape::Bounds(const Xf& x) const {
    const M33 R = M33::FromQ(x.q);
    switch (type) {
        case ShapeType::Sphere: return Aabb{x.p, x.p}.Grown(radius);
        case ShapeType::Box: {
            V3 e;
            for (int k = 0; k < 3; ++k)
                e[k] = std::fabs(R.m[k][0]) * halfExtents.x + std::fabs(R.m[k][1]) * halfExtents.y + std::fabs(R.m[k][2]) * halfExtents.z;
            return {x.p - e, x.p + e};
        }
        case ShapeType::Capsule: {
            const V3 a = R.Col(1) * halfHeight;
            const V3 e = Abs(a) + V3{radius, radius, radius};
            return {x.p - e, x.p + e};
        }
        case ShapeType::Cylinder: {
            const V3 a = R.Col(1);
            V3 e;
            for (int k = 0; k < 3; ++k) e[k] = halfHeight * std::fabs(a[k]) + radius * std::sqrt(std::max(0.0f, 1.0f - a[k] * a[k]));
            return {x.p - e, x.p + e};
        }
        case ShapeType::Plane: {
            const V3 big{1e6f, 1e6f, 1e6f};
            return {x.p - big, x.p + big};
        }
        case ShapeType::Mesh: {
            if (!mesh) return Aabb{x.p, x.p};
            const Aabb lb = mesh->Bounds();
            const V3 c = x.Apply(lb.Center()), h = (lb.hi - lb.lo) * 0.5f;
            V3 e;
            for (int k = 0; k < 3; ++k) e[k] = std::fabs(R.m[k][0]) * h.x + std::fabs(R.m[k][1]) * h.y + std::fabs(R.m[k][2]) * h.z;
            return {c - e, c + e};
        }
        default: {   // cone, hull, triangle: the support along each world axis
            Aabb b;
            for (int k = 0; k < 3; ++k) {
                V3 e; e[k] = 1.0f;
                const V3 hiP = x.Apply(Support(InvRotate(x.q, e)));
                const V3 loP = x.Apply(Support(InvRotate(x.q, -e)));
                b.hi[k] = hiP[k];
                b.lo[k] = loP[k];
            }
            return b;
        }
    }
}

MassProps Shape::Mass(float density) const {
    MassProps mp;
    switch (type) {
        case ShapeType::Sphere: {
            mp.mass = density * 4.0f / 3.0f * kPi * radius * radius * radius;
            const float i = 0.4f * mp.mass * radius * radius;
            mp.inertia = M33::Diagonal(V3{i, i, i});
            break;
        }
        case ShapeType::Box: {
            const V3 h = halfExtents;
            mp.mass = density * 8.0f * h.x * h.y * h.z;
            const float k = mp.mass / 3.0f;
            mp.inertia = M33::Diagonal(V3{k * (h.y * h.y + h.z * h.z), k * (h.x * h.x + h.z * h.z), k * (h.x * h.x + h.y * h.y)});
            break;
        }
        case ShapeType::Capsule: {
            const float r = radius, h = halfHeight;
            const float mc = density * kPi * r * r * 2.0f * h;
            const float ms = density * 4.0f / 3.0f * kPi * r * r * r;
            mp.mass = mc + ms;
            const float iy = mc * r * r * 0.5f + ms * 0.4f * r * r;
            const float ix = mc * (r * r / 4.0f + (2.0f * h) * (2.0f * h) / 12.0f) + ms * (0.4f * r * r + h * h + 0.75f * h * r);
            mp.inertia = M33::Diagonal(V3{ix, iy, ix});
            break;
        }
        case ShapeType::Cylinder: {
            const float r = radius, H = 2.0f * halfHeight;
            mp.mass = density * kPi * r * r * H;
            const float iy = mp.mass * r * r * 0.5f;
            const float ix = mp.mass * (3.0f * r * r + H * H) / 12.0f;
            mp.inertia = M33::Diagonal(V3{ix, iy, ix});
            break;
        }
        case ShapeType::Cone: {
            const float r = radius, H = 2.0f * halfHeight;
            mp.mass = density * kPi * r * r * H / 3.0f;
            mp.com = V3{0.0f, -halfHeight + H * 0.25f, 0.0f};   // a quarter of the height above the base
            const float iy = 0.3f * mp.mass * r * r;
            const float ix = mp.mass * (3.0f / 20.0f * r * r + 3.0f / 80.0f * H * H);
            mp.inertia = M33::Diagonal(V3{ix, iy, ix});
            break;
        }
        case ShapeType::Hull: {
            const HullData& h = *hull;
            const V3 c0 = h.centroid;   // any inside point works; the centroid keeps the numbers small
            M33 cov = M33::Zero();
            float vol = 0.0f;
            for (const HullData::Tri& t : h.tris) {
                const V3 a = h.verts[t.a] - c0, b = h.verts[t.b] - c0, c = h.verts[t.c] - c0;
                const float det = Dot(a, Cross(b, c));
                vol += det / 6.0f;
                // A * canon * A^T * det, A = [a b c] as columns
                const V3 col[3] = {a, b, c};
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) {
                        float s = 0.0f;
                        for (int p = 0; p < 3; ++p)
                            for (int q = 0; q < 3; ++q) s += col[p][i] * kCanon[p][q] * col[q][j];
                        cov.m[i][j] += s * det;
                    }
            }
            mp.mass = density * vol;
            mp.com = c0;   // (the covariance above is about the centroid, which is the centre of mass)
            mp.inertia = InertiaFromCovariance(cov * density);
            break;
        }
        case ShapeType::Triangle:
        case ShapeType::Plane:
        case ShapeType::Mesh:
            mp.mass = 0.0f;
            mp.inertia = M33::Zero();
            break;
    }
    return mp;
}

float Shape::OuterRadius() const {
    switch (type) {
        case ShapeType::Sphere: return radius;
        case ShapeType::Box: return Len(halfExtents);
        case ShapeType::Capsule: return halfHeight + radius;
        case ShapeType::Cylinder:
        case ShapeType::Cone: return std::sqrt(halfHeight * halfHeight + radius * radius);
        case ShapeType::Hull: { float m = 0.0f; for (const V3& v : hull->verts) m = std::max(m, Len(v)); return m; }
        case ShapeType::Triangle: return std::max({Len(tri[0]), Len(tri[1]), Len(tri[2])});
        case ShapeType::Plane: return 1e6f;
        case ShapeType::Mesh: {
            if (!mesh) return 0.0f;
            const Aabb b = mesh->Bounds();
            return Len(Max(Abs(b.lo), Abs(b.hi)));
        }
    }
    return 0.0f;
}

float Shape::InnerRadius() const {
    switch (type) {
        case ShapeType::Sphere: return radius;
        case ShapeType::Box: return std::min({halfExtents.x, halfExtents.y, halfExtents.z});
        case ShapeType::Capsule: return radius;
        case ShapeType::Cylinder: return std::min(radius, halfHeight);
        case ShapeType::Cone: return 0.5f * std::min(radius, halfHeight);
        case ShapeType::Hull: {
            float m = 1e30f;
            for (const HullData::Tri& t : hull->tris) m = std::min(m, t.d - Dot(t.n, hull->centroid));
            return std::max(m, 0.0f);
        }
        case ShapeType::Triangle:
        case ShapeType::Plane:
        case ShapeType::Mesh: return 0.0f;
    }
    return 0.0f;
}

} // namespace phys
} // namespace okay
