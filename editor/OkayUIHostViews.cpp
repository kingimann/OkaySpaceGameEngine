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
    bool showGrid = true;
    GizmoMode mode = GizmoMode::Move;
    // a press over the picture: a click (pick) unless it moves, or a gizmo drag when it started on a handle
    bool pressed = false; float downX = 0, downY = 0; bool moved = false;
    bool gizmo = false; sceneview::Axis axis = sceneview::Axis::X; float grabT = 0;
    okay::Vec3 startPos, startScale; okay::Quat startRot; sceneview::Ray3 grabRay;
};

void Draw(ViewWindow& v, HostModeState* st, const char* title) {
    using namespace OkayUI;
    ViewProbe& pr = g_probes[v.game ? 1 : 0];
    if (!DockBegin(title, st ? &st->OpenRef(title) : nullptr)) { DockEnd(); return; }
    okay::Scene* sc = v.src.scene ? v.src.scene() : nullptr;
    pr.note = "";
    if (!v.game) {   // the toolbar: Move / Rotate / Scale and the grid
        const char* names[3] = {"Move", "Rotate", "Scale"};
        for (int i = 0; i < 3; ++i) {
            if (i) SameLine();
            const bool on = (int)v.mode == i;
            if (on) PushStyleColor(Col_Button, 70, 120, 200);
            if (Button(names[i])) v.mode = (GizmoMode)i;
            pr.modeButton[i] = GetItemRect();
            if (on) PopStyleColor();
        }
        SameLine(); Checkbox("Grid", &v.showGrid); pr.gridCheck = GetItemRect();
        pr.mode = v.mode;
    }
    const Vec2 avail = GetContentRegionAvail();
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
    if (!v.game) {
        const float sx = avail.x / (float)tw, sy = avail.y / (float)th;   // picture pixels -> window pixels
        const DrawListHandle& dl = GetWindowDrawList();
        auto line = [&](const sceneview::Seg& g, Color32 col, float th2) { dl->AddLine(Vec2{p.x + g.x0 * sx, p.y + g.y0 * sy}, Vec2{p.x + g.x1 * sx, p.y + g.y1 * sy}, col, th2); };
        okay::GameObject* sel = v.src.selected ? v.src.selected() : nullptr;
        pr.gridLines = 0; pr.outlineEdges = 0; pr.gizmoAxes = 0;
        if (v.showGrid) {
            const float step = *v.src.dist > 60.0f ? 5.0f : 1.0f;
            const okay::Vec3 around = v.src.target ? *v.src.target : okay::Vec3{0, 0, 0};
            for (const auto& g : sceneview::GroundGrid(f, around, step, 20)) {
                line(g.s, g.xAxis ? RGBA(200, 70, 70, 190) : g.zAxis ? RGBA(70, 110, 220, 190) : g.major ? RGBA(120, 124, 132, 150) : RGBA(80, 84, 92, 110), 1.0f);
                ++pr.gridLines;
            }
        }
        if (sel) for (const auto& g : sceneview::BoundsOutline(*sel, f)) { line(g, RGBA(255, 170, 40, 255), 1.5f); ++pr.outlineEdges; }
        // the gizmo handles: one per world axis from the object's position, a fixed fraction of the orbit distance long
        okay::Vec3 centre{0, 0, 0}; float len = 1.0f; bool haveGizmo = false;
        const Color32 axisCol[3] = {RGBA(230, 70, 70, 255), RGBA(80, 200, 90, 255), RGBA(80, 130, 240, 255)};
        float ex[3] = {0, 0, 0}, ey[3] = {0, 0, 0}, cx0 = 0, cy0 = 0;
        if (sel && sel->transform) {
            centre = sel->transform->Position(); len = *v.src.dist * 0.18f;
            haveGizmo = sceneview::ToScreen(f, centre, cx0, cy0);
            for (int a = 0; haveGizmo && a < 3; ++a) {
                const okay::Vec3 tip = centre + sceneview::AxisDir((sceneview::Axis)a) * len;
                if (!sceneview::ToScreen(f, tip, ex[a], ey[a])) { haveGizmo = false; break; }
            }
        }
        const Input& in = GetFrameInput();
        const float mx = (in.mouseX - p.x) / sx, my = (in.mouseY - p.y) / sy;   // the mouse in picture pixels
        auto nearSeg = [&](int a) {
            const float vx = ex[a] - cx0, vy = ey[a] - cy0, wx = mx - cx0, wy = my - cy0, l2 = vx * vx + vy * vy;
            float t = l2 > 0 ? (vx * wx + vy * wy) / l2 : 0; t = t < 0 ? 0 : (t > 1 ? 1 : t);
            const float dx = cx0 + vx * t - mx, dy = cy0 + vy * t - my; return std::sqrt(dx * dx + dy * dy);
        };
        if (haveGizmo) {
            for (int a = 0; a < 3; ++a) {
                const bool hot = v.gizmo && (int)v.axis == a;
                line(sceneview::Seg{cx0, cy0, ex[a], ey[a]}, axisCol[a], hot ? 3.5f : 2.0f);
                dl->AddCircleFilled(Vec2{p.x + ex[a] * sx, p.y + ey[a] * sy}, v.mode == GizmoMode::Scale ? 4.0f : 5.0f, axisCol[a]);
                pr.gizmoHandle[a] = Rect{p.x + ex[a] * sx - 5.0f, p.y + ey[a] * sy - 5.0f, 10.0f, 10.0f};
                ++pr.gizmoAxes;
            }
        }
        const bool over = in.mouseX >= p.x && in.mouseX < p.x + avail.x && in.mouseY >= p.y && in.mouseY < p.y + avail.y && IsItemHovered();
        const bool left = in.mouseDown, right = in.rightDown, down = left || right;
        sceneview::Ray3 nowRay; const bool haveRay = sceneview::PixelRay(f, mx, my, nowRay);
        if (left && !v.pressed && !v.dragging && !v.gizmo && over) {   // a press on the picture
            v.pressed = true; v.downX = in.mouseX; v.downY = in.mouseY; v.moved = false;
            int hit = -1; float best = 9.0f;
            if (haveGizmo) for (int a = 0; a < 3; ++a) { const float d = nearSeg(a); if (d < best) { best = d; hit = a; } }
            if (hit >= 0 && haveRay && sel) {
                v.axis = (sceneview::Axis)hit; v.gizmo = true; v.pressed = false;
                v.startPos = sel->transform->Position(); v.startScale = sel->transform->localScale; v.startRot = sel->transform->Rotation(); v.grabRay = nowRay;
                if (!sceneview::ClosestOnAxis(v.startPos, sceneview::AxisDir(v.axis), nowRay, v.grabT)) v.grabT = 0;
                if (v.src.beginEdit) v.src.beginEdit(v.mode == GizmoMode::Move ? "Move" : v.mode == GizmoMode::Rotate ? "Rotate" : "Scale");
            }
        } else if (right && !v.pressed && !v.dragging && !v.gizmo && over) v.dragging = true;
        if (v.gizmo && left && sel && haveRay) {
            const okay::Vec3 dir = sceneview::AxisDir(v.axis);
            float t = 0;
            if (v.mode == GizmoMode::Rotate) {
                float deg = 0;
                if (sceneview::RotateAngle(v.startPos, v.axis, v.grabRay, nowRay, deg)) sel->transform->SetRotation(okay::Quat::AngleAxis(deg, dir) * v.startRot);
            } else if (sceneview::ClosestOnAxis(v.startPos, dir, nowRay, t)) {
                if (v.mode == GizmoMode::Move) sel->transform->SetPosition(sceneview::MoveAlong(v.startPos, v.axis, v.grabT, t));
                else { const float k = sceneview::ScaleFactor(v.grabT, t); okay::Vec3 sc = v.startScale; (v.axis == sceneview::Axis::X ? sc.x : v.axis == sceneview::Axis::Y ? sc.y : sc.z) *= k; sel->transform->localScale = sc; }
            }
            if (v.src.edited) v.src.edited();
        }
        pr.gizmoDragging = v.gizmo;
        if (v.pressed && left) {   // the drag after a press orbits; a press that never moves is a click
            const float ddx = in.mouseX - v.downX, ddy = in.mouseY - v.downY;
            if (ddx * ddx + ddy * ddy > 16.0f) v.moved = true;
            if (v.moved) {
                if (v.src.yaw) *v.src.yaw -= (in.mouseX - v.lastX) * 0.4f;
                if (v.src.pitch) *v.src.pitch = std::max(-85.0f, std::min(85.0f, *v.src.pitch + (in.mouseY - v.lastY) * 0.4f));
            }
        }
        if (v.dragging && right) {
            if (v.src.yaw) *v.src.yaw -= (in.mouseX - v.lastX) * 0.4f;
            if (v.src.pitch) *v.src.pitch = std::max(-85.0f, std::min(85.0f, *v.src.pitch + (in.mouseY - v.lastY) * 0.4f));
        }
        if (v.pressed && !left) {   // released: a click picks (Ctrl toggles), a click on nothing clears
            if (!v.moved && v.src.select && haveRay) { okay::GameObject* hit = sceneview::Pick(*sc, f, mx, my); pr.picked = hit; v.src.select(hit, in.ctrl); }
            v.pressed = false;
        }
        if (!left) v.gizmo = false;
        if (!right) v.dragging = false;
        if (!down) { v.pressed = false; }
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
