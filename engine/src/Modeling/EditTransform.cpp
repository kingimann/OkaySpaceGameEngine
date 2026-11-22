#include <map>
#include <tuple>
#include "okay/Modeling/EditTransform.hpp"
#include "okay/Math/Quat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace okay::modeling {

const char* EditXformName(EditXform k) {
    switch (k) {
        case EditXform::Move:   return "Move";
        case EditXform::Rotate: return "Rotate";
        case EditXform::Scale:  return "Scale";
    }
    return "";
}

const char* EditPivotName(EditPivot p) {
    switch (p) {
        case EditPivot::Median:     return "Median Point";
        case EditPivot::Individual: return "Individual Origins";
        case EditPivot::Active:     return "Active Vertex";
    }
    return "";
}

void AxisLock::Press(int a, bool shift) {
    if (a < 0 || a > 2) return;
    if (axis == a && plane == shift) { axis = -1; plane = false; return; }   // same key again: free
    axis = a;
    plane = shift;
}

std::string AxisLock::Label() const {
    static const char* names[3] = {"X", "Y", "Z"};
    if (axis < 0) return "";
    if (!plane) return names[axis];
    std::string s;
    for (int i = 0; i < 3; ++i) if (i != axis) s += names[i];
    return s + " plane";
}

bool NumericInput::Type(char c) {
    if (c >= '0' && c <= '9') { m_text += c; return true; }
    if (c == '.' || c == ',') {
        if (m_text.find('.') == std::string::npos) m_text += m_text.empty() ? "0." : ".";
        return true;
    }
    if (c == '-') { m_negative = !m_negative; return true; }
    return false;
}

void NumericInput::Backspace() {
    if (!m_text.empty()) m_text.pop_back();
    else m_negative = false;
}

float NumericInput::Value(float fallback) const {
    if (m_text.empty() || m_text == "0.") return m_text.empty() ? fallback : 0.0f;
    const float v = std::strtof(m_text.c_str(), nullptr);
    return m_negative ? -v : v;
}

float SnapTo(float v, float step) {
    if (step <= 0.0f) return v;
    return std::round(v / step) * step;
}

Mat4 MoveMatrix(const Vec3& offset, const AxisLock& lock) {
    Vec3 o{lock.Allows(0) ? offset.x : 0.0f, lock.Allows(1) ? offset.y : 0.0f, lock.Allows(2) ? offset.z : 0.0f};
    return Mat4::Translate(o);
}

Mat4 RotateMatrix(const Vec3& pivot, const Vec3& axisDir, float degrees) {
    Vec3 a = axisDir;
    if (a.SqrMagnitude() < 1e-12f) return Mat4::Identity();
    a = a.Normalized();
    return Mat4::Translate(pivot) * Mat4::Rotate(Quat::AngleAxis(degrees, a)) * Mat4::Translate(pivot * -1.0f);
}

Mat4 ScaleMatrix(const Vec3& pivot, const AxisLock& lock, float factor) {
    Vec3 s{lock.Allows(0) ? factor : 1.0f, lock.Allows(1) ? factor : 1.0f, lock.Allows(2) ? factor : 1.0f};
    return Mat4::Translate(pivot) * Mat4::Scale(s) * Mat4::Translate(pivot * -1.0f);
}

namespace {
bool RayPlane(const Ray& r, const Vec3& p, const Vec3& n, Vec3& hit) {
    const float d = Vec3::Dot(r.direction, n);
    if (std::fabs(d) < 1e-6f) return false;
    const float t = Vec3::Dot(p - r.origin, n) / d;
    hit = r.origin + r.direction * t;
    return true;
}
// Parameter along the line (p, axis) of the point closest to the ray.
bool RayLineParam(const Ray& r, const Vec3& p, const Vec3& axis, float& s) {
    const Vec3 w = p - r.origin;
    const float a = Vec3::Dot(axis, axis), b = Vec3::Dot(axis, r.direction), c = Vec3::Dot(r.direction, r.direction);
    const float d = Vec3::Dot(axis, w), e = Vec3::Dot(r.direction, w);
    const float den = a * c - b * b;
    if (std::fabs(den) < 1e-8f) return false;   // looking straight down the axis
    s = (b * e - c * d) / den;
    return true;
}
Vec3 Unit(int a) { return Vec3{a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f}; }
} // namespace

Vec3 MoveFromRays(const AxisLock& lock, const Vec3& pivot, const Vec3& viewDir, const Ray& start, const Ray& now) {
    if (!lock.Free() && !lock.plane) {
        const Vec3 ax = Unit(lock.axis);
        float s0 = 0.0f, s1 = 0.0f;
        if (!RayLineParam(start, pivot, ax, s0) || !RayLineParam(now, pivot, ax, s1)) return Vec3{0, 0, 0};
        return ax * (s1 - s0);
    }
    const Vec3 n = lock.Free() ? viewDir : Unit(lock.axis);
    Vec3 h0, h1;
    if (!RayPlane(start, pivot, n, h0) || !RayPlane(now, pivot, n, h1)) return Vec3{0, 0, 0};
    Vec3 d = h1 - h0;
    if (!lock.Free()) (&d.x)[lock.axis] = 0.0f;
    return d;
}

float ScreenAngleDelta(float px, float py, float fx, float fy, float tx, float ty) {
    // Screen y is down, so negate it to get the usual counter-clockwise-positive angle.
    const float a0 = std::atan2(-(fy - py), fx - px);
    const float a1 = std::atan2(-(ty - py), tx - px);
    float d = (a1 - a0) * 57.29577951308232f;
    while (d >= 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

void EditTransformSession::Begin(Mesh& mesh, EditXform kind, const std::vector<int>& selected, const Mat4& toWorld, const Options& opt) {
    m_kind = kind;
    m_toWorld = toWorld;
    m_toLocal = toWorld.Inverse();
    lock = AxisLock{};
    typed.Clear();
    accumulatedDegrees = 0.0f;
    m_verts.clear(); m_orig.clear(); m_weight.clear(); m_mirror.clear(); m_group.clear(); m_groupPivot.clear();
    std::vector<int> source;   // per moved vertex: the selected vertex whose part it follows
    m_name = mesh.name;
    m_normals = mesh.normals;

    const int n = (int)mesh.vertices.size();
    std::vector<char> isSel(n, 0);
    for (int v : selected) if (v >= 0 && v < n) isSel[v] = 1;
    std::unordered_map<int, std::size_t> slot;   // vertex -> index in m_verts
    auto add = [&](int v, float w, int mirrored, int src) {
        auto it = slot.find(v);
        if (it != slot.end()) {   // already moved: keep the strongest pull
            if (w > m_weight[it->second]) { m_weight[it->second] = w; m_mirror[it->second] = mirrored; source[it->second] = src; }
            return;
        }
        slot[v] = m_verts.size();
        m_verts.push_back(v); m_orig.push_back(mesh.vertices[v]); m_weight.push_back(w); m_mirror.push_back((char)mirrored); source.push_back(src);
    };
    std::vector<Vec3> anchors;
    std::vector<int> anchorIds;
    for (int v = 0; v < n; ++v) if (isSel[v]) { add(v, 1.0f, 0, v); anchors.push_back(mesh.vertices[v]); anchorIds.push_back(v); }

    // Soft selection: every other vertex within the radius, smoothstep falloff
    // by distance to the nearest selected vertex (as Move Soft does).
    if (opt.softRadius > 1e-6f && !anchors.empty()) {
        for (int v = 0; v < n; ++v) {
            if (isSel[v]) continue;
            float best = 1e30f; int near = anchorIds[0];
            for (std::size_t k = 0; k < anchors.size(); ++k) {
                const float d2 = (mesh.vertices[v] - anchors[k]).SqrMagnitude();
                if (d2 < best) { best = d2; near = anchorIds[k]; }
            }
            const float d = std::sqrt(best);
            if (d >= opt.softRadius) continue;
            const float t = 1.0f - d / opt.softRadius;
            add(v, t * t * (3.0f - 2.0f * t), 0, near);
        }
    }
    // Symmetry: each moved vertex's partner across the enabled planes (and across combinations of them) gets the
    // reflected transform. A vertex on a plane is its own partner for that plane and is left as is.
    const int axes = (opt.mirrorX ? 1 : 0) | (opt.mirrorY ? 2 : 0) | (opt.mirrorZ ? 4 : 0);
    if (axes) {
        auto qk = [](const Vec3& p) { return std::make_tuple((long)std::lround(p.x * 1e4f), (long)std::lround(p.y * 1e4f), (long)std::lround(p.z * 1e4f)); };
        std::map<std::tuple<long, long, long>, std::vector<int>> where;
        for (int w = 0; w < n; ++w) where[qk(mesh.vertices[w])].push_back(w);
        const std::size_t count = m_verts.size();
        for (std::size_t i = 0; i < count; ++i) {
            for (int mask = 1; mask <= 7; ++mask) {
                if ((mask & axes) != mask) continue;
                const Vec3& o = m_orig[i];
                bool onPlane = false;
                if ((mask & 1) && std::fabs(o.x) < 1e-5f) onPlane = true;
                if ((mask & 2) && std::fabs(o.y) < 1e-5f) onPlane = true;
                if ((mask & 4) && std::fabs(o.z) < 1e-5f) onPlane = true;
                if (onPlane) continue;
                const Vec3 want{(mask & 1) ? -o.x : o.x, (mask & 2) ? -o.y : o.y, (mask & 4) ? -o.z : o.z};
                auto it = where.find(qk(want));
                if (it == where.end()) continue;
                for (int w : it->second)
                    if (w != m_verts[i] && (mesh.vertices[w] - want).SqrMagnitude() < 1e-8f && !isSel[w]) add(w, m_weight[i], mask, source[i]);
            }
        }
    }

    Vec3 c{0, 0, 0};
    for (int v : selected) if (v >= 0 && v < n) c = c + mesh.vertices[v];
    const int sc = (int)anchors.size();
    m_pivotLocal = sc > 0 ? c * (1.0f / (float)sc) : Vec3{0, 0, 0};
    if (opt.pivot == EditPivot::Active && opt.activeVertex >= 0 && opt.activeVertex < n && isSel[opt.activeVertex])
        m_pivotLocal = mesh.vertices[opt.activeVertex];
    m_pivotWorld = toWorld.MultiplyPoint(m_pivotLocal);

    // Individual Origins: split the selection into parts (joined by a triangle edge between two selected
    // vertices, or by sitting at the same place, so an unwelded mesh's split corners stay together).
    if (opt.pivot == EditPivot::Individual && sc > 0) {
        std::vector<int> parent(n);
        for (int v = 0; v < n; ++v) parent[v] = v;
        auto root = [&](int v) { while (parent[v] != v) v = parent[v] = parent[parent[v]]; return v; };
        auto join = [&](int a, int b) { a = root(a); b = root(b); if (a != b) parent[std::max(a, b)] = std::min(a, b); };
        const std::size_t nt = mesh.triangles.size() / 3;
        auto joinTri = [&](std::size_t t) {
            for (int k = 0; k < 3; ++k) {
                const int a = mesh.triangles[t * 3 + k], b = mesh.triangles[t * 3 + (k + 1) % 3];
                if (a >= 0 && b >= 0 && a < n && b < n && isSel[a] && isSel[b]) join(a, b);
            }
        };
        // Face mode: only the selected triangles' edges join; Edge mode: only the selected edges; else any
        // edge between two selected vertices (Vertex mode).
        std::vector<char> pinned(n, 0);   // vertices the face / edge list touches: only those join by position
        if (opt.faces) {
            for (int t : *opt.faces) {
                if (t < 0 || (std::size_t)t >= nt) continue;
                joinTri((std::size_t)t);
                for (int k = 0; k < 3; ++k) { const int v = mesh.triangles[(std::size_t)t * 3 + k]; if (v >= 0 && v < n) pinned[v] = 1; }
            }
        } else if (opt.edges) {
            for (const auto& e : *opt.edges)
                if (e.first >= 0 && e.second >= 0 && e.first < n && e.second < n && isSel[e.first] && isSel[e.second]) {
                    join(e.first, e.second); pinned[e.first] = pinned[e.second] = 1;
                }
        } else {
            for (std::size_t t = 0; t < nt; ++t) joinTri(t);
        }
        const bool anyList = opt.faces || opt.edges;
        std::map<std::tuple<long, long, long>, int> at;
        for (int v : anchorIds) {
            if (anyList && !pinned[v]) continue;
            const Vec3& p = mesh.vertices[v];
            if (!std::isfinite(p.x + p.y + p.z)) continue;
            const auto key = std::make_tuple((long)std::lround(p.x * 1e5f), (long)std::lround(p.y * 1e5f), (long)std::lround(p.z * 1e5f));
            auto it = at.find(key);
            if (it == at.end()) at.emplace(key, v); else join(v, it->second);
        }
        std::unordered_map<int, int> groupOf;   // root -> group number
        std::vector<Vec3> sum; std::vector<int> count;
        for (int v : anchorIds) {
            const int r = root(v);
            auto it = groupOf.find(r);
            if (it == groupOf.end()) { it = groupOf.emplace(r, (int)sum.size()).first; sum.push_back(Vec3{0, 0, 0}); count.push_back(0); }
            sum[it->second] = sum[it->second] + mesh.vertices[v]; ++count[it->second];
        }
        for (std::size_t g = 0; g < sum.size(); ++g) m_groupPivot.push_back(sum[g] * (1.0f / (float)count[g]));
        m_group.assign(m_verts.size(), -1);
        for (std::size_t i = 0; i < m_verts.size(); ++i) {
            const int src = source[i];
            if (src >= 0 && src < n && isSel[src]) m_group[i] = groupOf[root(src)];
        }
    }
    m_active = !m_verts.empty();
}

void EditTransformSession::Apply(Mesh& mesh, const Mat4& world) {
    if (!m_active) return;
    const Mat4 local = m_toLocal * world * m_toWorld;   // the same move, in the mesh's own space
    Mat4 mirrored[8];                                  // the same move reflected across each combination of local planes
    for (int mask = 1; mask <= 7; ++mask) {
        const Mat4 S = Mat4::Scale(Vec3{(mask & 1) ? -1.0f : 1.0f, (mask & 2) ? -1.0f : 1.0f, (mask & 4) ? -1.0f : 1.0f});
        mirrored[mask] = S * local * S;
    }
    // Individual Origins: the same move carried from the shared pivot onto each part's own centre.
    std::vector<Mat4> perGroup;
    for (const Vec3& pg : m_groupPivot) perGroup.push_back(Mat4::Translate(pg - m_pivotLocal) * local * Mat4::Translate(m_pivotLocal - pg));
    for (std::size_t i = 0; i < m_verts.size(); ++i) {
        const int v = m_verts[i];
        if (v < 0 || v >= (int)mesh.vertices.size()) continue;
        const Vec3 o = m_orig[i];
        const int g = i < m_group.size() ? m_group[i] : -1;
        Mat4 L = g >= 0 && g < (int)perGroup.size() ? perGroup[(std::size_t)g] : local;
        if (m_mirror[i]) {
            const int mask = (int)m_mirror[i] & 7;
            if (g >= 0) {
                const Mat4 S = Mat4::Scale(Vec3{(mask & 1) ? -1.0f : 1.0f, (mask & 2) ? -1.0f : 1.0f, (mask & 4) ? -1.0f : 1.0f});
                L = S * L * S;
            } else L = mirrored[mask];
        }
        const Vec3 moved = L.MultiplyPoint(o);
        mesh.vertices[v] = o + (moved - o) * m_weight[i];
    }
    mesh.name = "";
    mesh.normals.clear();   // flat shading while editing, as the other edit tools do
}

void EditTransformSession::Restore(Mesh& mesh) const {
    for (std::size_t i = 0; i < m_verts.size(); ++i)
        if (m_verts[i] >= 0 && m_verts[i] < (int)mesh.vertices.size()) mesh.vertices[m_verts[i]] = m_orig[i];
}

void EditTransformSession::Cancel(Mesh& mesh) {
    if (!m_active) return;
    Restore(mesh);
    mesh.name = m_name;
    mesh.normals = m_normals;
    m_active = false;
}

} // namespace okay::modeling
