// See OkayUIHostViews.hpp. Written for window mode over editor/SceneViewCore.hpp (the engine's software renderer) and OkayUI's Image; no Dear ImGui.
#include "OkayUIHostViews.hpp"
#include "SceneViewCore.hpp"
#include "okay/Platform/Painter.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace oked {
namespace wm {

namespace {
ViewProbe g_probes[2];

struct ViewWindow {
    bool game = false;
    okay::platform::Painter* painter = nullptr;
    ViewSource src;
    sceneview::Renderer renderer;
    okay::ui2d::Tex tex = nullptr;
    int tw = 0, th = 0;
    bool dragging = false; float lastX = 0, lastY = 0;
};

void Draw(ViewWindow& v, HostModeState* st, const char* title) {
    using namespace OkayUI;
    ViewProbe& pr = g_probes[v.game ? 1 : 0];
    if (!DockBegin(title, st ? &st->OpenRef(title) : nullptr)) { DockEnd(); return; }
    okay::Scene* sc = v.src.scene ? v.src.scene() : nullptr;
    const Vec2 avail = GetContentRegionAvail();
    pr.note = "";
    if (!sc) { pr.note = "no scene"; TextDisabled("No scene is open."); DockEnd(); return; }
    if (avail.x < 8.0f || avail.y < 8.0f) { pr.note = "too small"; DockEnd(); return; }
    sceneview::Frame f;
    int tw = 0, th = 0;
    sceneview::FitSize((int)avail.x, (int)avail.y, 1.0f, 1920, tw, th);
    if (v.game) {
        okay::Camera* cam = sceneview::GameCamera(*sc);
        if (!sceneview::GameFrame(cam, tw, th, f)) { pr.note = "no camera"; TextDisabled("No main Camera in the scene."); DockEnd(); return; }
    } else {
        sceneview::OrbitCamera oc;
        if (v.src.yaw) oc.yawDeg = *v.src.yaw;
        if (v.src.pitch) oc.pitchDeg = *v.src.pitch;
        if (v.src.dist) oc.dist = *v.src.dist;
        if (v.src.target) oc.target = *v.src.target;
        f = sceneview::EditorFrame(oc, tw, th);
    }
    const std::uint32_t* px = v.renderer.Render(*sc, f, 1, nullptr);
    if (!px || !v.painter) { pr.note = px ? "no painter" : "no picture"; DockEnd(); return; }
    if (!v.tex || v.tw != tw || v.th != th) {
        if (v.tex) v.painter->DestroyTexture(v.tex);
        okay::platform::TextureDesc d; d.width = tw; d.height = th; d.blend = okay::platform::BlendMode::Blend; d.scale = okay::platform::ScaleMode::Linear; d.streaming = true;
        v.tex = v.painter->CreateTexture(d, nullptr);
        v.tw = tw; v.th = th;
    }
    if (!v.tex) { pr.note = "no texture"; DockEnd(); return; }
    v.painter->UpdateTexture(v.tex, nullptr, px, tw * 4);
    const Vec2 p = GetCursorScreenPos();
    // the backdrop: the camera's background in the Game view, a dark grey in the Scene view (the picture is transparent where nothing is drawn)
    Color32 bg = RGBA(42, 45, 51, 255);
    if (v.game) if (okay::Camera* c = sc->mainCamera) bg = RGBA((unsigned)(c->backgroundColor.r * 255), (unsigned)(c->backgroundColor.g * 255), (unsigned)(c->backgroundColor.b * 255), 255);
    GetWindowDrawList()->AddRectFilled(p, Vec2{p.x + avail.x, p.y + avail.y}, bg);
    Image(reinterpret_cast<SDL_Texture*>(v.tex), avail.x, avail.y);
    pr.image = Rect{p.x, p.y, avail.x, avail.y};
    pr.texW = tw; pr.texH = th; pr.litPixels = sceneview::AnyPixel(px, tw, th) ? 1 : 0;
    if (pr.litPixels) { int n = 0; for (long i = 0, m = (long)tw * th; i < m; ++i) if (px[i] >> 24) ++n; pr.litPixels = n; }
    ++pr.frames;
    if (!v.game) {   // orbit with a drag that started over the picture, zoom with the wheel over it
        const Input& in = GetFrameInput();
        const bool over = in.mouseX >= p.x && in.mouseX < p.x + avail.x && in.mouseY >= p.y && in.mouseY < p.y + avail.y && IsItemHovered();
        const bool down = in.mouseDown || in.rightDown;
        if (down && !v.dragging && over) { v.dragging = true; }
        else if (v.dragging && down) {
            if (v.src.yaw) *v.src.yaw -= (in.mouseX - v.lastX) * 0.4f;
            if (v.src.pitch) *v.src.pitch = std::max(-85.0f, std::min(85.0f, *v.src.pitch + (in.mouseY - v.lastY) * 0.4f));
        }
        if (!down) v.dragging = false;
        if (over && in.wheel != 0.0f && v.src.dist) *v.src.dist = std::max(2.0f, std::min(400.0f, *v.src.dist * (1.0f - in.wheel * 0.1f)));
        v.lastX = in.mouseX; v.lastY = in.mouseY;
    }
    DockEnd();
}
}   // namespace

const ViewProbe& GetViewProbe(bool game) { return g_probes[game ? 1 : 0]; }

void AddSceneViews(HostModeState::Panels& panels, HostModeState* st, okay::platform::Painter* painter, const ViewSource& src) {
    auto make = [&](bool game) {
        auto v = std::make_shared<ViewWindow>();
        v->game = game; v->painter = painter; v->src = src;
        const char* title = game ? kPanelGame : kPanelScene;
        return [v, st, title] { Draw(*v, st, title); };
    };
    panels.scene = make(false);
    panels.game = make(true);
}

} // namespace wm
} // namespace oked
