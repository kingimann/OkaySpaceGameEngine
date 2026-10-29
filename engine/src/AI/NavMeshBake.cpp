#include "okay/AI/NavMeshBake.hpp"

#include "okay/Components/Character.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/Projectile3D.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <tuple>

namespace okay {

// ---- Heightfield copy ---------------------------------------------------------

float NavBakeInput::Heightfield::Sample(float lx, float lz) const {
    if (dim < 2) return 0.0f;
    const float cell = size / (float)(dim - 1), half = size * 0.5f;
    const float gx = (lx + half) / cell, gz = (lz + half) / cell;
    const int x0 = (int)std::floor(gx), z0 = (int)std::floor(gz);
    const float fx = gx - x0, fz = gz - z0;
    const float h00 = Height(x0, z0), h10 = Height(x0 + 1, z0), h01 = Height(x0, z0 + 1), h11 = Height(x0 + 1, z0 + 1);
    return (h00 * (1 - fx) + h10 * fx) * (1 - fz) + (h01 * (1 - fx) + h11 * fx) * fz;
}

Vec3 NavBakeInput::Heightfield::Normal(float lx, float lz) const {
    if (dim < 2) return Vec3{0, 1, 0};
    const float e = size / (float)(dim - 1);
    const float hl = Sample(lx - e, lz), hr = Sample(lx + e, lz), hd = Sample(lx, lz - e), hu = Sample(lx, lz + e);
    Vec3 n{hl - hr, 2.0f * e, hd - hu};
    const float m = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return m > 1e-6f ? n * (1.0f / m) : Vec3{0, 1, 0};
}

// ---- Gather ------------------------------------------------------------------

bool NavMeshBaker::IsMovingThing(const GameObject* go) {
    for (const GameObject* g = go; g; g = (g->transform && g->transform->Parent()) ? g->transform->Parent()->gameObject : nullptr) {
        if (g->GetComponent<NavMeshAgent>() || g->GetComponent<NavMeshObstacle>() ||
            g->GetComponent<NPCController>() || g->GetComponent<ClickToMoveController>() ||
            g->GetComponent<Character>() || g->GetComponent<FirstPersonController>() ||
            g->GetComponent<ThirdPersonController>() || g->GetComponent<ThirdPersonShooterController>() ||
            g->GetComponent<TopDownController>() || g->GetComponent<TopDownShooterController>() ||
            g->GetComponent<CharacterController3D>() || g->GetComponent<Platformer25DController>() ||
            g->GetComponent<FreeRoamController>() || g->GetComponent<VehicleController>() ||
            g->GetComponent<Projectile3D>())
            return true;
    }
    return false;
}

void NavMeshBaker::Gather(Scene& scene, NavBakeInput& out, bool includeDynamic, std::uint32_t layerMask) {
    out.boxes.clear(); out.spheres.clear(); out.capsules.clear(); out.terrains.clear();
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || !go->transform) continue;
        if (((layerMask >> (go->layer & 31)) & 1u) == 0) continue;
        if (IsMovingThing(go)) continue;
        if (auto* rb = go->GetComponent<Rigidbody3D>())
            if (rb->bodyType == Rigidbody3D::BodyType::Dynamic && !includeDynamic) continue;
        for (const auto& comp : go->Components()) {
            auto* c = dynamic_cast<Collider3D*>(comp.get());
            if (!c || !c->enabled || c->isTrigger) continue;
            switch (c->shape()) {
                case Collider3D::Shape::Box:
                case Collider3D::Shape::Mesh: {
                    auto* b = static_cast<BoxCollider3D*>(c);
                    out.boxes.push_back({b->WorldCenter(), b->HalfExtents(), go->transform->Rotation()});
                    break;
                }
                case Collider3D::Shape::Sphere: {
                    auto* s = static_cast<SphereCollider3D*>(c);
                    out.spheres.push_back({s->WorldCenter(), s->WorldRadius()});
                    break;
                }
                case Collider3D::Shape::Capsule:
                case Collider3D::Shape::Cylinder: {
                    auto* cp = static_cast<CapsuleCollider3D*>(c);
                    NavBakeInput::Capsule k; cp->Segment(k.a, k.b); k.radius = cp->WorldRadius();
                    out.capsules.push_back(k);
                    break;
                }
            }
        }
    }
    for (Terrain* t : scene.FindObjectsOfType<Terrain>()) {
        if (!t || !t->enabled || !t->gameObject || !t->gameObject->active || !t->gameObject->transform) continue;
        if (((layerMask >> (t->gameObject->layer & 31)) & 1u) == 0) continue;
        NavBakeInput::Heightfield h;
        h.l2w = t->gameObject->transform->LocalToWorldMatrix();
        h.w2l = h.l2w.Inverse();
        h.size = t->size; h.dim = t->Dim(); h.heights = t->heights;
        if ((int)h.heights.size() < h.dim * h.dim) h.heights.resize((std::size_t)h.dim * h.dim, 0.0f);
        float lo = 0.0f, hi = 0.0f; t->HeightRange(lo, hi);
        const float half = t->size * 0.5f;
        h.aabbMin = Vec3{1e30f, 1e30f, 1e30f}; h.aabbMax = Vec3{-1e30f, -1e30f, -1e30f};
        for (int k = 0; k < 8; ++k) {
            Vec3 w = h.l2w.MultiplyPoint({(k & 1) ? half : -half, (k & 2) ? hi : lo, (k & 4) ? half : -half});
            h.aabbMin.x = std::min(h.aabbMin.x, w.x); h.aabbMax.x = std::max(h.aabbMax.x, w.x);
            h.aabbMin.y = std::min(h.aabbMin.y, w.y); h.aabbMax.y = std::max(h.aabbMax.y, w.y);
            h.aabbMin.z = std::min(h.aabbMin.z, w.z); h.aabbMax.z = std::max(h.aabbMax.z, w.z);
        }
        out.terrains.push_back(std::move(h));
    }
}

// ---- Bake --------------------------------------------------------------------

namespace {

struct Span { float y0, y1, ny; int next; };   // a solid interval of a column, with its top's normal

struct Heightfield {
    int nx = 0, nz = 0;
    float cs = 0.25f;
    float originX = 0.0f, originZ = 0.0f;
    std::vector<int> head;     // per column: first span (-1 = none), kept sorted by y0
    std::vector<Span> spans;
    float CenterX(int ix) const { return originX + ((float)ix + 0.5f) * cs; }
    float CenterZ(int iz) const { return originZ + ((float)iz + 0.5f) * cs; }
    void AddSpan(int col, float y0, float y1, float ny) {
        if (!(y1 >= y0)) return;
        // Merge with every overlapping span of the column (the top's normal follows the highest top).
        int prev = -1, cur = head[(std::size_t)col];
        while (cur >= 0 && spans[(std::size_t)cur].y0 <= y1 + 1e-4f) {
            Span& s = spans[(std::size_t)cur];
            if (s.y1 >= y0 - 1e-4f) {   // overlap / touch: absorb into the new interval, unlink
                if (s.y1 > y1) { y1 = s.y1; ny = s.ny; }
                if (s.y0 < y0) y0 = s.y0;
                const int nxt = s.next;
                if (prev >= 0) spans[(std::size_t)prev].next = nxt; else head[(std::size_t)col] = nxt;
                cur = nxt;
                continue;
            }
            prev = cur; cur = s.next;
        }
        Span n{y0, y1, ny, cur};
        spans.push_back(n);
        const int id = (int)spans.size() - 1;
        if (prev >= 0) spans[(std::size_t)prev].next = id; else head[(std::size_t)col] = id;
    }
};

inline void Expand(Vec3& mn, Vec3& mx, const Vec3& p) {
    mn.x = std::min(mn.x, p.x); mx.x = std::max(mx.x, p.x);
    mn.y = std::min(mn.y, p.y); mx.y = std::max(mx.y, p.y);
    mn.z = std::min(mn.z, p.z); mx.z = std::max(mx.z, p.z);
}

void BoxAABB(const NavBakeInput::Box& b, Vec3& mn, Vec3& mx) {
    mn = Vec3{1e30f, 1e30f, 1e30f}; mx = Vec3{-1e30f, -1e30f, -1e30f};
    for (int k = 0; k < 8; ++k) {
        Vec3 l{(k & 1) ? b.half.x : -b.half.x, (k & 2) ? b.half.y : -b.half.y, (k & 4) ? b.half.z : -b.half.z};
        Expand(mn, mx, b.center + b.rot * l);
    }
}

// The vertical line through (x, z) against an oriented box: the y interval it is
// inside and the y component of the normal of the face it leaves through (the top).
bool LineBox(const NavBakeInput::Box& b, const Quat& inv, float x, float z, float& y0, float& y1, float& ny) {
    const Vec3 p = inv * Vec3{x - b.center.x, -b.center.y, z - b.center.z};   // world (x, 0, z) in box space
    const Vec3 d = inv * Vec3{0, 1, 0};
    const float pp[3] = {p.x, p.y, p.z}, dd[3] = {d.x, d.y, d.z}, hh[3] = {b.half.x, b.half.y, b.half.z};
    float tmin = -1e30f, tmax = 1e30f;
    int exitAxis = -1; float exitSign = 1.0f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-6f) {
            if (std::fabs(pp[i]) > hh[i]) return false;
            continue;
        }
        float t0 = (-hh[i] - pp[i]) / dd[i], t1 = (hh[i] - pp[i]) / dd[i];
        float sign = dd[i] > 0.0f ? 1.0f : -1.0f;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) { tmax = t1; exitAxis = i; exitSign = sign; }
    }
    if (tmin > tmax || exitAxis < 0) return false;
    Vec3 nl{0, 0, 0};
    if (exitAxis == 0) nl.x = exitSign; else if (exitAxis == 1) nl.y = exitSign; else nl.z = exitSign;
    const Vec3 nw = b.rot * nl;
    y0 = tmin; y1 = tmax; ny = nw.y;
    return true;
}

struct Cell {
    float y = 0.0f, ny = 1.0f, ceil = 1e30f;
    bool  walk = false;
    int   con[4] = {-1, -1, -1, -1};   // neighbour cell per direction (+x, -x, +z, -z) or -1
    int   rect = -1;
    int   dist = 0;
};

constexpr int kDX[4] = {1, -1, 0, 0};
constexpr int kDZ[4] = {0, 0, 1, -1};

struct CellRect { int ix0, iz0, w, h; std::vector<int> cells; /* row-major, h rows of w */ 
    int at(int r, int c) const { return cells[(std::size_t)r * w + c]; } };

} // namespace

bool NavMeshBaker::Bake(const NavBakeInput& in, const NavMeshBuildSettings& settingsIn, NavMesh& out,
                        std::atomic<float>* progress, std::atomic<bool>* cancel, NavBakeStats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    auto report = [&](float f) { if (progress) progress->store(f); };
    auto stop = [&]() { return cancel && cancel->load(); };
    NavBakeStats st;
    out.Clear();
    NavMeshBuildSettings S = settingsIn;
    S.cellSize = std::max(S.cellSize, 0.05f);
    S.cellHeight = std::max(S.cellHeight, 0.01f);
    S.agentRadius = std::max(S.agentRadius, 0.0f);
    S.agentHeight = std::max(S.agentHeight, 0.1f);
    S.maxStep = std::max(S.maxStep, 0.0f);
    S.maxSlope = std::clamp(S.maxSlope, 0.0f, 89.0f);
    out.settings = S;

    // ---- Bounds: the bake area --------------------------------------------
    Vec3 bmin{1e30f, 1e30f, 1e30f}, bmax{-1e30f, -1e30f, -1e30f};
    if (in.hasBounds) { bmin = in.boundsMin; bmax = in.boundsMax; }
    else {
        for (const auto& b : in.boxes) { Vec3 mn, mx; BoxAABB(b, mn, mx); Expand(bmin, bmax, mn); Expand(bmin, bmax, mx); }
        for (const auto& s : in.spheres) { Expand(bmin, bmax, s.center - Vec3{s.radius, s.radius, s.radius}); Expand(bmin, bmax, s.center + Vec3{s.radius, s.radius, s.radius}); }
        for (const auto& c : in.capsules) {
            const Vec3 r{c.radius, c.radius, c.radius};
            Expand(bmin, bmax, c.a - r); Expand(bmin, bmax, c.a + r); Expand(bmin, bmax, c.b - r); Expand(bmin, bmax, c.b + r);
        }
        for (const auto& t : in.terrains) { Expand(bmin, bmax, t.aabbMin); Expand(bmin, bmax, t.aabbMax); }
        if (bmin.x > bmax.x) { if (stats) *stats = st; report(1.0f); return true; }   // nothing to bake
        const float pad = S.cellSize;
        bmin -= Vec3{pad, S.agentHeight, pad}; bmax += Vec3{pad, S.agentHeight, pad};
    }
    const float cs = S.cellSize;
    const double colsX = std::ceil((bmax.x - bmin.x) / cs), colsZ = std::ceil((bmax.z - bmin.z) / cs);
    if (colsX < 1 || colsZ < 1) { if (stats) *stats = st; report(1.0f); return true; }
    if (colsX * colsZ > 20000000.0) return false;   // 20M columns: raise the cell size or shrink the bounds
    Heightfield hf;
    hf.nx = (int)colsX; hf.nz = (int)colsZ; hf.cs = cs;
    hf.originX = bmin.x; hf.originZ = bmin.z;
    hf.head.assign((std::size_t)hf.nx * hf.nz, -1);
    st.columns = hf.nx * hf.nz;
    out.boundsMin = bmin; out.boundsMax = bmax;

    auto colRange = [&](const Vec3& mn, const Vec3& mx, int& x0, int& z0, int& x1, int& z1) {
        x0 = std::clamp((int)std::floor((mn.x - hf.originX) / cs), 0, hf.nx - 1);
        x1 = std::clamp((int)std::floor((mx.x - hf.originX) / cs), 0, hf.nx - 1);
        z0 = std::clamp((int)std::floor((mn.z - hf.originZ) / cs), 0, hf.nz - 1);
        z1 = std::clamp((int)std::floor((mx.z - hf.originZ) / cs), 0, hf.nz - 1);
        return mn.x <= bmax.x && mx.x >= bmin.x && mn.z <= bmax.z && mx.z >= bmin.z;
    };

    // ---- Rasterize: solid spans per column --------------------------------
    const std::size_t prims = in.boxes.size() + in.spheres.size() + in.capsules.size() + in.terrains.size();
    std::size_t done = 0;
    auto tick = [&]() { if ((++done & 15) == 0) report(0.4f * (float)done / (float)std::max<std::size_t>(prims, 1)); };
    for (const auto& b : in.boxes) {
        if (stop()) return false;
        Vec3 mn, mx; BoxAABB(b, mn, mx);
        int x0, z0, x1, z1; if (!colRange(mn, mx, x0, z0, x1, z1)) { tick(); continue; }
        const Quat inv = b.rot.Conjugate();
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix) {
                float y0, y1, ny;
                if (LineBox(b, inv, hf.CenterX(ix), hf.CenterZ(iz), y0, y1, ny)) hf.AddSpan(iz * hf.nx + ix, y0, y1, ny);
            }
        tick();
    }
    for (const auto& s : in.spheres) {
        if (stop()) return false;
        const Vec3 r{s.radius, s.radius, s.radius};
        int x0, z0, x1, z1; if (!colRange(s.center - r, s.center + r, x0, z0, x1, z1)) { tick(); continue; }
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix) {
                const float dx = hf.CenterX(ix) - s.center.x, dz = hf.CenterZ(iz) - s.center.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 >= s.radius * s.radius) continue;
                const float h = std::sqrt(s.radius * s.radius - d2);
                hf.AddSpan(iz * hf.nx + ix, s.center.y - h, s.center.y + h, h / std::max(s.radius, 1e-6f));
            }
        tick();
    }
    for (const auto& c : in.capsules) {
        if (stop()) return false;
        const Vec3 r{c.radius, c.radius, c.radius};
        Vec3 mn = c.a, mx = c.a; Expand(mn, mx, c.b); mn -= r; mx += r;
        int x0, z0, x1, z1; if (!colRange(mn, mx, x0, z0, x1, z1)) { tick(); continue; }
        Vec3 u = c.b - c.a; const float len = u.Magnitude();
        if (len > 1e-6f) u = u * (1.0f / len);
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix) {
                const float x = hf.CenterX(ix), z = hf.CenterZ(iz);
                const int col = iz * hf.nx + ix;
                // End caps as spheres.
                for (const Vec3* e : {&c.a, &c.b}) {
                    const float dx = x - e->x, dz = z - e->z, d2 = dx * dx + dz * dz;
                    if (d2 >= c.radius * c.radius) continue;
                    const float h = std::sqrt(c.radius * c.radius - d2);
                    hf.AddSpan(col, e->y - h, e->y + h, h / std::max(c.radius, 1e-6f));
                }
                if (len <= 1e-6f) continue;
                // The cylinder part: points (x, y, z) within `radius` of the segment's line,
                // with the projection parameter inside the segment.
                const Vec3 w0{x - c.a.x, -c.a.y, z - c.a.z};
                const float s0 = Vec3::Dot(w0, u);
                const float A = 1.0f - u.y * u.y, B = 2.0f * (w0.y - s0 * u.y), C = w0.SqrMagnitude() - s0 * s0 - c.radius * c.radius;
                float lo, hi;
                if (A < 1e-6f) { if (C > 0.0f) continue; lo = -1e30f; hi = 1e30f; }
                else {
                    const float disc = B * B - 4.0f * A * C;
                    if (disc <= 0.0f) continue;
                    const float sq = std::sqrt(disc);
                    lo = (-B - sq) / (2.0f * A); hi = (-B + sq) / (2.0f * A);
                }
                if (std::fabs(u.y) > 1e-6f) {
                    float sa = (0.0f - s0) / u.y, sb = (len - s0) / u.y;
                    if (sa > sb) std::swap(sa, sb);
                    lo = std::max(lo, sa); hi = std::min(hi, sb);
                } else if (s0 < 0.0f || s0 > len) continue;
                if (lo > hi) continue;
                const float sTop = std::clamp(s0 + hi * u.y, 0.0f, len);
                const Vec3 closest = c.a + u * sTop;
                hf.AddSpan(col, lo, hi, std::clamp((hi - closest.y) / std::max(c.radius, 1e-6f), -1.0f, 1.0f));
            }
        tick();
    }
    for (const auto& t : in.terrains) {
        if (stop()) return false;
        int x0, z0, x1, z1; if (!colRange(t.aabbMin, t.aabbMax, x0, z0, x1, z1)) { tick(); continue; }
        const float half = t.size * 0.5f;
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix) {
                const Vec3 lp = t.w2l.MultiplyPoint({hf.CenterX(ix), 0.0f, hf.CenterZ(iz)});
                if (lp.x < -half || lp.x > half || lp.z < -half || lp.z > half) continue;
                const float h = t.Sample(lp.x, lp.z);
                const float yw = t.l2w.MultiplyPoint({lp.x, h, lp.z}).y;
                const Vec3 n = t.l2w.MultiplyVector(t.Normal(lp.x, lp.z)).Normalized();
                hf.AddSpan(iz * hf.nx + ix, yw - 1000.0f, yw, n.y);
            }
        tick();
    }
    report(0.4f);

    // ---- Floor cells: span tops with head room ------------------------------
    const std::size_t ncol = (std::size_t)hf.nx * hf.nz;
    std::vector<int> colStart(ncol + 1, 0);
    std::vector<Cell> cells;
    cells.reserve(ncol);
    const float minSlopeNy = std::cos(S.maxSlope * 3.14159265f / 180.0f) - 1e-4f;
    for (std::size_t col = 0; col < ncol; ++col) {
        colStart[col] = (int)cells.size();
        for (int si = hf.head[col]; si >= 0; si = hf.spans[(std::size_t)si].next) {
            const Span& sp = hf.spans[(std::size_t)si];
            const float ceil = sp.next >= 0 ? hf.spans[(std::size_t)sp.next].y0 : 1e30f;
            if (ceil - sp.y1 < S.agentHeight - 1e-4f) continue;
            if (sp.y1 < bmin.y - 1e-4f || sp.y1 > bmax.y + 1e-4f) continue;
            Cell c; c.y = sp.y1; c.ny = sp.ny; c.ceil = ceil;
            c.walk = sp.ny >= minSlopeNy;
            cells.push_back(c);
        }
    }
    colStart[ncol] = (int)cells.size();
    st.cells = (int)cells.size();
    hf.spans.clear(); hf.spans.shrink_to_fit();
    if (stop()) return false;

    // ---- Connectivity: neighbours within the step height, sharing head room -----
    auto nearestInColumn = [&](std::size_t col, float y) -> int {
        int best = -1; float bd = 1e30f;
        for (int k = colStart[col]; k < colStart[col + 1]; ++k) {
            const float d = std::fabs(cells[(std::size_t)k].y - y);
            if (d < bd) { bd = d; best = k; }
        }
        return best;
    };
    auto connect = [&]() {
        for (int iz = 0; iz < hf.nz; ++iz)
            for (int ix = 0; ix < hf.nx; ++ix) {
                const std::size_t col = (std::size_t)iz * hf.nx + ix;
                for (int k = colStart[col]; k < colStart[col + 1]; ++k) {
                    Cell& c = cells[(std::size_t)k];
                    for (int d = 0; d < 4; ++d) {
                        c.con[d] = -1;
                        if (!c.walk) continue;
                        const int nx2 = ix + kDX[d], nz2 = iz + kDZ[d];
                        if (nx2 < 0 || nz2 < 0 || nx2 >= hf.nx || nz2 >= hf.nz) continue;
                        const int n = nearestInColumn((std::size_t)nz2 * hf.nx + nx2, c.y);
                        if (n < 0) continue;
                        const Cell& o = cells[(std::size_t)n];
                        if (!o.walk || std::fabs(o.y - c.y) > S.maxStep + 1e-4f) continue;
                        if (std::min(c.ceil, o.ceil) - std::max(c.y, o.y) < S.agentHeight - 1e-4f) continue;
                        c.con[d] = n;
                    }
                }
            }
    };
    connect();
    report(0.45f);

    // ---- Erode by the agent radius (chamfer distance from every boundary) ------
    if (S.agentRadius > 0.0f && !cells.empty()) {
        const int INF = 1 << 28;
        for (Cell& c : cells) {
            if (!c.walk) { c.dist = 0; continue; }
            c.dist = (c.con[0] < 0 || c.con[1] < 0 || c.con[2] < 0 || c.con[3] < 0) ? 0 : INF;
        }
        auto relax = [&](int k, int dir, int diagDir) {
            Cell& c = cells[(std::size_t)k];
            const int n = c.con[dir];
            if (n < 0) return;
            c.dist = std::min(c.dist, cells[(std::size_t)n].dist + 2);
            const int nn = cells[(std::size_t)n].con[diagDir];
            if (nn >= 0) c.dist = std::min(c.dist, cells[(std::size_t)nn].dist + 3);
        };
        // Pass 1: -x, -z (and their diagonals); pass 2: +x, +z.
        for (int iz = 0; iz < hf.nz; ++iz)
            for (int ix = 0; ix < hf.nx; ++ix) {
                const std::size_t col = (std::size_t)iz * hf.nx + ix;
                for (int k = colStart[col]; k < colStart[col + 1]; ++k) {
                    if (!cells[(std::size_t)k].walk) continue;
                    relax(k, 1, 3); relax(k, 3, 0);   // (-x, -x-z) and (-z, +x-z)
                }
            }
        for (int iz = hf.nz - 1; iz >= 0; --iz)
            for (int ix = hf.nx - 1; ix >= 0; --ix) {
                const std::size_t col = (std::size_t)iz * hf.nx + ix;
                for (int k = colStart[col]; k < colStart[col + 1]; ++k) {
                    if (!cells[(std::size_t)k].walk) continue;
                    relax(k, 0, 2); relax(k, 2, 1);   // (+x, +x+z) and (+z, -x+z)
                }
            }
        // A cell's chamfer distance counts half cells from the nearest boundary cell
        // centre; its near edge is that far from the wall, and must stay a radius away.
        for (Cell& c : cells)
            if (c.walk && (float)c.dist * 0.5f * cs < S.agentRadius - 1e-4f) c.walk = false;
        connect();
    }
    for (const Cell& c : cells) if (c.walk) ++st.walkable;
    report(0.55f);
    if (stop()) return false;

    // ---- Merge cells into rectangles (convex polygons) -------------------------
    const float tol = S.cellHeight;
    std::vector<CellRect> rects;
    auto free = [&](int k) { return k >= 0 && cells[(std::size_t)k].walk && cells[(std::size_t)k].rect < 0; };
    // Can the rectangle grow by a column on the +x side? Fills `col` with the new cells.
    auto growX = [&](const CellRect& r, std::vector<int>& col) {
        col.clear();
        for (int row = 0; row < r.h; ++row) {
            const int k = cells[(std::size_t)r.at(row, r.w - 1)].con[0];
            if (!free(k)) return false;
            if (row > 0 && cells[(std::size_t)col[(std::size_t)row - 1]].con[2] != k) return false;
            col.push_back(k);
        }
        // Same profile along z as the first column, and a straight line along x.
        const float base = cells[(std::size_t)col[0]].y, base0 = cells[(std::size_t)r.at(0, 0)].y;
        for (int row = 1; row < r.h; ++row)
            if (std::fabs((cells[(std::size_t)col[(std::size_t)row]].y - base) - (cells[(std::size_t)r.at(row, 0)].y - base0)) > tol) return false;
        if (r.w >= 2) {
            const float y1 = cells[(std::size_t)r.at(0, r.w - 1)].y, y2 = cells[(std::size_t)r.at(0, r.w - 2)].y;
            if (std::fabs(base - (y1 + (y1 - y2))) > tol) return false;
        }
        return true;
    };
    auto growZ = [&](const CellRect& r, std::vector<int>& row) {
        row.clear();
        for (int c = 0; c < r.w; ++c) {
            const int k = cells[(std::size_t)r.at(r.h - 1, c)].con[2];
            if (!free(k)) return false;
            if (c > 0 && cells[(std::size_t)row[(std::size_t)c - 1]].con[0] != k) return false;
            row.push_back(k);
        }
        const float base = cells[(std::size_t)row[0]].y, base0 = cells[(std::size_t)r.at(0, 0)].y;
        for (int c = 1; c < r.w; ++c)
            if (std::fabs((cells[(std::size_t)row[(std::size_t)c]].y - base) - (cells[(std::size_t)r.at(0, c)].y - base0)) > tol) return false;
        if (r.h >= 2) {
            const float y1 = cells[(std::size_t)r.at(r.h - 1, 0)].y, y2 = cells[(std::size_t)r.at(r.h - 2, 0)].y;
            if (std::fabs(base - (y1 + (y1 - y2))) > tol) return false;
        }
        return true;
    };
    auto addCol = [&](CellRect& r, const std::vector<int>& col) {
        std::vector<int> n; n.reserve((std::size_t)(r.w + 1) * r.h);
        for (int row = 0; row < r.h; ++row) {
            for (int c = 0; c < r.w; ++c) n.push_back(r.at(row, c));
            n.push_back(col[(std::size_t)row]);
        }
        r.cells.swap(n); ++r.w;
    };
    auto addRow = [&](CellRect& r, const std::vector<int>& row) { r.cells.insert(r.cells.end(), row.begin(), row.end()); ++r.h; };
    std::vector<int> tmp;
    auto grow = [&](CellRect r, bool xFirst) {
        if (xFirst) { while (growX(r, tmp)) addCol(r, tmp); while (growZ(r, tmp)) addRow(r, tmp); }
        else        { while (growZ(r, tmp)) addRow(r, tmp); while (growX(r, tmp)) addCol(r, tmp); }
        return r;
    };
    for (int iz = 0; iz < hf.nz; ++iz) {
        if (stop()) return false;
        if ((iz & 31) == 0) report(0.55f + 0.3f * (float)iz / (float)hf.nz);
        for (int ix = 0; ix < hf.nx; ++ix) {
            const std::size_t col = (std::size_t)iz * hf.nx + ix;
            for (int k = colStart[col]; k < colStart[col + 1]; ++k) {
                if (!free(k)) continue;
                CellRect seed; seed.ix0 = ix; seed.iz0 = iz; seed.w = 1; seed.h = 1; seed.cells = {k};
                CellRect a = grow(seed, true), b = grow(seed, false);
                CellRect& best = (a.w * a.h >= b.w * b.h) ? a : b;
                const int id = (int)rects.size();
                for (int c : best.cells) cells[(std::size_t)c].rect = id;
                rects.push_back(std::move(best));
            }
        }
    }
    report(0.85f);

    // ---- Polygons: plane-fitted corner heights, de-duplicated vertices ----------
    struct Plane { float a, b, c, mx, mz; float At(float x, float z) const { return a + b * (x - mx) + c * (z - mz); } };
    std::vector<Plane> planes(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const CellRect& r = rects[i];
        const float mx = hf.originX + ((float)r.ix0 + (float)r.w * 0.5f) * cs;
        const float mz = hf.originZ + ((float)r.iz0 + (float)r.h * 0.5f) * cs;
        double sy = 0, sxx = 0, szz = 0, sxz = 0, sxy = 0, szy = 0;
        for (int row = 0; row < r.h; ++row)
            for (int c = 0; c < r.w; ++c) {
                const double x = hf.CenterX(r.ix0 + c) - mx, z = hf.CenterZ(r.iz0 + row) - mz, y = cells[(std::size_t)r.at(row, c)].y;
                sy += y; sxx += x * x; szz += z * z; sxz += x * z; sxy += x * y; szy += z * y;
            }
        const double n = (double)r.w * r.h;
        Plane p; p.mx = mx; p.mz = mz; p.a = (float)(sy / n); p.b = 0.0f; p.c = 0.0f;
        const double det = sxx * szz - sxz * sxz;
        if (det > 1e-9) { p.b = (float)((sxy * szz - szy * sxz) / det); p.c = (float)((szy * sxx - sxy * sxz) / det); }
        else { if (sxx > 1e-9) p.b = (float)(sxy / sxx); if (szz > 1e-9) p.c = (float)(szy / szz); }
        planes[i] = p;
    }
    std::map<std::tuple<int, int, int>, int> vertexIds;
    auto vertex = [&](int bx, int bz, float y) {
        const auto key = std::make_tuple(bx, bz, (int)std::lround(y * 1000.0f));
        auto it = vertexIds.find(key);
        if (it != vertexIds.end()) return it->second;
        const int id = (int)out.vertices.size();
        out.vertices.push_back(Vec3{hf.originX + (float)bx * cs, std::round(y * 1000.0f) / 1000.0f, hf.originZ + (float)bz * cs});
        vertexIds.emplace(key, id);
        return id;
    };
    out.polys.resize(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const CellRect& r = rects[i]; const Plane& p = planes[i];
        const int bx0 = r.ix0, bx1 = r.ix0 + r.w, bz0 = r.iz0, bz1 = r.iz0 + r.h;
        const float x0 = hf.originX + (float)bx0 * cs, x1 = hf.originX + (float)bx1 * cs;
        const float z0 = hf.originZ + (float)bz0 * cs, z1 = hf.originZ + (float)bz1 * cs;
        NavMesh::Poly& poly = out.polys[i];
        poly.v[0] = vertex(bx0, bz0, p.At(x0, z0));
        poly.v[1] = vertex(bx1, bz0, p.At(x1, z0));
        poly.v[2] = vertex(bx1, bz1, p.At(x1, z1));
        poly.v[3] = vertex(bx0, bz1, p.At(x0, z1));
    }
    // ---- Links: runs of connected boundary cells between two rectangles --------
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const CellRect& r = rects[i];
        auto portalY = [&](std::size_t other, float x, float z) { return 0.5f * (planes[i].At(x, z) + planes[other].At(x, z)); };
        for (int side = 0; side < 4; ++side) {
            const bool alongZ = side < 2;              // the shared edge runs along z (sides +x / -x)
            const int n = alongZ ? r.h : r.w;
            int runStart = -1, runRect = -1;
            auto flush = [&](int endExclusive) {
                if (runStart < 0 || runRect < 0) return;
                NavMesh::Link l; l.from = (int)i; l.to = runRect;
                if (alongZ) {
                    const float x = hf.originX + (float)(side == 0 ? r.ix0 + r.w : r.ix0) * cs;
                    const float za = hf.originZ + (float)(r.iz0 + runStart) * cs, zb = hf.originZ + (float)(r.iz0 + endExclusive) * cs;
                    l.a = Vec3{x, portalY((std::size_t)runRect, x, za), za};
                    l.b = Vec3{x, portalY((std::size_t)runRect, x, zb), zb};
                } else {
                    const float z = hf.originZ + (float)(side == 2 ? r.iz0 + r.h : r.iz0) * cs;
                    const float xa = hf.originX + (float)(r.ix0 + runStart) * cs, xb = hf.originX + (float)(r.ix0 + endExclusive) * cs;
                    l.a = Vec3{xa, portalY((std::size_t)runRect, xa, z), z};
                    l.b = Vec3{xb, portalY((std::size_t)runRect, xb, z), z};
                }
                out.links.push_back(l);
                runStart = -1; runRect = -1;
            };
            for (int t = 0; t < n; ++t) {
                const int k = alongZ ? r.at(t, side == 0 ? r.w - 1 : 0) : r.at(side == 2 ? r.h - 1 : 0, t);
                const int nb = cells[(std::size_t)k].con[side];
                const int other = nb >= 0 ? cells[(std::size_t)nb].rect : -1;
                if (other < 0 || other == (int)i) { flush(t); continue; }
                if (other != runRect) { flush(t); runStart = t; runRect = other; }
            }
            flush(n);
        }
    }
    out.Finalize();
    st.polys = (int)out.polys.size(); st.links = (int)out.links.size(); st.vertices = (int)out.vertices.size();
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (stats) *stats = st;
    report(1.0f);
    return true;
}

} // namespace okay
