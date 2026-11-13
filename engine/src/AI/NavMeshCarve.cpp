// Obstacle carving: cut solid volumes (NavMeshObstacle footprints) out of a baked NavMesh.
//
// A baked polygon is an axis-aligned rectangle with a height at each corner. A volume's footprint is covered
// by axis-aligned strips (NavCarveRects), each grown by the agent radius; every polygon a strip overlaps (and
// whose floor the volume really stands on) has the strip subtracted, leaving up to four rectangles that take
// their heights from the original polygon. Portals between the surviving rectangles are rebuilt from the
// original portals, clipped to the rectangles that still touch.
#include "okay/AI/NavMesh.hpp"

#include <algorithm>
#include <cmath>

namespace okay {

namespace {
constexpr float kTouch = 2e-3f;      // coordinates this close are "the same edge"
constexpr float kSliver = 0.02f;     // a leftover rectangle thinner than this is dropped
constexpr float kMinPortal = 0.02f;  // a portal shorter than this is not a way through

struct Rect { float x0, x1, z0, z1; };

float Cross(float ax, float az, float bx, float bz, float cx, float cz) {
    return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
}
} // namespace

// ---- NavCarveVolume --------------------------------------------------------------------------

void NavCarveVolume::SetHull(const float* xs, const float* zs, int n) {
    count = 0;
    if (n <= 0) return;
    std::vector<int> idx((std::size_t)n);
    for (int i = 0; i < n; ++i) idx[(std::size_t)i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return xs[a] < xs[b] || (xs[a] == xs[b] && zs[a] < zs[b]); });
    std::vector<int> h((std::size_t)n * 2 + 2);
    int k = 0;
    for (int i = 0; i < n; ++i) {   // lower hull
        while (k >= 2 && Cross(xs[h[(std::size_t)k - 2]], zs[h[(std::size_t)k - 2]], xs[h[(std::size_t)k - 1]], zs[h[(std::size_t)k - 1]],
                               xs[idx[(std::size_t)i]], zs[idx[(std::size_t)i]]) <= 0.0f) --k;
        h[(std::size_t)k++] = idx[(std::size_t)i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; --i) {   // upper hull
        while (k >= t && Cross(xs[h[(std::size_t)k - 2]], zs[h[(std::size_t)k - 2]], xs[h[(std::size_t)k - 1]], zs[h[(std::size_t)k - 1]],
                               xs[idx[(std::size_t)i]], zs[idx[(std::size_t)i]]) <= 0.0f) --k;
        h[(std::size_t)k++] = idx[(std::size_t)i];
    }
    if (k > 1) --k;   // the last point repeats the first
    k = std::min(k, kMaxPoints);
    for (int i = 0; i < k; ++i) { x[i] = xs[h[(std::size_t)i]]; z[i] = zs[h[(std::size_t)i]]; }
    count = k;
}

void NavCarveVolume::SetCircle(float cx, float cz, float radius) {
    const float r = std::fabs(radius) / std::cos(3.14159265f / (float)kMaxPoints);   // circumscribed
    count = kMaxPoints;
    for (int i = 0; i < kMaxPoints; ++i) {
        const float a = 6.28318531f * (float)i / (float)kMaxPoints;
        x[i] = cx + r * std::cos(a);
        z[i] = cz + r * std::sin(a);
    }
}

float NavCarveVolume::Delta(const NavCarveVolume& o) const {
    if (count != o.count) return 1e9f;
    float d = std::max(std::fabs(minY - o.minY), std::fabs(maxY - o.maxY));
    for (int i = 0; i < count; ++i) d = std::max(d, std::max(std::fabs(x[i] - o.x[i]), std::fabs(z[i] - o.z[i])));
    return d;
}

// ---- Footprint -> rectangles -----------------------------------------------------------------

int NavCarveRects(const NavCarveVolume& v, float inflate, float strip, std::vector<NavCarveRect>& out) {
    if (v.count < 3 || !(v.maxY >= v.minY)) return 0;
    float bx0 = 1e30f, bx1 = -1e30f, bz0 = 1e30f, bz1 = -1e30f, area = 0.0f;
    for (int i = 0; i < v.count; ++i) {
        const int j = (i + 1) % v.count;
        bx0 = std::min(bx0, v.x[i]); bx1 = std::max(bx1, v.x[i]);
        bz0 = std::min(bz0, v.z[i]); bz1 = std::max(bz1, v.z[i]);
        area += v.x[i] * v.z[j] - v.x[j] * v.z[i];
    }
    area = std::fabs(area) * 0.5f;
    const float boxArea = (bx1 - bx0) * (bz1 - bz0);
    const std::size_t before = out.size();
    auto push = [&](float x0, float x1, float z0, float z1) {
        NavCarveRect r;
        r.minX = x0 - inflate; r.maxX = x1 + inflate; r.minZ = z0 - inflate; r.maxZ = z1 + inflate;
        r.minY = v.minY; r.maxY = v.maxY;
        out.push_back(r);
    };
    const float width = bx1 - bx0;
    strip = std::max(strip, 0.05f);
    if (width <= strip * 1.001f || area >= boxArea * 0.999f) {   // already a rectangle (or one strip is enough)
        push(bx0, bx1, bz0, bz1);
        return 1;
    }
    const int n = std::clamp((int)std::ceil(width / strip), 1, 64);
    for (int s = 0; s < n; ++s) {
        const float xa = bx0 + width * (float)s / (float)n, xb = bx0 + width * (float)(s + 1) / (float)n;
        float zlo = 1e30f, zhi = -1e30f;
        auto take = [&](float z) { zlo = std::min(zlo, z); zhi = std::max(zhi, z); };
        for (int i = 0; i < v.count; ++i) {
            const int j = (i + 1) % v.count;
            const float x0 = v.x[i], z0 = v.z[i], x1 = v.x[j], z1 = v.z[j];
            if (x0 >= xa && x0 <= xb) take(z0);
            for (float xc : {xa, xb}) {
                if ((x0 < xc && x1 > xc) || (x0 > xc && x1 < xc)) take(z0 + (z1 - z0) * (xc - x0) / (x1 - x0));
            }
        }
        if (zhi >= zlo) push(xa, xb, zlo, zhi);
    }
    return (int)(out.size() - before);
}

// ---- Carving ---------------------------------------------------------------------------------

namespace {
// Do a and b touch along a shared vertical (x = const) or horizontal (z = const) line, and over what
// range? `alongX` true: the line has constant x and the range is in z.
bool SharedEdge(const Rect& a, const Rect& b, bool& alongX, float& line, float& lo, float& hi) {
    auto same = [&](float p, float q) { return std::fabs(p - q) < kTouch; };
    if (same(a.x1, b.x0) || same(a.x0, b.x1)) {
        alongX = true; line = same(a.x1, b.x0) ? a.x1 : a.x0;
        lo = std::max(a.z0, b.z0); hi = std::min(a.z1, b.z1);
        if (hi - lo > kMinPortal) return true;
    }
    if (same(a.z1, b.z0) || same(a.z0, b.z1)) {
        alongX = false; line = same(a.z1, b.z0) ? a.z1 : a.z0;
        lo = std::max(a.x0, b.x0); hi = std::min(a.x1, b.x1);
        if (hi - lo > kMinPortal) return true;
    }
    return false;
}
} // namespace

int NavMesh::Carve(const std::vector<NavCarveVolume>& volumes, NavMesh& out) const {
    out.Clear();
    out.settings = settings;
    out.vertices = vertices;
    const std::size_t np = polys.size();

    // The strips, and which polygons each reaches.
    std::vector<NavCarveRect> rects;
    for (const NavCarveVolume& v : volumes) NavCarveRects(v, settings.agentRadius, settings.cellSize, rects);
    std::vector<std::vector<int>> cuts(np);   // polygon -> indices into `rects`
    std::vector<int> near;
    for (std::size_t r = 0; r < rects.size(); ++r) {
        const NavCarveRect& c = rects[r];
        PolysNear(c.minX, c.minZ, c.maxX, c.maxZ, near);
        for (int pi : near) {
            const Poly& p = polys[(std::size_t)pi];
            if (c.maxX - p.minX <= kTouch || p.maxX - c.minX <= kTouch || c.maxZ - p.minZ <= kTouch || p.maxZ - c.minZ <= kTouch) continue;
            // The volume must stand on this floor: reach above it and start below head room.
            if (!(c.minY < p.maxY + settings.agentHeight && c.maxY > p.minY + 0.05f)) continue;
            cuts[(std::size_t)pi].push_back((int)r);
        }
    }

    // New polygons: untouched ones keep their corners, the others become their leftover rectangles.
    std::vector<std::vector<int>> pieces(np);   // original polygon -> new indices
    std::vector<Rect> rectOf;                   // new polygon -> its rectangle
    int changed = 0;
    for (std::size_t i = 0; i < np; ++i) {
        const Poly& p = polys[i];
        if (cuts[i].empty()) {
            out.polys.push_back(p);
            pieces[i].push_back((int)out.polys.size() - 1);
            rectOf.push_back({p.minX, p.maxX, p.minZ, p.maxZ});
            continue;
        }
        ++changed;
        std::vector<Rect> left{{p.minX, p.maxX, p.minZ, p.maxZ}}, next;
        for (int ci : cuts[i]) {
            const NavCarveRect& c = rects[(std::size_t)ci];
            next.clear();
            for (const Rect& r : left) {
                const float ix0 = std::max(r.x0, c.minX), ix1 = std::min(r.x1, c.maxX);
                const float iz0 = std::max(r.z0, c.minZ), iz1 = std::min(r.z1, c.maxZ);
                if (ix1 - ix0 <= kTouch || iz1 - iz0 <= kTouch) { next.push_back(r); continue; }
                if (ix0 - r.x0 > kSliver) next.push_back({r.x0, ix0, r.z0, r.z1});
                if (r.x1 - ix1 > kSliver) next.push_back({ix1, r.x1, r.z0, r.z1});
                if (iz0 - r.z0 > kSliver) next.push_back({ix0, ix1, r.z0, iz0});
                if (r.z1 - iz1 > kSliver) next.push_back({ix0, ix1, iz1, r.z1});
            }
            left.swap(next);
        }
        for (const Rect& r : left) {
            Poly piece;
            piece.area = p.area;
            const float xs[4] = {r.x0, r.x1, r.x1, r.x0}, zs[4] = {r.z0, r.z0, r.z1, r.z1};
            for (int k = 0; k < 4; ++k) {
                piece.v[k] = (int)out.vertices.size();
                out.vertices.push_back(Vec3{xs[k], HeightAt((int)i, xs[k], zs[k]), zs[k]});
            }
            out.polys.push_back(piece);
            pieces[i].push_back((int)out.polys.size() - 1);
            rectOf.push_back(r);
        }
    }

    // Portals. Both directions of an original portal exist as two links; each is clipped to the rectangles
    // that still touch across it. Pieces of one cut polygon get portals to each other.
    for (const Link& l : links) {
        const bool fromCut = !cuts[(std::size_t)l.from].empty(), toCut = !cuts[(std::size_t)l.to].empty();
        if (!fromCut && !toCut) {
            Link k = l; k.from = pieces[(std::size_t)l.from][0]; k.to = pieces[(std::size_t)l.to][0];
            out.links.push_back(k);
            continue;
        }
        const bool alongX = std::fabs(l.a.x - l.b.x) < kTouch;   // the portal runs along z at constant x
        const float c0 = alongX ? l.a.z : l.a.x, c1 = alongX ? l.b.z : l.b.x;
        const float span = c1 - c0;
        if (std::fabs(span) < 1e-6f) continue;
        for (int a : pieces[(std::size_t)l.from])
            for (int b : pieces[(std::size_t)l.to]) {
                bool ax; float line, lo, hi;
                if (!SharedEdge(rectOf[(std::size_t)a], rectOf[(std::size_t)b], ax, line, lo, hi) || ax != alongX) continue;
                if (std::fabs(line - (alongX ? l.a.x : l.a.z)) > kTouch * 5.0f) continue;
                lo = std::max(lo, std::min(c0, c1)); hi = std::min(hi, std::max(c0, c1));
                if (hi - lo <= kMinPortal) continue;
                const float t0 = (lo - c0) / span, t1 = (hi - c0) / span;
                Link k; k.from = a; k.to = b;
                k.a = l.a + (l.b - l.a) * t0; k.b = l.a + (l.b - l.a) * t1;
                out.links.push_back(k);
            }
    }
    for (std::size_t i = 0; i < np; ++i) {
        if (cuts[i].empty()) continue;
        const std::vector<int>& ps = pieces[i];
        for (int a : ps)
            for (int b : ps) {
                if (a == b) continue;
                bool alongX; float line, lo, hi;
                if (!SharedEdge(rectOf[(std::size_t)a], rectOf[(std::size_t)b], alongX, line, lo, hi)) continue;
                Link k; k.from = a; k.to = b;
                const float ya = HeightAt((int)i, alongX ? line : lo, alongX ? lo : line);
                const float yb = HeightAt((int)i, alongX ? line : hi, alongX ? hi : line);
                k.a = alongX ? Vec3{line, ya, lo} : Vec3{lo, ya, line};
                k.b = alongX ? Vec3{line, yb, hi} : Vec3{hi, yb, line};
                out.links.push_back(k);
            }
    }
    out.Finalize();
    return changed;
}

} // namespace okay
