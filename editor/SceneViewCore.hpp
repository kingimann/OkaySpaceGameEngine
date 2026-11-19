#pragma once
// The picture of a scene, without any UI: the part of the Scene and Game views that is not Dear ImGui (docs/own_ui_toolkit.md 6.24 step 6).
// Written from what main.cpp's DrawScene3D / Render3DTexture do for the camera and the software renderer call (okay::RenderMeshesSS), as plain functions
// over plain values so the window-mode panels (OkayUIHostPanels.cpp) and tests can use them. It does not replace the Dear ImGui editor's own path:
// that one also draws the grid, gizmos and labels, picks the GPU renderer and handles ortho and axis views, which this does not do yet.
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Lightmap.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace oked {
namespace sceneview {

/// The orbit camera the Scene view uses (the values EditorState keeps: yaw and pitch in degrees, distance from the target).
struct OrbitCamera {
    okay::Vec3 target{0, 0, 0};
    float yawDeg = 35.0f, pitchDeg = 22.0f, dist = 14.0f;
    float fovDeg = 60.0f, nearZ = 0.3f, farZ = 2000.0f;
};

/// What one picture is rendered with.
struct Frame {
    okay::Mat4 vp;
    okay::Vec3 eye{0, 0, 0};
    int w = 0, h = 0;
};

/// Pixel size of the render target for a panel: scaled by `scale` (0.25 to 1), each side at least 1 and the long side at most `maxEdge` (the editor uses 1920).
inline void FitSize(int panelW, int panelH, float scale, int maxEdge, int& w, int& h) {
    scale = scale < 0.25f ? 0.25f : (scale > 1.0f ? 1.0f : scale);
    w = std::max(1, (int)((float)std::max(panelW, 1) * scale));
    h = std::max(1, (int)((float)std::max(panelH, 1) * scale));
    const int longEdge = std::max(w, h);
    if (maxEdge > 0 && longEdge > maxEdge) {
        const float k = (float)maxEdge / (float)longEdge;
        w = std::max(1, (int)((float)w * k)); h = std::max(1, (int)((float)h * k));
    }
}

/// The Scene view's camera: the eye on a sphere of radius `dist` around the target, looking at the target.
inline Frame EditorFrame(const OrbitCamera& c, int w, int h) {
    const float yawR = c.yawDeg * okay::Mathf::Deg2Rad, pitchR = c.pitchDeg * okay::Mathf::Deg2Rad;
    const okay::Vec3 dir{okay::Mathf::Cos(pitchR) * okay::Mathf::Sin(yawR), okay::Mathf::Sin(pitchR), okay::Mathf::Cos(pitchR) * okay::Mathf::Cos(yawR)};
    Frame f;
    f.eye = c.target + dir * c.dist;
    f.w = std::max(w, 1); f.h = std::max(h, 1);
    f.vp = okay::Mat4::Perspective(c.fovDeg, (float)f.w / (float)f.h, c.nearZ, c.farZ) * okay::Mat4::LookAt(f.eye, c.target, okay::Vec3::Up);
    return f;
}

/// The camera the Game view looks through: the scene's main camera if it can render, else the best active one (the game's own choice), or null.
inline okay::Camera* GameCamera(okay::Scene& s) {
    std::vector<okay::Camera*> cams;
    for (const auto& go : s.Objects())
        if (auto* c = go->GetComponent<okay::Camera>()) cams.push_back(c);
    s.mainCamera = okay::PickRenderCamera(s.mainCamera, cams);
    return s.mainCamera;
}

/// The Game view's frame through `cam` (its own field of view and projection), or false when the camera has no transform.
inline bool GameFrame(const okay::Camera* cam, int w, int h, Frame& f) {
    if (!cam || !cam->gameObject || !cam->gameObject->transform) return false;
    f.w = std::max(w, 1); f.h = std::max(h, 1);
    f.eye = cam->gameObject->transform->Position();
    f.vp = cam->ProjectionMatrix((float)f.w / (float)f.h) * cam->ViewMatrix();
    return true;
}

/// Renders frames with the engine's software renderer and keeps its buffers between frames. The pixels are RGBA bytes in memory (ABGR as a 32-bit value),
/// transparent where nothing is drawn, row 0 at the top. The pointer stays valid until the next Render.
class Renderer {
public:
    const std::uint32_t* Render(const okay::Scene& scene, const Frame& f, int supersample = 1, const okay::GameObject* ignore = nullptr) {
        okay::ApplySceneLight(scene);   // a Light object aims the shading
        return okay::RenderMeshesSS(m_raster, m_down, scene, f.vp, f.eye, f.w, f.h, supersample < 1 ? 1 : supersample, ignore);
    }
private:
    okay::Raster m_raster;
    std::vector<std::uint32_t> m_down;
};

/// True when any pixel of a w*h picture is not fully transparent.
inline bool AnyPixel(const std::uint32_t* px, int w, int h) {
    if (!px) return false;
    for (long i = 0, n = (long)w * h; i < n; ++i) if (px[i] >> 24) return true;
    return false;
}


// ---- Grid, picking and gizmo math (round 11): plain functions over a Frame, no UI. Written from standard projection and ray geometry. ----

/// A point in window pixels (origin top-left) of a w*h picture; false when the point is behind the camera.
inline bool ToScreen(const Frame& f, const okay::Vec3& p, float& x, float& y) {
    const okay::Vec4 c = f.vp * okay::Vec4(p.x, p.y, p.z, 1.0f);
    if (c.w <= 0.05f) return false;
    x = (c.x / c.w * 0.5f + 0.5f) * (float)f.w;
    y = (0.5f - c.y / c.w * 0.5f) * (float)f.h;
    return true;
}

/// A line segment in window pixels.
struct Seg { float x0, y0, x1, y1; };

/// A world segment as a screen segment, cut at the near plane like the mesh renderer does (z + w >= 0), so a line that starts behind the camera still draws its visible part.
inline bool SegToScreen(const Frame& f, const okay::Vec3& a, const okay::Vec3& b, Seg& out) {
    okay::Vec4 ca = f.vp * okay::Vec4(a.x, a.y, a.z, 1.0f), cb = f.vp * okay::Vec4(b.x, b.y, b.z, 1.0f);
    const float da = ca.z + ca.w, db = cb.z + cb.w;
    if (da < 0.0f && db < 0.0f) return false;
    if (da < 0.0f) { const float t = da / (da - db); ca = okay::Vec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t); }
    else if (db < 0.0f) { const float t = db / (db - da); cb = okay::Vec4(cb.x + (ca.x - cb.x) * t, cb.y + (ca.y - cb.y) * t, cb.z + (ca.z - cb.z) * t, cb.w + (ca.w - cb.w) * t); }
    if (ca.w <= 1e-4f || cb.w <= 1e-4f) return false;
    out = {(ca.x / ca.w * 0.5f + 0.5f) * (float)f.w, (0.5f - ca.y / ca.w * 0.5f) * (float)f.h, (cb.x / cb.w * 0.5f + 0.5f) * (float)f.w, (0.5f - cb.y / cb.w * 0.5f) * (float)f.h};
    return true;
}

/// The ground grid on the XZ plane: lines every `step` units out to `half` units around the target's cell, `major` marks every n-th line (the axes lines are major).
struct GridLine { Seg s; bool major = false; bool xAxis = false, zAxis = false; };
inline std::vector<GridLine> GroundGrid(const Frame& f, const okay::Vec3& around, float step, int cells, int majorEvery = 5) {
    std::vector<GridLine> out;
    if (!(step > 0.0f) || cells < 1) return out;
    const float cx = std::floor(around.x / step) * step, cz = std::floor(around.z / step) * step, half = step * (float)cells;
    for (int i = -cells; i <= cells; ++i) {
        const float x = cx + step * (float)i, z = cz + step * (float)i;
        GridLine l; l.major = majorEvery > 0 && (((int)std::lround(x / step)) % majorEvery == 0);
        if (SegToScreen(f, {x, 0, cz - half}, {x, 0, cz + half}, l.s)) { l.zAxis = std::fabs(x) < 1e-4f; out.push_back(l); }
        GridLine m; m.major = majorEvery > 0 && (((int)std::lround(z / step)) % majorEvery == 0);
        if (SegToScreen(f, {cx - half, 0, z}, {cx + half, 0, z}, m.s)) { m.xAxis = std::fabs(z) < 1e-4f; out.push_back(m); }
    }
    return out;
}

/// A world ray through window pixel (px, py) of a Frame.
struct Ray3 { okay::Vec3 o, d; };
inline bool PixelRay(const Frame& f, float px, float py, Ray3& r) {
    const okay::Mat4 inv = f.vp.Inverse();
    const float nx = px / (float)f.w * 2.0f - 1.0f, ny = 1.0f - py / (float)f.h * 2.0f;
    okay::Vec4 a = inv * okay::Vec4(nx, ny, -1.0f, 1.0f), b = inv * okay::Vec4(nx, ny, 1.0f, 1.0f);
    if (std::fabs(a.w) < 1e-12f || std::fabs(b.w) < 1e-12f) return false;
    const okay::Vec3 pa{a.x / a.w, a.y / a.w, a.z / a.w}, pb{b.x / b.w, b.y / b.w, b.z / b.w};
    okay::Vec3 d = pb - pa; const float len = std::sqrt(okay::Vec3::Dot(d, d));
    if (!(len > 1e-9f)) return false;
    r.o = pa; r.d = d * (1.0f / len);
    return true;
}

/// The nearest object under window pixel (px, py): every active mesh renderer's triangles in world space against the pixel's ray. Null when nothing is hit.
inline okay::GameObject* Pick(const okay::Scene& scene, const Frame& f, float px, float py, float* hitDistance = nullptr) {
    Ray3 r; if (!PixelRay(f, px, py, r)) return nullptr;
    okay::GameObject* best = nullptr; float bestT = 1e30f;
    for (const auto& up : scene.Objects()) {
        okay::GameObject* go = up.get();
        if (!go || !go->active || !go->transform) continue;
        auto* mr = go->GetComponent<okay::MeshRenderer>();
        if (!mr || !mr->enabled) continue;
        const okay::Mesh& m = mr->DisplayMesh();
        const okay::Mat4 M = go->transform->LocalToWorldMatrix();
        for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
            const int a = m.triangles[i], b = m.triangles[i + 1], c = m.triangles[i + 2];
            if (a < 0 || b < 0 || c < 0 || (std::size_t)a >= m.vertices.size() || (std::size_t)b >= m.vertices.size() || (std::size_t)c >= m.vertices.size()) continue;
            auto w = [&](int k) { const okay::Vec4 v = M * okay::Vec4(m.vertices[k].x, m.vertices[k].y, m.vertices[k].z, 1.0f); return okay::Vec3{v.x, v.y, v.z}; };
            float t;
            if (okay::RayTriangle(r.o, r.d, w(a), w(b), w(c), bestT, t) && t < bestT) { bestT = t; best = go; }
        }
    }
    if (best && hitDistance) *hitDistance = bestT;
    return best;
}

/// The 12 edges of an object's world-space bounding box as screen segments (its selection outline). Empty when it has no mesh.
inline std::vector<Seg> BoundsOutline(const okay::GameObject& go, const Frame& f) {
    std::vector<Seg> out;
    auto* mr = go.GetComponent<okay::MeshRenderer>();
    if (!mr || !go.transform) return out;
    const okay::Mesh& m = mr->DisplayMesh();
    if (m.vertices.empty()) return out;
    okay::Vec3 lo = m.vertices[0], hi = m.vertices[0];
    for (const auto& v : m.vertices) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
    const okay::Mat4 M = go.transform->LocalToWorldMatrix();
    okay::Vec3 c[8];
    for (int i = 0; i < 8; ++i) { const okay::Vec4 v = M * okay::Vec4((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z, 1.0f); c[i] = {v.x, v.y, v.z}; }
    static const int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for (auto& k : e) { Seg s; if (SegToScreen(f, c[k[0]], c[k[1]], s)) out.push_back(s); }
    return out;
}

// ---- Gizmos: the drag maths for Move, Rotate and Scale along one world axis ----
enum class Axis { X = 0, Y = 1, Z = 2 };
inline okay::Vec3 AxisDir(Axis a) { return a == Axis::X ? okay::Vec3{1, 0, 0} : a == Axis::Y ? okay::Vec3{0, 1, 0} : okay::Vec3{0, 0, 1}; }

/// The parameter t along the line (origin + dir * t) that is closest to the ray (`dir` unit). False when the two are parallel.
inline bool ClosestOnAxis(const okay::Vec3& origin, const okay::Vec3& dir, const Ray3& ray, float& t) {
    const okay::Vec3 w0 = origin - ray.o;
    const float a = okay::Vec3::Dot(dir, dir), b = okay::Vec3::Dot(dir, ray.d), c = okay::Vec3::Dot(ray.d, ray.d);
    const float d = okay::Vec3::Dot(dir, w0), e = okay::Vec3::Dot(ray.d, w0);
    const float den = a * c - b * b;
    if (std::fabs(den) < 1e-6f) return false;
    t = (b * e - c * d) / den;
    return true;
}

/// Move: the new position of an object grabbed at parameter `grabT` along its axis when the mouse ray now closest-hits at parameter `nowT`.
inline okay::Vec3 MoveAlong(const okay::Vec3& startPos, Axis a, float grabT, float nowT) { return startPos + AxisDir(a) * (nowT - grabT); }

/// Scale: the factor that stretches the object along the axis by the ratio of the distances from its centre (held to 0.01 .. 100).
inline float ScaleFactor(float grabT, float nowT) {
    if (std::fabs(grabT) < 1e-4f) return 1.0f;
    float k = nowT / grabT; if (k < 0.01f) k = 0.01f; if (k > 100.0f) k = 100.0f; return k;
}

/// Rotate: the signed angle in degrees about the world axis through `centre` between the grab ray's hit and the current ray's hit on the plane normal to the axis.
inline bool PlaneHit(const okay::Vec3& centre, const okay::Vec3& n, const Ray3& r, okay::Vec3& hit) {
    const float den = okay::Vec3::Dot(n, r.d);
    if (std::fabs(den) < 1e-6f) return false;
    const float t = okay::Vec3::Dot(n, centre - r.o) / den;
    if (t < 0.0f) return false;
    hit = r.o + r.d * t; return true;
}
inline bool RotateAngle(const okay::Vec3& centre, Axis a, const Ray3& grab, const Ray3& now, float& degrees) {
    const okay::Vec3 n = AxisDir(a); okay::Vec3 g, c;
    if (!PlaneHit(centre, n, grab, g) || !PlaneHit(centre, n, now, c)) return false;
    const okay::Vec3 u = g - centre, v = c - centre;
    const float cr = okay::Vec3::Dot(n, okay::Vec3::Cross(u, v)), dt = okay::Vec3::Dot(u, v);
    degrees = std::atan2(cr, dt) * 57.29577951f;
    return true;
}

} // namespace sceneview
} // namespace oked
