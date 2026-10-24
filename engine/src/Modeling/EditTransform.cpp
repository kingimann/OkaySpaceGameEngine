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
    const float d = Vec3::Dot(r.dir, n);
    if (std::fabs(d) < 1e-6f) return false;
    const float t = Vec3::Dot(p - r.origin, n) / d;
    hit = r.origin + r.dir * t;
    return true;
}
// Parameter along the line (p, axis) of the point closest to the ray.
bool RayLineParam(const Ray& r, const Vec3& p, const Vec3& axis, float& s) {
    const Vec3 w = p - r.origin;
    const float a = Vec3::Dot(axis, axis), b = Vec3::Dot(axis, r.dir), c = Vec3::Dot(r.dir, r.dir);
    const float d = Vec3::Dot(axis, w), e = Vec3::Dot(r.dir, w);
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
    m_verts.clear(); m_orig.clear(); m_weight.clear(); m_mirror.clear();
    m_name = mesh.name;
    m_normals = mesh.normals;

    const int n = (int)mesh.vertices.size();
    std::vector<char> isSel(n, 0);
    for (int v : selected) if (v >= 0 && v < n) isSel[v] = 1;
    std::unordered_map<int, std::size_t> slot;   // vertex -> index in m_verts
    auto add = [&](int v, float w, bool mirrored) {
        auto it = slot.find(v);
        if (it != slot.end()) {   // already moved: keep the strongest pull
            if (w > m_weight[it->second]) { m_weight[it->second] = w; m_mirror[it->second] = mirrored; }
            return;
        }
        slot[v] = m_verts.size();
        m_verts.push_back(v); m_orig.push_back(mesh.vertices[v]); m_weight.push_back(w); m_mirror.push_back(mirrored);
    };
    std::vector<Vec3> anchors;
    for (int v = 0; v < n; ++v) if (isSel[v]) { add(v, 1.0f, false); anchors.push_back(mesh.vertices[v]); }

    // Soft selection: every other vertex within the radius, smoothstep falloff
    // by distance to the nearest selected vertex (as Move Soft does).
    if (opt.softRadius > 1e-6f && !anchors.empty()) {
        for (int v = 0; v < n; ++v) {
            if (isSel[v]) continue;
            float best = 1e30f;
            for (const Vec3& a : anchors) best = std::min(best, (mesh.vertices[v] - a).SqrMagnitude());
            const float d = std::sqrt(best);
            if (d >= opt.softRadius) continue;
            const float t = 1.0f - d / opt.softRadius;
            add(v, t * t * (3.0f - 2.0f * t), false);
        }
    }
    // X symmetry: each moved vertex's partner at (-x, y, z) gets the mirrored
    // transform. A vertex on the plane is its own partner and is left as is.
    if (opt.mirrorX) {
        const std::size_t count = m_verts.size();
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3 want{-m_orig[i].x, m_orig[i].y, m_orig[i].z};
            if (std::fabs(m_orig[i].x) < 1e-5f) continue;
            for (int w = 0; w < n; ++w)
                if (w != m_verts[i] && (mesh.vertices[w] - want).SqrMagnitude() < 1e-8f && !isSel[w]) add(w, m_weight[i], true);
        }
    }

    Vec3 c{0, 0, 0};
    for (int v : selected) if (v >= 0 && v < n) c = c + mesh.vertices[v];
    const int sc = (int)anchors.size();
    m_pivotWorld = toWorld.MultiplyPoint(sc > 0 ? c * (1.0f / (float)sc) : Vec3{0, 0, 0});
    m_active = !m_verts.empty();
}

void EditTransformSession::Apply(Mesh& mesh, const Mat4& world) {
    if (!m_active) return;
    const Mat4 local = m_toLocal * world * m_toWorld;   // the same move, in the mesh's own space
    const Mat4 S = Mat4::Scale(Vec3{-1, 1, 1});
    const Mat4 mirrored = S * local * S;              // the same move reflected across local x = 0
    for (std::size_t i = 0; i < m_verts.size(); ++i) {
        const int v = m_verts[i];
        if (v < 0 || v >= (int)mesh.vertices.size()) continue;
        const Vec3 o = m_orig[i];
        const Vec3 moved = (m_mirror[i] ? mirrored : local).MultiplyPoint(o);
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
