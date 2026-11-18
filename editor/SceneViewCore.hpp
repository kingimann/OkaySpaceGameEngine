#pragma once
// The picture of a scene, without any UI: the part of the Scene and Game views that is not Dear ImGui (docs/own_ui_toolkit.md 6.24 step 6).
// Written from what main.cpp's DrawScene3D / Render3DTexture do for the camera and the software renderer call (okay::RenderMeshesSS), as plain functions
// over plain values so the window-mode panels (OkayUIHostPanels.cpp) and tests can use them. It does not replace the Dear ImGui editor's own path:
// that one also draws the grid, gizmos and labels, picks the GPU renderer and handles ortho and axis views, which this does not do yet.
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>
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

} // namespace sceneview
} // namespace oked
