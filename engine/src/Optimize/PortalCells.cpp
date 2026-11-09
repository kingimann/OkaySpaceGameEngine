// Portal / cell visibility. See PortalCells.hpp.
#include "okay/Optimize/PortalCells.hpp"
#include <algorithm>
#include <cmath>

namespace okay::optimize {
namespace {

VisPlane Normalize(float a, float b, float c, float d) {
    VisPlane p;
    const float l = std::sqrt(a * a + b * b + c * c);
    if (l > 1e-12f) { p.n = {a / l, b / l, c / l}; p.d = d / l; }
    else { p.n = {0, 0, 0}; p.d = 1.0f; }       // degenerate plane that accepts everything
    return p;
}

/// Newell normal of a polygon (not normalised); zero for degenerate outlines.
Vec3 PolygonNormal(const std::vector<Vec3>& poly) {
    Vec3 n{0, 0, 0};
    const std::size_t c = poly.size();
    for (std::size_t i = 0; i < c; ++i) {
        const Vec3& a = poly[i];
        const Vec3& b = poly[(i + 1) % c];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

/// True when `p` (already on the polygon's plane) lies inside the convex polygon.
bool InsidePolygon(const std::vector<Vec3>& poly, const Vec3& unitNormal, const Vec3& p) {
    int pos = 0, neg = 0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vec3& a = poly[i];
        const Vec3& b = poly[(i + 1) % poly.size()];
        const float s = Vec3::Dot(unitNormal, Vec3::Cross(b - a, p - a));
        if (s > 1e-5f) ++pos; else if (s < -1e-5f) ++neg;
    }
    return pos == 0 || neg == 0;
}

constexpr float kDoorway = 0.15f;      // an eye this close to a portal's plane, inside its outline, is standing in it

struct Node {
    int cell;
    int fromPortal;
    int depth;
    ConvexRegion region;
    std::vector<int> path;
};

}  // namespace

ConvexRegion ConvexRegion::FromViewProjection(const Mat4& m) {
    ConvexRegion r;
    auto row = [&](int i, float o[4]) { o[0] = m.m[i]; o[1] = m.m[4 + i]; o[2] = m.m[8 + i]; o[3] = m.m[12 + i]; };
    float r0[4], r1[4], r2[4], r3[4];
    row(0, r0); row(1, r1); row(2, r2); row(3, r3);
    auto make = [&](float s, const float* a) {
        return Normalize(r3[0] + s * a[0], r3[1] + s * a[1], r3[2] + s * a[2], r3[3] + s * a[3]);
    };
    r.planes = {make(1, r0), make(-1, r0), make(1, r1), make(-1, r1), make(1, r2), make(-1, r2)};
    return r;
}

bool ConvexRegion::ContainsPoint(const Vec3& p) const {
    for (const VisPlane& pl : planes) if (pl.Distance(p) < 0.0f) return false;
    return true;
}

bool ConvexRegion::IntersectsBounds(const Bounds& b) const {
    for (const VisPlane& pl : planes) {
        const Vec3 pv{b.center.x + (pl.n.x >= 0 ? b.extents.x : -b.extents.x),
                      b.center.y + (pl.n.y >= 0 ? b.extents.y : -b.extents.y),
                      b.center.z + (pl.n.z >= 0 ? b.extents.z : -b.extents.z)};
        if (pl.Distance(pv) < 0.0f) return false;
    }
    return true;
}

bool ConvexRegion::IntersectsSphere(const Vec3& c, float r) const {
    for (const VisPlane& pl : planes) if (pl.Distance(c) < -r) return false;
    return true;
}

std::vector<Vec3> ClipPolygon(const std::vector<Vec3>& polygon, const ConvexRegion& region) {
    std::vector<Vec3> in = polygon, out;
    for (const VisPlane& pl : region.planes) {
        if (in.empty()) break;
        out.clear();
        const std::size_t n = in.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Vec3& cur = in[i];
            const Vec3& nxt = in[(i + 1) % n];
            const float dc = pl.Distance(cur), dn = pl.Distance(nxt);
            if (dc >= 0.0f) out.push_back(cur);
            if ((dc >= 0.0f) != (dn >= 0.0f)) {
                const float t = dc / (dc - dn);
                out.push_back(cur + (nxt - cur) * t);
            }
        }
        in.swap(out);
    }
    return in;
}

bool PortalVisibility::CellVisible(int cell) const {
    return std::binary_search(visibleCells.begin(), visibleCells.end(), cell);
}

bool PortalVisibility::BoundsVisible(int cell, const Bounds& bounds) const {
    if (cameraOutside) return true;
    if (cell < 0 || cell >= static_cast<int>(regions.size())) return false;
    for (const ConvexRegion& r : regions[cell]) if (r.IntersectsBounds(bounds)) return true;
    return false;
}

int PortalCells::AddCell(const Bounds& bounds, const std::string& name) {
    PortalCell c;
    c.bounds = bounds;
    c.name = name;
    cells_.push_back(c);
    return static_cast<int>(cells_.size()) - 1;
}

int PortalCells::AddPortal(int cellA, int cellB, const std::vector<Vec3>& polygon, bool open, const std::string& name) {
    if (cellA < 0 || cellB < 0 || cellA >= CellCount() || cellB >= CellCount() || cellA == cellB || polygon.size() < 3)
        return -1;
    Portal p;
    p.cellA = cellA; p.cellB = cellB; p.polygon = polygon; p.open = open; p.name = name;
    portals_.push_back(p);
    const int id = static_cast<int>(portals_.size()) - 1;
    cells_[cellA].portals.push_back(id);
    cells_[cellB].portals.push_back(id);
    return id;
}

int PortalCells::AddPortalRect(int cellA, int cellB, const Vec3& c, const Vec3& hu, const Vec3& hv, bool open,
                               const std::string& name) {
    return AddPortal(cellA, cellB, {c - hu - hv, c + hu - hv, c + hu + hv, c - hu + hv}, open, name);
}

void PortalCells::SetPortalOpen(int portal, bool open) {
    if (portal >= 0 && portal < PortalCount()) portals_[portal].open = open;
}

int PortalCells::FindCell(const Vec3& p) const {
    int best = -1;
    float bestVol = 0.0f;
    for (int i = 0; i < CellCount(); ++i) {
        if (!cells_[i].bounds.Contains(p)) continue;
        const Vec3 e = cells_[i].bounds.extents;
        const float vol = e.x * e.y * e.z;
        if (best < 0 || vol < bestVol) { best = i; bestVol = vol; }
    }
    return best;
}

PortalVisibility PortalCells::ComputeVisibility(const Vec3& eye, const Mat4& viewProj, int maxDepth, int maxVisits) const {
    PortalVisibility out;
    out.regions.assign(cells_.size(), {});
    out.cameraCell = FindCell(eye);
    if (out.cameraCell < 0) { out.cameraOutside = true; return out; }
    const ConvexRegion frustum = ConvexRegion::FromViewProjection(viewProj);
    const VisPlane farPlane = frustum.planes[5];
    std::vector<char> seen(cells_.size(), 0);
    std::vector<Node> queue;
    queue.push_back({out.cameraCell, -1, 0, frustum, {out.cameraCell}});
    seen[out.cameraCell] = 1;
    out.regions[out.cameraCell].push_back(frustum);
    for (std::size_t head = 0; head < queue.size(); ++head) {
        if (out.nodesVisited >= maxVisits) { out.truncated = true; break; }
        ++out.nodesVisited;
        const Node node = queue[head];          // copy: the queue may grow below
        if (node.depth >= maxDepth) continue;
        for (int pid : cells_[node.cell].portals) {
            if (pid == node.fromPortal) continue;
            const Portal& portal = portals_[pid];
            if (!portal.open) continue;
            const int other = portal.cellA == node.cell ? portal.cellB : portal.cellA;
            if (std::find(node.path.begin(), node.path.end(), other) != node.path.end()) continue;   // no loops
            ++out.portalsTested;
            const Vec3 rawN = PolygonNormal(portal.polygon);
            const float rawL = rawN.Magnitude();
            if (rawL < 1e-9f) continue;
            const Vec3 rawUnit = rawN * (1.0f / rawL);
            const float eyeDist = Vec3::Dot(rawUnit, eye - portal.polygon[0]);
            ConvexRegion next;
            if (std::fabs(eyeDist) <= kDoorway && InsidePolygon(portal.polygon, rawUnit, eye - rawUnit * eyeDist)) {
                next = node.region;                                     // standing in the doorway: nothing to narrow
            } else {
                std::vector<Vec3> clipped = ClipPolygon(portal.polygon, node.region);
                if (clipped.size() < 3) continue;
                const Vec3 pn = PolygonNormal(clipped);
                const float pnl = pn.Magnitude();
                if (pnl < 1e-9f) continue;                              // edge-on or empty
                const Vec3 unit = pn * (1.0f / pnl);
                const float dist = Vec3::Dot(unit, eye - clipped[0]);
                if (std::fabs(dist) > 1e-3f) {
                    // Side planes through the eye and every edge of the clipped outline.
                    Vec3 centroid{0, 0, 0};
                    for (const Vec3& v : clipped) centroid += v;
                    centroid = centroid * (1.0f / static_cast<float>(clipped.size()));
                    for (std::size_t i = 0; i < clipped.size(); ++i) {
                        const Vec3 a = clipped[i] - eye, b = clipped[(i + 1) % clipped.size()] - eye;
                        Vec3 n = Vec3::Cross(a, b);
                        const float l = n.Magnitude();
                        if (l < 1e-9f) continue;                        // eye on this edge's line: no narrowing from it
                        n = n * (1.0f / l);
                        VisPlane pl{n, -Vec3::Dot(n, eye)};
                        if (pl.Distance(centroid) < 0.0f) { pl.n = -pl.n; pl.d = -pl.d; }
                        next.planes.push_back(pl);
                    }
                    // Only what is beyond the portal counts.
                    VisPlane beyond{unit, -Vec3::Dot(unit, clipped[0])};
                    if (beyond.Distance(eye) > 0.0f) { beyond.n = -beyond.n; beyond.d = -beyond.d; }
                    next.planes.push_back(beyond);
                    next.planes.push_back(farPlane);
                } else {
                    next = node.region;                                 // eye in the portal's plane: no narrowing
                }
            }
            ++out.portalsPassed;
            out.regions[other].push_back(next);
            seen[other] = 1;
            Node child;
            child.cell = other; child.fromPortal = pid; child.depth = node.depth + 1;
            child.region = std::move(next); child.path = node.path; child.path.push_back(other);
            queue.push_back(std::move(child));
        }
    }
    for (int i = 0; i < CellCount(); ++i) if (seen[i]) out.visibleCells.push_back(i);
    return out;
}

} // namespace okay::optimize
