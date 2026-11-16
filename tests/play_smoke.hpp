#pragma once
// Small helpers the "does it really work" tests share (templates_work and examples_work): capture what the Console
// would show, and find out whether the player would see anything when Play starts.
#include <Okay.hpp>
#include "okay/Components/UIElement.hpp"
#include "okay/Scene/EnsureCamera.hpp"

#include <string>
#include <vector>

namespace okaytest {

/// While one of these is alive the Console's errors and warnings are collected here (and not printed).
struct LogCatch {
    std::vector<std::string> errors, warnings;
    okay::Log::Sink old;
    bool oldConsole;
    LogCatch() : old(okay::Log::sink), oldConsole(okay::Log::console) {
        okay::Log::console = false;
        okay::Log::sink = [this](okay::Log::Level l, const std::string& m) {
            if (l == okay::Log::Level::Error) errors.push_back(m);
            else if (l == okay::Log::Level::Warning) warnings.push_back(m);
        };
    }
    ~LogCatch() { okay::Log::sink = old; okay::Log::console = oldConsole; }
    LogCatch(const LogCatch&) = delete;
    LogCatch& operator=(const LogCatch&) = delete;
};

/// A world point on the screen (the 1280 x 720 the engine uses until a window says otherwise) seen through the scene's
/// main camera. False when the point is behind the camera.
inline bool ScreenOf(const okay::Scene& sc, const okay::Vec3& p, okay::Vec2& out) {
    okay::Camera* cam = sc.mainCamera;
    if (!cam) return false;
    const float w = okay::UICanvas::Width(), h = okay::UICanvas::Height();
    const okay::Mat4 vp = cam->ProjectionMatrix(w / h) * cam->ViewMatrix();
    const okay::Vec4 clip = vp * okay::Vec4{p.x, p.y, p.z, 1.0f};
    if (clip.w <= 1e-5f) return false;
    out = {(clip.x / clip.w * 0.5f + 0.5f) * w, (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * h};
    return true;
}

/// Would the person see anything when Play starts? True when the scene has screen-space UI or text, a terrain, water or
/// particles, or a mesh / sprite whose centre is in front of the main camera and inside the picture. `what` says which.
/// This is a heuristic, a floor under "the camera points at nothing": it does not look at occlusion or size.
inline bool SomethingVisible(const okay::Scene& sc, std::string* what = nullptr) {
    using namespace okay;
    auto say = [&](const std::string& s) { if (what) *what = s; return true; };
    if (!sc.mainCamera) { if (what) *what = "no main camera"; return false; }
    const float w = UICanvas::Width(), h = UICanvas::Height();
    for (const auto& up : sc.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->ActiveInHierarchy()) continue;
        if (IsUIElement(g)) return say("screen UI on " + g->name);
        if (g->GetComponent<Terrain>() || g->GetComponent<VoxelTerrain>() || g->GetComponent<Water>() || g->GetComponent<ParticleSystem>())
            return say("a terrain, water or particles on " + g->name);
        const bool drawn = g->GetComponent<MeshRenderer>() || g->GetComponent<SpriteRenderer>() || g->GetComponent<Tilemap>();
        if (!drawn || !g->transform) continue;
        Vec2 px;
        if (ScreenOf(sc, g->transform->Position(), px) && px.x > -0.1f * w && px.x < 1.1f * w && px.y > -0.1f * h && px.y < 1.1f * h)
            return say(g->name + " is in view");
    }
    if (what) *what = "nothing drawn is in front of the camera";
    return false;
}

/// How many of the scene's drawn objects (an active object with a mesh or a sprite; the camera's own object does not count)
/// have their centre inside the picture. A scene with drawn objects and none in view is a camera pointing away from its
/// content: it runs, and shows only the sky and any on-screen text. (The engine's camera looks down its own -Z, so a camera with
/// no rotation placed at negative z looks away from the origin.)
inline void DrawnInView(const okay::Scene& sc, int& drawn, int& inView) {
    using namespace okay;
    drawn = inView = 0;
    const float w = UICanvas::Width(), h = UICanvas::Height();
    for (const auto& up : sc.Objects()) {
        GameObject* g = up.get();
        if (!g || !g->ActiveInHierarchy() || !g->transform || g->GetComponent<Camera>()) continue;
        if (!(g->GetComponent<MeshRenderer>() || g->GetComponent<SpriteRenderer>())) continue;
        ++drawn;
        // A terrain or a lake is far bigger than its centre point says: the camera standing on it sees it.
        if (g->GetComponent<Terrain>() || g->GetComponent<VoxelTerrain>() || g->GetComponent<Water>()) { ++inView; continue; }
        Vec2 px;
        if (ScreenOf(sc, g->transform->Position(), px) && px.x > 0.0f && px.x < w && px.y > 0.0f && px.y < h) ++inView;
    }
}

}  // namespace okaytest
