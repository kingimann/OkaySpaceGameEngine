#include "okay/Modeling/Measure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace okay::modeling {

namespace {
Vec3 Scaled(const Vec3& v, const Vec3& s) { return Vec3{v.x * s.x, v.y * s.y, v.z * s.z}; }
bool Ok(const Mesh& m, int v) { return v >= 0 && v < (int)m.vertices.size(); }
void Fit(Vec3& lo, Vec3& hi, bool& any, const Vec3& p) {
    if (!any) { lo = hi = p; any = true; return; }
    lo = Vec3{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = Vec3{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
}
float TriArea(const Vec3& a, const Vec3& b, const Vec3& c) { return Vec3::Cross(b - a, c - a).Magnitude() * 0.5f; }
}

std::string Measurement::Summary() const {
    char buf[200];
    if (hasDistance) std::snprintf(buf, sizeof buf, "Distance %.3f  (dx %.3f  dy %.3f  dz %.3f)", distance, delta.x, delta.y, delta.z);
    else if (faces > 0 && edgeLength <= 0.0f) std::snprintf(buf, sizeof buf, "%d face%s, area %.3f, size %.3f x %.3f x %.3f", faces, faces == 1 ? "" : "s", area, size.x, size.y, size.z);
    else if (edges > 0) std::snprintf(buf, sizeof buf, "%d edge%s, length %.3f", edges, edges == 1 ? "" : "s", edgeLength);
    else if (verts > 0) std::snprintf(buf, sizeof buf, "%d vertex%s, size %.3f x %.3f x %.3f", verts, verts == 1 ? "" : "es", size.x, size.y, size.z);
    else std::snprintf(buf, sizeof buf, "Size %.3f x %.3f x %.3f, area %.3f, volume %.3f", size.x, size.y, size.z, area, volume);
    return buf;
}

Measurement MeasureMesh(const Mesh& m, const Vec3& scale) {
    Measurement r;
    bool any = false; Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    for (const Vec3& v : m.vertices) Fit(lo, hi, any, Scaled(v, scale));
    if (any) { r.size = hi - lo; r.diagonal = r.size.Magnitude(); }
    r.verts = (int)m.vertices.size(); r.faces = m.TriangleCount();
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const int i0 = m.triangles[t * 3], i1 = m.triangles[t * 3 + 1], i2 = m.triangles[t * 3 + 2];
        if (!Ok(m, i0) || !Ok(m, i1) || !Ok(m, i2)) continue;
        const Vec3 a = Scaled(m.vertices[i0], scale), b = Scaled(m.vertices[i1], scale), c = Scaled(m.vertices[i2], scale);
        r.area += TriArea(a, b, c);
        r.volume += Vec3::Dot(a, Vec3::Cross(b, c)) / 6.0f;
    }
    return r;
}

Measurement MeasureSelection(const Mesh& m, const std::vector<int>& verts, const std::vector<int>& tris,
                             const std::vector<std::pair<int, int>>& edges, const Vec3& scale) {
    Measurement r;
    bool any = false; Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    std::set<int> vs;
    for (int v : verts) if (Ok(m, v)) vs.insert(v);
    r.verts = (int)vs.size();
    std::vector<int> order;                       // vertices in the order given (for the two-vertex distance)
    for (int v : verts) if (Ok(m, v) && std::find(order.begin(), order.end(), v) == order.end()) order.push_back(v);
    std::set<std::pair<int, int>> es;
    for (const auto& e : edges) {
        if (!Ok(m, e.first) || !Ok(m, e.second) || e.first == e.second) continue;
        es.insert({std::min(e.first, e.second), std::max(e.first, e.second)});
    }
    r.edges = (int)es.size();
    for (const auto& e : es) {
        r.edgeLength += (Scaled(m.vertices[e.first], scale) - Scaled(m.vertices[e.second], scale)).Magnitude();
        vs.insert(e.first); vs.insert(e.second);
    }
    std::vector<char> seenTri((std::size_t)std::max(0, m.TriangleCount()), 0);
    std::vector<int> ts;
    for (int t : tris) if (t >= 0 && t < m.TriangleCount() && !seenTri[t]) { seenTri[t] = 1; ts.push_back(t); }
    std::vector<char> seenV(m.vertices.size(), 0);
    for (int v : vs) seenV[v] = 1;
    for (int t : ts) {
        const int i0 = m.triangles[t * 3], i1 = m.triangles[t * 3 + 1], i2 = m.triangles[t * 3 + 2];
        if (!Ok(m, i0) || !Ok(m, i1) || !Ok(m, i2)) continue;
        r.area += TriArea(Scaled(m.vertices[i0], scale), Scaled(m.vertices[i1], scale), Scaled(m.vertices[i2], scale));
        for (int v : {i0, i1, i2}) if (!seenV[v]) { seenV[v] = 1; vs.insert(v); }
        ++r.faces;
    }
    for (int v : vs) Fit(lo, hi, any, Scaled(m.vertices[v], scale));
    if (any) { r.size = hi - lo; r.diagonal = r.size.Magnitude(); }
    if (order.size() == 2 && edges.empty() && tris.empty()) {
        r.hasDistance = true;
        r.delta = Scaled(m.vertices[order[1]], scale) - Scaled(m.vertices[order[0]], scale);
        r.distance = r.delta.Magnitude();
    }
    return r;
}

const char* SnapTargetName(SnapTarget t) {
    switch (t) {
        case SnapTarget::Grid:   return "Grid";
        case SnapTarget::Vertex: return "Vertex";
        case SnapTarget::Edge:   return "Edge";
        case SnapTarget::Face:   return "Face";
    }
    return "Grid";
}

Vec3 ClosestOnSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const float len2 = ab.SqrMagnitude();
    if (len2 < 1e-20f) return a;
    const float t = std::clamp(Vec3::Dot(p - a, ab) / len2, 0.0f, 1.0f);
    return a + ab * t;
}

// Closest point on a triangle: the plane point if it falls inside, else the nearest on the three edges.
Vec3 ClosestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 n = Vec3::Cross(b - a, c - a);
    const float n2 = n.SqrMagnitude();
    if (n2 > 1e-20f) {
        const Vec3 q = p - n * (Vec3::Dot(p - a, n) / n2);        // p dropped onto the plane
        const float u = Vec3::Dot(Vec3::Cross(c - b, q - b), n);
        const float v = Vec3::Dot(Vec3::Cross(a - c, q - c), n);
        const float w = Vec3::Dot(Vec3::Cross(b - a, q - a), n);
        if (u >= 0 && v >= 0 && w >= 0) return q;
    }
    const Vec3 c1 = ClosestOnSegment(p, a, b), c2 = ClosestOnSegment(p, b, c), c3 = ClosestOnSegment(p, c, a);
    const float d1 = (c1 - p).SqrMagnitude(), d2 = (c2 - p).SqrMagnitude(), d3 = (c3 - p).SqrMagnitude();
    return d1 <= d2 && d1 <= d3 ? c1 : (d2 <= d3 ? c2 : c3);
}

SnapHit SnapToGeometry(const Mesh& m, const Vec3& p, SnapTarget target, float radius, const std::vector<int>& skipVerts) {
    SnapHit best;
    if (target == SnapTarget::Grid || !(radius > 0.0f) || !std::isfinite(p.x + p.y + p.z)) return best;
    std::vector<char> skip(m.vertices.size(), 0);
    for (int v : skipVerts) if (Ok(m, v)) skip[v] = 1;
    float bestD = radius;
    auto offer = [&](const Vec3& q, int a, int b) {
        const float d = (q - p).Magnitude();
        if (d <= bestD) { bestD = d; best.found = true; best.point = q; best.distance = d; best.a = a; best.b = b; }
    };
    if (target == SnapTarget::Vertex) {
        for (int v = 0; v < (int)m.vertices.size(); ++v) if (!skip[v]) offer(m.vertices[v], v, -1);
        return best;
    }
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const int i[3] = {m.triangles[t * 3], m.triangles[t * 3 + 1], m.triangles[t * 3 + 2]};
        if (!Ok(m, i[0]) || !Ok(m, i[1]) || !Ok(m, i[2]) || skip[i[0]] || skip[i[1]] || skip[i[2]]) continue;
        if (target == SnapTarget::Face) offer(ClosestOnTriangle(p, m.vertices[i[0]], m.vertices[i[1]], m.vertices[i[2]]), t, -1);
        else for (int k = 0; k < 3; ++k) {
            const int a = i[k], b = i[(k + 1) % 3];
            offer(ClosestOnSegment(p, m.vertices[a], m.vertices[b]), std::min(a, b), std::max(a, b));
        }
    }
    return best;
}

RayHit RayMeshHit(const Mesh& m, const Vec3& o, const Vec3& dir) {
    RayHit best;
    if (!std::isfinite(o.x + o.y + o.z + dir.x + dir.y + dir.z) || dir.SqrMagnitude() < 1e-20f) return best;
    const Vec3 d = dir.Normalized();
    float bestT = 1e30f;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const int i0 = m.triangles[t * 3], i1 = m.triangles[t * 3 + 1], i2 = m.triangles[t * 3 + 2];
        if (!Ok(m, i0) || !Ok(m, i1) || !Ok(m, i2)) continue;
        const Vec3& a = m.vertices[i0];
        const Vec3 e1 = m.vertices[i1] - a, e2 = m.vertices[i2] - a;
        const Vec3 p = Vec3::Cross(d, e2);
        const float det = Vec3::Dot(e1, p);
        if (std::fabs(det) < 1e-12f) continue;                  // parallel or collapsed
        const float inv = 1.0f / det;
        const Vec3 tv = o - a;
        const float u = Vec3::Dot(tv, p) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const Vec3 q = Vec3::Cross(tv, e1);
        const float v = Vec3::Dot(d, q) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float tt = Vec3::Dot(e2, q) * inv;
        if (tt > 1e-6f && tt < bestT) {
            bestT = tt; best.found = true; best.t = tt; best.tri = t;
            Vec3 n = Vec3::Cross(e1, e2);
            n = n.SqrMagnitude() > 1e-20f ? n.Normalized() : Vec3{0, 1, 0};
            best.normal = Vec3::Dot(n, d) > 0.0f ? n * -1.0f : n;   // facing the ray
        }
    }
    if (best.found) best.point = o + d * best.t;
    return best;
}

} // namespace okay::modeling
