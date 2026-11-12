#include "okay/AI/NavMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace okay {

namespace {

constexpr float kEps = 1e-4f;

// 2D cross product on the XZ plane: > 0 when v points to the right of u (seen
// from above, walking along u).
inline float Cross2(const Vec3& u, const Vec3& v) { return u.x * v.z - u.z * v.x; }
inline float Dist2D(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}
inline float Dist3(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
inline bool Same2D(const Vec3& a, const Vec3& b) {
    return std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.z - b.z) < 1e-5f;
}
inline float Rand01(std::uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return (float)((seed >> 8) & 0xFFFFFF) / (float)0x1000000;
}

} // namespace

const char* NavAreaDefaultName(int area) {
    static const char* const kNames[kNavAreaCount] = {
        "Walkable", "Not Walkable", "Water", "Road", "Area 4", "Area 5", "Area 6", "Area 7", "Area 8", "Area 9", "Area 10",
        "Area 11", "Area 12", "Area 13", "Area 14", "Area 15", "Area 16", "Area 17", "Area 18", "Area 19", "Area 20",
        "Area 21", "Area 22", "Area 23", "Area 24", "Area 25", "Area 26", "Area 27", "Area 28", "Area 29", "Area 30", "Area 31"};
    return area >= 0 && area < kNavAreaCount ? kNames[area] : "Area ?";
}

float NavAreaDefaultCost(int area) {
    switch (area) {
        case NavArea::Water: return 3.0f;
        case NavArea::Road:  return 0.5f;
        default: return 1.0f;
    }
}

void NavMesh::Clear() {
    vertices.clear(); polys.clear(); links.clear();
    boundsMin = boundsMax = Vec3{0, 0, 0};
    m_gridStart.clear(); m_gridItems.clear(); m_cumArea.clear();
    m_gridW = m_gridH = 0;
}

void NavMesh::Finalize() {
    for (Poly& p : polys) {
        p.minX = p.minZ = p.minY = 1e30f; p.maxX = p.maxZ = p.maxY = -1e30f;
        p.center = Vec3{0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            const Vec3& v = vertices[(std::size_t)p.v[k]];
            p.minX = std::min(p.minX, v.x); p.maxX = std::max(p.maxX, v.x);
            p.minZ = std::min(p.minZ, v.z); p.maxZ = std::max(p.maxZ, v.z);
            p.minY = std::min(p.minY, v.y); p.maxY = std::max(p.maxY, v.y);
            p.center += v * 0.25f;
        }
    }
    std::stable_sort(links.begin(), links.end(), [](const Link& a, const Link& b) { return a.from < b.from; });
    for (Poly& p : polys) { p.firstLink = 0; p.linkCount = 0; }
    for (std::size_t i = 0; i < links.size(); ++i) {
        Poly& p = polys[(std::size_t)links[i].from];
        if (p.linkCount == 0) p.firstLink = (int)i;
        ++p.linkCount;
    }
    if (!polys.empty()) {
        boundsMin = Vec3{1e30f, 1e30f, 1e30f}; boundsMax = Vec3{-1e30f, -1e30f, -1e30f};
        for (const Poly& p : polys) {
            boundsMin.x = std::min(boundsMin.x, p.minX); boundsMax.x = std::max(boundsMax.x, p.maxX);
            boundsMin.y = std::min(boundsMin.y, p.minY); boundsMax.y = std::max(boundsMax.y, p.maxY);
            boundsMin.z = std::min(boundsMin.z, p.minZ); boundsMax.z = std::max(boundsMax.z, p.maxZ);
        }
    } else boundsMin = boundsMax = Vec3{0, 0, 0};
    BuildGrid();
    m_cumArea.resize(polys.size());
    float acc = 0.0f;
    for (std::size_t i = 0; i < polys.size(); ++i) {
        const Poly& p = polys[i];
        acc += (p.maxX - p.minX) * (p.maxZ - p.minZ);
        m_cumArea[i] = acc;
    }
    // Scratch buffers sized once for this mesh.
    const std::size_t n = polys.size();
    m_s.g.assign(n, 0.0f); m_s.parent.assign(n, -1); m_s.parentLink.assign(n, -1);
    m_s.epoch.assign(n, 0); m_s.closed.assign(n, 0); m_s.mark.assign(n, 0);
    m_s.stamp = 0; m_s.markStamp = 0;
    m_s.heap.reserve(64); m_s.polyPath.reserve(64); m_s.pathLinks.reserve(64); m_s.nearPolys.reserve(64);
    m_s.left.reserve(64); m_s.right.reserve(64);
}

void NavMesh::BuildGrid() {
    m_gridStart.clear(); m_gridItems.clear();
    m_gridW = m_gridH = 0;
    if (polys.empty()) return;
    const float ex = boundsMax.x - boundsMin.x, ez = boundsMax.z - boundsMin.z;
    m_gridCell = std::max(ex, ez) / 64.0f;
    if (m_gridCell < 1.0f) m_gridCell = 1.0f;
    if (m_gridCell > 8.0f) m_gridCell = 8.0f;
    m_gridW = (int)(ex / m_gridCell) + 1;
    m_gridH = (int)(ez / m_gridCell) + 1;
    std::vector<int> count((std::size_t)m_gridW * m_gridH + 1, 0);
    auto range = [&](const Poly& p, int& x0, int& z0, int& x1, int& z1) {
        x0 = std::clamp((int)((p.minX - boundsMin.x) / m_gridCell), 0, m_gridW - 1);
        x1 = std::clamp((int)((p.maxX - boundsMin.x) / m_gridCell), 0, m_gridW - 1);
        z0 = std::clamp((int)((p.minZ - boundsMin.z) / m_gridCell), 0, m_gridH - 1);
        z1 = std::clamp((int)((p.maxZ - boundsMin.z) / m_gridCell), 0, m_gridH - 1);
    };
    for (const Poly& p : polys) {
        int x0, z0, x1, z1; range(p, x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) ++count[(std::size_t)z * m_gridW + x];
    }
    m_gridStart.assign(count.size(), 0);
    for (std::size_t i = 1; i < count.size(); ++i) m_gridStart[i] = m_gridStart[i - 1] + count[i - 1];
    m_gridItems.assign((std::size_t)m_gridStart.back(), 0);
    std::vector<int> fill(m_gridStart.begin(), m_gridStart.end());
    for (std::size_t i = 0; i < polys.size(); ++i) {
        int x0, z0, x1, z1; range(polys[i], x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) m_gridItems[(std::size_t)fill[(std::size_t)z * m_gridW + x]++] = (int)i;
    }
}

void NavMesh::PolysNear(float minX, float minZ, float maxX, float maxZ, std::vector<int>& out) const {
    out.clear();
    if (m_gridW <= 0 || m_gridH <= 0) return;
    if (maxX < boundsMin.x || minX > boundsMax.x || maxZ < boundsMin.z || minZ > boundsMax.z) return;
    int x0 = std::clamp((int)((minX - boundsMin.x) / m_gridCell), 0, m_gridW - 1);
    int x1 = std::clamp((int)((maxX - boundsMin.x) / m_gridCell), 0, m_gridW - 1);
    int z0 = std::clamp((int)((minZ - boundsMin.z) / m_gridCell), 0, m_gridH - 1);
    int z1 = std::clamp((int)((maxZ - boundsMin.z) / m_gridCell), 0, m_gridH - 1);
    if (++m_s.markStamp == 0) { std::fill(m_s.mark.begin(), m_s.mark.end(), 0u); m_s.markStamp = 1; }
    if (m_s.mark.size() != polys.size()) m_s.mark.assign(polys.size(), 0);
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const std::size_t c = (std::size_t)z * m_gridW + x;
            for (int k = m_gridStart[c]; k < m_gridStart[c + 1]; ++k) {
                const int pi = m_gridItems[(std::size_t)k];
                if (m_s.mark[(std::size_t)pi] == m_s.markStamp) continue;
                m_s.mark[(std::size_t)pi] = m_s.markStamp;
                out.push_back(pi);
            }
        }
    std::sort(out.begin(), out.end());   // deterministic regardless of grid order
}

bool NavMesh::Contains(int poly, float x, float z) const {
    if (poly < 0 || poly >= (int)polys.size()) return false;
    const Poly& p = polys[(std::size_t)poly];
    return x >= p.minX - kEps && x <= p.maxX + kEps && z >= p.minZ - kEps && z <= p.maxZ + kEps;
}

float NavMesh::HeightAt(int poly, float x, float z) const {
    if (poly < 0 || poly >= (int)polys.size()) return 0.0f;
    const Poly& p = polys[(std::size_t)poly];
    const float w = p.maxX - p.minX, d = p.maxZ - p.minZ;
    float u = w > kEps ? std::clamp((x - p.minX) / w, 0.0f, 1.0f) : 0.0f;
    float v = d > kEps ? std::clamp((z - p.minZ) / d, 0.0f, 1.0f) : 0.0f;
    const float y0 = vertices[(std::size_t)p.v[0]].y, y1 = vertices[(std::size_t)p.v[1]].y;
    const float y2 = vertices[(std::size_t)p.v[2]].y, y3 = vertices[(std::size_t)p.v[3]].y;
    const float top = y0 + (y1 - y0) * u, bottom = y3 + (y2 - y3) * u;
    return top + (bottom - top) * v;
}

int NavMesh::FindNearestPoly(const Vec3& p, const Vec3& extents, Vec3* nearest) const {
    PolysNear(p.x - extents.x, p.z - extents.z, p.x + extents.x, p.z + extents.z, m_s.nearPolys);
    int best = -1; float bestScore = 1e30f; Vec3 bestPt;
    const float maxXZ = std::max(extents.x, extents.z);
    for (int pi : m_s.nearPolys) {
        const Poly& q = polys[(std::size_t)pi];
        const float cx = std::clamp(p.x, q.minX, q.maxX), cz = std::clamp(p.z, q.minZ, q.maxZ);
        const float y = HeightAt(pi, cx, cz);
        const float dy = std::fabs(p.y - y);
        if (dy > extents.y) continue;
        const float dxz2 = (p.x - cx) * (p.x - cx) + (p.z - cz) * (p.z - cz);
        if (dxz2 > maxXZ * maxXZ) continue;
        const float score = dxz2 + dy * dy;
        if (score < bestScore) { bestScore = score; best = pi; bestPt = Vec3{cx, y, cz}; }
    }
    if (best >= 0 && nearest) *nearest = bestPt;
    return best;
}

bool NavMesh::SamplePosition(const Vec3& p, float maxDistance, Vec3& out, int* poly) const {
    int pi = FindNearestPoly(p, Vec3{maxDistance, maxDistance * 2.0f, maxDistance}, &out);
    if (poly) *poly = pi;
    return pi >= 0;
}

float NavMesh::SampleHeight(const Vec3& p, float fallback, float maxDistance) const {
    int pi = FindNearestPoly(p, Vec3{maxDistance, maxDistance * 2.0f, maxDistance});
    return pi >= 0 ? HeightAt(pi, p.x, p.z) : fallback;
}

bool NavMesh::FindPolyPath(int start, const Vec3& from, int end, const Vec3& to, std::vector<int>& out,
                           const NavQueryFilter* filter) const {
    out.clear();
    m_s.pathLinks.clear();
    const std::size_t n = polys.size();
    if (start < 0 || end < 0 || start >= (int)n || end >= (int)n) return false;
    const NavQueryFilter* F = filter;
    auto allowed = [&](int pi) { return !F || F->Allows(polys[(std::size_t)pi].area); };
    auto areaCost = [&](int pi) { return F ? F->Cost(polys[(std::size_t)pi].area) : 1.0f; };
    if (!allowed(end)) return false;
    const std::vector<NavOffMeshEdge>* edges = F ? F->edges : nullptr;
    // The cheapest a unit of distance can ever cost: keeps the heuristic admissible.
    float minCost = 1.0f;
    if (F) {
        minCost = 1e30f;
        for (int a = 0; a < kNavAreaCount; ++a) if (F->Allows(a)) minCost = std::min(minCost, F->Cost(a));
        if (edges)
            for (const NavOffMeshEdge& e : *edges)
                if (F->Allows(e.area)) minCost = std::min(minCost, e.cost * F->Cost(e.area));
        if (minCost > 1e29f) minCost = 1.0f;
        minCost = std::max(minCost, 0.001f);
    }
    if (m_s.g.size() != n) {
        m_s.g.assign(n, 0.0f); m_s.parent.assign(n, -1); m_s.parentLink.assign(n, -1);
        m_s.epoch.assign(n, 0); m_s.closed.assign(n, 0); m_s.stamp = 0;
    }
    if (++m_s.stamp == 0) { std::fill(m_s.epoch.begin(), m_s.epoch.end(), 0u); m_s.stamp = 1; }
    const std::uint32_t st = m_s.stamp;
    auto& heap = m_s.heap; heap.clear();
    auto greater = [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a > b; };
    // Where the search stands in a polygon: the start point, the portal it came through
    // (its midpoint) or the landing point of the off-mesh edge it came by (parentLink <= -2).
    auto nodePos = [&](int pi) -> Vec3 {
        const int l = m_s.parentLink[(std::size_t)pi];
        if (pi == start || l == -1) return from;
        if (l <= -2) return (*edges)[(std::size_t)(-l - 2)].to;
        return (links[(std::size_t)l].a + links[(std::size_t)l].b) * 0.5f;
    };
    m_s.g[(std::size_t)start] = 0.0f; m_s.parent[(std::size_t)start] = -1; m_s.parentLink[(std::size_t)start] = -1;
    m_s.epoch[(std::size_t)start] = st; m_s.closed[(std::size_t)start] = 0;
    heap.emplace_back(Dist3(from, to) * minCost, start);
    bool found = false;
    auto relax = [&](int cur, int nb, int linkCode, float stepCost, const Vec3& arrive) {
        const bool seen = m_s.epoch[(std::size_t)nb] == st;
        if (seen && m_s.closed[(std::size_t)nb]) return;
        float ng = m_s.g[(std::size_t)cur] + stepCost;
        if (nb == end) ng += Dist3(arrive, to) * areaCost(nb);   // the last stretch inside the goal polygon
        if (seen && ng >= m_s.g[(std::size_t)nb]) return;
        m_s.g[(std::size_t)nb] = ng; m_s.parent[(std::size_t)nb] = cur; m_s.parentLink[(std::size_t)nb] = linkCode;
        m_s.epoch[(std::size_t)nb] = st; m_s.closed[(std::size_t)nb] = 0;
        heap.emplace_back(ng + (nb == end ? 0.0f : Dist3(arrive, to) * minCost), nb);
        std::push_heap(heap.begin(), heap.end(), greater);
    };
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), greater);
        const int cur = heap.back().second; heap.pop_back();
        if (m_s.closed[(std::size_t)cur]) continue;
        m_s.closed[(std::size_t)cur] = 1;
        if (cur == end) { found = true; break; }
        const Vec3 cp = nodePos(cur);
        const Poly& P = polys[(std::size_t)cur];
        const float here = areaCost(cur);
        for (int k = 0; k < P.linkCount; ++k) {
            const int li = P.firstLink + k;
            const Link& l = links[(std::size_t)li];
            const int nb = l.to;
            if (nb < 0 || nb >= (int)n || !allowed(nb)) continue;
            const Vec3 mid = (l.a + l.b) * 0.5f;
            relax(cur, nb, li, Dist3(cp, mid) * here, mid);
        }
        if (edges)
            for (std::size_t ei = 0; ei < edges->size(); ++ei) {
                const NavOffMeshEdge& e = (*edges)[ei];
                if (e.fromPoly != cur || e.toPoly < 0 || e.toPoly >= (int)n) continue;
                if (!F->Allows(e.area) || !allowed(e.toPoly)) continue;
                const float c = Dist3(cp, e.from) * here + Dist3(e.from, e.to) * std::max(e.cost, 0.0f) * F->Cost(e.area);
                relax(cur, e.toPoly, -2 - (int)ei, c, e.to);
            }
    }
    if (!found) return false;
    for (int c = end; c >= 0; c = m_s.parent[(std::size_t)c]) {
        out.push_back(c);
        m_s.pathLinks.push_back(m_s.parentLink[(std::size_t)c]);
        if (c == start) break;
    }
    std::reverse(out.begin(), out.end());
    std::reverse(m_s.pathLinks.begin(), m_s.pathLinks.end());
    return true;
}

void NavMesh::PortalLeftRight(const Link& l, const Vec3& dir, Vec3& left, Vec3& right) const {
    // b is to the right of a (walking along dir) when the cross product is positive.
    if (Cross2(dir, l.b - l.a) > 0.0f) { left = l.a; right = l.b; }
    else { left = l.b; right = l.a; }
}

void NavMesh::StringPull(std::size_t i0, std::size_t i1, const Vec3& s, const Vec3& e, bool smooth, std::vector<Vec3>& out) const {
    const auto& path = m_s.polyPath;
    if (i0 == i1) { out.push_back(s); out.push_back(e); return; }
    if (!smooth) {
        out.push_back(s);
        for (std::size_t i = i0 + 1; i < i1; ++i) out.push_back(polys[(std::size_t)path[i]].center);
        out.push_back(e);
        return;
    }
    // Funnel (string pulling) across the portals between consecutive polygons.
    auto& L = m_s.left; auto& R = m_s.right;
    L.clear(); R.clear();
    L.push_back(s); R.push_back(s);
    for (std::size_t i = i0 + 1; i <= i1; ++i) {
        const int li = m_s.pathLinks[i];
        if (li < 0 || li >= (int)links.size()) { out.push_back(s); out.push_back(e); return; }
        const Vec3 dir = polys[(std::size_t)path[i]].center - polys[(std::size_t)path[i - 1]].center;
        Vec3 l, r; PortalLeftRight(links[(std::size_t)li], dir, l, r);
        L.push_back(l); R.push_back(r);
    }
    L.push_back(e); R.push_back(e);
    Vec3 apex = s, pl = s, pr = s;
    std::size_t apexIdx = 0, leftIdx = 0, rightIdx = 0;
    out.push_back(s);
    const std::size_t first = out.size() - 1;
    for (std::size_t i = 1; i < L.size(); ++i) {
        const Vec3 l = L[i], r = R[i];
        // Right side of the funnel: tighten when the new point is inside it.
        if (Cross2(pr - apex, r - apex) <= 0.0f) {
            if (Same2D(apex, pr) || Cross2(pl - apex, r - apex) >= 0.0f) { pr = r; rightIdx = i; }
            else {   // crossed over the left edge: the left point is a corner
                out.push_back(pl);
                apex = pl; apexIdx = leftIdx;
                pl = apex; pr = apex; leftIdx = apexIdx; rightIdx = apexIdx;
                i = apexIdx;
                continue;
            }
        }
        if (Cross2(pl - apex, l - apex) >= 0.0f) {
            if (Same2D(apex, pl) || Cross2(pr - apex, l - apex) <= 0.0f) { pl = l; leftIdx = i; }
            else {
                out.push_back(pr);
                apex = pr; apexIdx = rightIdx;
                pl = apex; pr = apex; leftIdx = apexIdx; rightIdx = apexIdx;
                i = apexIdx;
                continue;
            }
        }
    }
    if (out.size() == first + 1 || !Same2D(out.back(), e)) out.push_back(e);
}

bool NavMesh::FindPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, bool smooth,
                       const NavQueryFilter* filter, std::vector<int>* legEdges) const {
    out.clear();
    if (legEdges) legEdges->clear();
    const Vec3 ext{1.0f, 2.0f, 1.0f};
    Vec3 s, e;
    const int ps = FindNearestPoly(from, ext, &s);
    const int pe = FindNearestPoly(to, ext, &e);
    if (ps < 0 || pe < 0) return false;
    if (filter && !filter->Allows(polys[(std::size_t)pe].area)) return false;
    if (ps == pe) {
        out.push_back(s); out.push_back(e);
        if (legEdges) legEdges->assign(2, -1);
        return true;
    }
    auto& path = m_s.polyPath;
    if (!FindPolyPath(ps, s, pe, e, path, filter)) return false;
    const std::size_t n = path.size();
    const std::vector<NavOffMeshEdge>* edges = filter ? filter->edges : nullptr;
    if (legEdges) legEdges->clear();
    // Split the polygon path at every off-mesh jump: walk to its start, then restart at its landing.
    std::size_t segStart = 0;
    Vec3 segFrom = s;
    int arrivedBy = -1;
    for (std::size_t i = 1; i <= n; ++i) {
        const bool jump = i < n && m_s.pathLinks[i] <= -2 && edges;
        if (i < n && !jump) continue;
        const std::size_t last = i - 1;
        const NavOffMeshEdge* je = jump ? &(*edges)[(std::size_t)(-m_s.pathLinks[i] - 2)] : nullptr;
        const Vec3 segTo = je ? je->from : e;
        const std::size_t base = out.size();
        StringPull(segStart, last, segFrom, segTo, smooth, out);
        if (legEdges) { legEdges->resize(out.size(), -1); (*legEdges)[base] = arrivedBy; }
        if (!je) break;
        arrivedBy = je->id;
        segStart = i; segFrom = je->to;
    }
    if (legEdges) legEdges->resize(out.size(), -1);
    return true;
}

bool NavMesh::Raycast(const Vec3& from, const Vec3& to, Vec3& hit, int* lastPoly) const {
    const Vec3 ext{1.0f, 2.0f, 1.0f};
    Vec3 start;
    int poly = FindNearestPoly(from, ext, &start);
    if (lastPoly) *lastPoly = poly;
    if (poly < 0) { hit = from; return true; }
    Vec3 p = start;
    const float dx = to.x - from.x, dz = to.z - from.z;
    for (std::size_t guard = 0; guard < polys.size() + 2; ++guard) {
        const Poly& P = polys[(std::size_t)poly];
        if (lastPoly) *lastPoly = poly;
        if (Contains(poly, to.x, to.z)) { hit = Vec3{to.x, HeightAt(poly, to.x, to.z), to.z}; return false; }
        // Exit edge along the ray: the nearest positive crossing of the rectangle's sides.
        float best = 1e30f; int edge = -1;   // 0 +x, 1 -x, 2 +z, 3 -z
        if (dx > kEps)  { float t = (P.maxX - p.x) / dx; if (t < best) { best = t; edge = 0; } }
        if (dx < -kEps) { float t = (P.minX - p.x) / dx; if (t < best) { best = t; edge = 1; } }
        if (dz > kEps)  { float t = (P.maxZ - p.z) / dz; if (t < best) { best = t; edge = 2; } }
        if (dz < -kEps) { float t = (P.minZ - p.z) / dz; if (t < best) { best = t; edge = 3; } }
        if (edge < 0) { hit = Vec3{p.x, HeightAt(poly, p.x, p.z), p.z}; return true; }
        if (best < 0.0f) best = 0.0f;
        const Vec3 q{p.x + dx * best, 0.0f, p.z + dz * best};
        int next = -1;
        for (int k = 0; k < P.linkCount; ++k) {
            const Link& l = links[(std::size_t)(P.firstLink + k)];
            const float ax = l.a.x, az = l.a.z, bx = l.b.x, bz = l.b.z;
            bool onEdge = false;
            if (edge == 0) onEdge = std::fabs(ax - P.maxX) < 1e-3f && std::fabs(bx - P.maxX) < 1e-3f;
            if (edge == 1) onEdge = std::fabs(ax - P.minX) < 1e-3f && std::fabs(bx - P.minX) < 1e-3f;
            if (edge == 2) onEdge = std::fabs(az - P.maxZ) < 1e-3f && std::fabs(bz - P.maxZ) < 1e-3f;
            if (edge == 3) onEdge = std::fabs(az - P.minZ) < 1e-3f && std::fabs(bz - P.minZ) < 1e-3f;
            if (!onEdge) continue;
            const bool alongX = edge >= 2;
            const float qq = alongX ? q.x : q.z, a0 = alongX ? ax : az, b0 = alongX ? bx : bz;
            if (qq >= std::min(a0, b0) - 1e-3f && qq <= std::max(a0, b0) + 1e-3f) { next = l.to; break; }
        }
        if (next < 0) { hit = Vec3{q.x, HeightAt(poly, q.x, q.z), q.z}; return true; }
        poly = next;
        p = Vec3{q.x + dx * 1e-5f, 0.0f, q.z + dz * 1e-5f};
    }
    hit = to;
    return true;
}

float NavMesh::Area() const { return m_cumArea.empty() ? 0.0f : m_cumArea.back(); }

bool NavMesh::RandomPoint(std::uint32_t& seed, Vec3& out) const {
    if (polys.empty() || m_cumArea.empty() || m_cumArea.back() <= 0.0f) return false;
    const float r = Rand01(seed) * m_cumArea.back();
    auto it = std::lower_bound(m_cumArea.begin(), m_cumArea.end(), r);
    std::size_t pi = (std::size_t)(it - m_cumArea.begin());
    if (pi >= polys.size()) pi = polys.size() - 1;
    const Poly& p = polys[pi];
    const float x = p.minX + (p.maxX - p.minX) * Rand01(seed);
    const float z = p.minZ + (p.maxZ - p.minZ) * Rand01(seed);
    out = Vec3{x, HeightAt((int)pi, x, z), z};
    return true;
}

bool NavMesh::RandomPointNear(const Vec3& center, float radius, std::uint32_t& seed, Vec3& out) const {
    if (radius < 0.0f) radius = 0.0f;
    for (int k = 0; k < 12; ++k) {
        const float a = Rand01(seed) * 6.2831853f, d = std::sqrt(Rand01(seed)) * radius;
        const Vec3 c{center.x + std::cos(a) * d, center.y, center.z + std::sin(a) * d};
        Vec3 s;
        if (SamplePosition(c, std::max(radius * 0.5f, 0.5f), s) && Dist2D(s, center) <= radius + 1e-3f) { out = s; return true; }
    }
    return SamplePosition(center, std::max(radius, 1.0f), out);
}

// ---- Files --------------------------------------------------------------------

std::string NavMesh::Serialize() const {
    std::ostringstream out;
    char buf[160];
    out << "okaynav 1\n";
    std::snprintf(buf, sizeof(buf), "settings %g %g %g %g %g %g\n", settings.agentRadius, settings.agentHeight,
                  settings.maxSlope, settings.maxStep, settings.cellSize, settings.cellHeight);
    out << buf;
    std::snprintf(buf, sizeof(buf), "bounds %.4f %.4f %.4f %.4f %.4f %.4f\n", boundsMin.x, boundsMin.y, boundsMin.z,
                  boundsMax.x, boundsMax.y, boundsMax.z);
    out << buf;
    out << "verts " << vertices.size() << "\n";
    for (const Vec3& v : vertices) { std::snprintf(buf, sizeof(buf), "v %.4f %.4f %.4f\n", v.x, v.y, v.z); out << buf; }
    out << "polys " << polys.size() << "\n";
    for (const Poly& p : polys) out << "p " << p.v[0] << " " << p.v[1] << " " << p.v[2] << " " << p.v[3] << " " << p.area << "\n";
    out << "links " << links.size() << "\n";
    for (const Link& l : links) {
        std::snprintf(buf, sizeof(buf), "l %d %d %.4f %.4f %.4f %.4f %.4f %.4f\n", l.from, l.to,
                      l.a.x, l.a.y, l.a.z, l.b.x, l.b.y, l.b.z);
        out << buf;
    }
    return out.str();
}

bool NavMesh::Deserialize(const std::string& text, std::string* error) {
    Clear();
    std::istringstream in(text);
    std::string tag; int version = 0;
    if (!(in >> tag >> version) || tag != "okaynav") { if (error) *error = "not an okaynav file"; return false; }
    if (version != 1) { if (error) *error = "unsupported okaynav version"; return false; }
    std::size_t nv = 0, np = 0, nl = 0;
    while (in >> tag) {
        if (tag == "settings") {
            in >> settings.agentRadius >> settings.agentHeight >> settings.maxSlope >> settings.maxStep
               >> settings.cellSize >> settings.cellHeight;
        } else if (tag == "bounds") {
            in >> boundsMin.x >> boundsMin.y >> boundsMin.z >> boundsMax.x >> boundsMax.y >> boundsMax.z;
        } else if (tag == "verts") {
            in >> nv; if (nv > 50000000u) { if (error) *error = "too many vertices"; return false; }
            vertices.reserve(nv);
        } else if (tag == "v") {
            Vec3 v; in >> v.x >> v.y >> v.z; vertices.push_back(v);
        } else if (tag == "polys") {
            in >> np; if (np > 50000000u) { if (error) *error = "too many polygons"; return false; }
            polys.reserve(np);
        } else if (tag == "p") {
            Poly p; in >> p.v[0] >> p.v[1] >> p.v[2] >> p.v[3];
            {   // the area is a fifth number older files do not have
                std::string rest; std::getline(in, rest);
                std::istringstream rs(rest); int area = 0;
                if (rs >> area) p.area = std::clamp(area, 0, kNavAreaCount - 1);
            }
            for (int k = 0; k < 4; ++k)
                if (p.v[k] < 0 || p.v[k] >= (int)vertices.size()) {
                    if (error) *error = "polygon vertex out of range";
                    Clear(); return false;
                }
            polys.push_back(p);
        } else if (tag == "links") {
            in >> nl; if (nl > 50000000u) { if (error) *error = "too many links"; return false; }
            links.reserve(nl);
        } else if (tag == "l") {
            Link l; in >> l.from >> l.to >> l.a.x >> l.a.y >> l.a.z >> l.b.x >> l.b.y >> l.b.z;
            if (l.from < 0 || l.to < 0 || l.from >= (int)polys.size() || l.to >= (int)polys.size()) {
                if (error) *error = "link polygon out of range";
                Clear(); return false;
            }
            links.push_back(l);
        } else {
            std::string rest; std::getline(in, rest);   // a record a newer build wrote
        }
        if (!in && !in.eof()) {
            if (error) *error = "malformed okaynav data";
            Clear(); return false;
        }
    }
    Finalize();
    return true;
}

bool NavMesh::SaveToFile(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << Serialize();
    return (bool)f;
}

bool NavMesh::LoadFromFile(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return Deserialize(ss.str(), error);
}

} // namespace okay
