#include "okay/Modeling/ScreenSelect.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace okay::modeling {

SelectShape SelectShape::Box(Vec2 p, Vec2 q) { SelectShape s; s.kind = Kind::Box; s.a = p; s.b = q; return s; }
SelectShape SelectShape::Circle(Vec2 c, float r) { SelectShape s; s.kind = Kind::Circle; s.centre = c; s.radius = r; return s; }
SelectShape SelectShape::Lasso(std::vector<Vec2> o) { SelectShape s; s.kind = Kind::Lasso; s.poly = std::move(o); return s; }

bool SelectShape::Valid() const {
    switch (kind) {
        case Kind::Box: return std::isfinite(a.x + a.y + b.x + b.y);
        case Kind::Circle: return std::isfinite(centre.x + centre.y + radius) && radius > 0.0f;
        case Kind::Lasso: return poly.size() >= 3;
    }
    return false;
}

bool SelectShape::Contains(float x, float y) const {
    if (!Valid() || !std::isfinite(x + y)) return false;
    switch (kind) {
        case Kind::Box:
            return x >= std::min(a.x, b.x) && x <= std::max(a.x, b.x) && y >= std::min(a.y, b.y) && y <= std::max(a.y, b.y);
        case Kind::Circle: {
            const float dx = x - centre.x, dy = y - centre.y;
            return dx * dx + dy * dy <= radius * radius;
        }
        case Kind::Lasso: {                                   // even-odd crossing count
            bool in = false;
            for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
                const Vec2& p = poly[i]; const Vec2& q = poly[j];
                if (((p.y > y) != (q.y > y)) && (x < (q.x - p.x) * (y - p.y) / (q.y - p.y) + p.x)) in = !in;
            }
            return in;
        }
    }
    return false;
}

std::vector<char> FrontFacingVertices(const Mesh& m, const Vec3& eye) {
    std::vector<char> front(m.vertices.size(), 0);
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const int i0 = m.triangles[t * 3], i1 = m.triangles[t * 3 + 1], i2 = m.triangles[t * 3 + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= (int)front.size() || i1 >= (int)front.size() || i2 >= (int)front.size()) continue;
        const Vec3& a = m.vertices[i0];
        const Vec3 n = Vec3::Cross(m.vertices[i1] - a, m.vertices[i2] - a);
        const Vec3 c = (a + m.vertices[i1] + m.vertices[i2]) * (1.0f / 3.0f);
        if (Vec3::Dot(n, eye - c) > 0.0f) front[i0] = front[i1] = front[i2] = 1;
    }
    return front;
}

namespace {
bool Inside(const SelectShape& s, const std::vector<ScreenPoint>& proj, int v) {
    return v >= 0 && v < (int)proj.size() && proj[v].ok && s.Contains(proj[v].x, proj[v].y);
}
}

std::vector<int> SelectVerticesIn(const Mesh& m, const std::vector<ScreenPoint>& proj, const SelectShape& s, bool xray, const Vec3& eye) {
    std::vector<int> out;
    if (!s.Valid()) return out;
    const std::vector<char> front = xray ? std::vector<char>() : FrontFacingVertices(m, eye);
    for (int v = 0; v < (int)m.vertices.size(); ++v) {
        if (!xray && !front[v]) continue;
        if (Inside(s, proj, v)) out.push_back(v);
    }
    return out;
}

std::vector<int> SelectFacesIn(const Mesh& m, const std::vector<ScreenPoint>& proj, const SelectShape& s, bool xray, const Vec3& eye) {
    std::vector<int> out;
    if (!s.Valid()) return out;
    const int nt = m.TriangleCount();
    const bool groups = (int)m.polyFace.size() == nt && !m.polyFace.empty();
    struct G { float x = 0, y = 0; int n = 0; bool front = false; bool ok = true; std::vector<int> tris; };
    std::map<int, G> g;
    for (int t = 0; t < nt; ++t) {
        G& gr = g[groups ? m.polyFace[t] : t];
        gr.tris.push_back(t);
        const int i0 = m.triangles[t * 3], i1 = m.triangles[t * 3 + 1], i2 = m.triangles[t * 3 + 2];
        const int idx[3] = {i0, i1, i2};
        bool good = true;
        for (int i : idx) if (i < 0 || i >= (int)proj.size() || i >= (int)m.vertices.size() || !proj[i].ok) good = false;
        if (!good) { gr.ok = false; continue; }
        for (int i : idx) { gr.x += proj[i].x; gr.y += proj[i].y; ++gr.n; }
        const Vec3& a = m.vertices[i0];
        const Vec3 n = Vec3::Cross(m.vertices[i1] - a, m.vertices[i2] - a);
        const Vec3 c = (a + m.vertices[i1] + m.vertices[i2]) * (1.0f / 3.0f);
        if (Vec3::Dot(n, eye - c) > 0.0f) gr.front = true;
    }
    for (auto& kv : g) {
        const G& gr = kv.second;
        if (!gr.ok || gr.n == 0) continue;
        if (!xray && !gr.front) continue;
        if (s.Contains(gr.x / (float)gr.n, gr.y / (float)gr.n)) out.insert(out.end(), gr.tris.begin(), gr.tris.end());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::pair<int, int>> SelectEdgesIn(const Mesh& m, const std::vector<std::pair<int, int>>& edges, const std::vector<ScreenPoint>& proj,
                                               const SelectShape& s, bool xray, const Vec3& eye) {
    std::vector<std::pair<int, int>> out;
    if (!s.Valid()) return out;
    const std::vector<char> front = xray ? std::vector<char>() : FrontFacingVertices(m, eye);
    for (const auto& e : edges) {
        if (!xray && (e.first < 0 || e.second < 0 || e.first >= (int)front.size() || e.second >= (int)front.size() || !front[e.first] || !front[e.second])) continue;
        if (Inside(s, proj, e.first) && Inside(s, proj, e.second)) out.push_back(e);
    }
    return out;
}

} // namespace okay::modeling
