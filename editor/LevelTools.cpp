// Level-design tools in the editor: foliage brush, terrain texture layers, decal projector, cable, the
// measuring tool and the orthographic views. See LevelTools.hpp and docs/level_tools.md.
#include "LevelTools.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "InspectorPanel.hpp"   // AssetPathPicker, HighlightAssetDrop
#include "okay/Components/Cable.hpp"
#include "okay/Components/Decal.hpp"
#include "okay/Components/Foliage.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/SurfaceRaycast.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

extern bool g_uiHandled;          // a widget consumed this frame's click (main.cpp)
bool& TerrainSculptRef();         // the terrain sculpt brush's switch (main.cpp)

namespace {

// ---- State -------------------------------------------------------------------------------------
bool  s_foliage = false;          // the foliage brush is on for the selected Foliage object
int   s_tool = 0;                 // 0 paint, 1 erase, 2 rescale
bool  s_layerPaint = false;       // the terrain layer brush is on for the selected Terrain
int   s_layer = 0;                // layer being painted
float s_layerRadius = 6.0f, s_layerStrength = 1.0f, s_layerHardness = 0.5f;
bool  s_measure = false;
Vec3  s_mp[2];
int   s_mn = 0;
bool  s_ortho = false;
const char* s_viewName = "";
int   s_scatterProp = 0, s_scatterCount = 80;
float s_scatterMinScale = 0.7f, s_scatterMaxScale = 1.5f, s_scatterSlope = 35.0f;
float s_scatterMinH = -100.0f, s_scatterMaxH = 100.0f;

const char* kProps[] = {"Tree", "Pine", "PalmTree", "DeadTree", "Bush", "Rock", "Mushroom", "Cactus", "Crystal"};
const int kPropCount = 9;

void Tip(const char* text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text); }

// A project file path box: type, drop from the Project panel, or pick.
bool PathField(const char* label, std::string& s, const char* kind) {
    char buf[512];
    std::strncpy(buf, s.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    bool changed = false;
    if (ImGui::InputText(label, buf, sizeof(buf))) { s = buf; changed = true; }
    if (AssetPathPicker(s, kind)) changed = true;
    HighlightAssetDrop(kind);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) { s = std::string((const char*)p->Data); changed = true; }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

template <class T>
bool ObjectCombo(EditorState& ed, const char* label, std::string& name, const char* none) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name.empty() ? none : name.c_str())) {
        if (ImGui::Selectable(none, name.empty())) { name.clear(); changed = true; }
        for (const auto& up : ed.scene().Objects()) {
            if (!up || up->transient) continue;
            if constexpr (!std::is_same_v<T, Transform>) { if (!up->GetComponent<T>()) continue; }
            ImGui::PushID(up.get());
            if (ImGui::Selectable(up->name.c_str(), up->name == name)) { name = up->name; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// ---- Screen helpers -------------------------------------------------------------------------------
struct View {
    const Mat4* vp; ImVec2 center, size;
    Vec4 Clip(const Vec3& p) const { return (*vp) * Vec4{p.x, p.y, p.z, 1.0f}; }
    ImVec2 Project(const Vec4& c) const { return ImVec2(center.x + (c.x / c.w) * size.x * 0.5f, center.y - (c.y / c.w) * size.y * 0.5f); }
    bool ToScreen(const Vec3& p, ImVec2& out) const {
        const Vec4 c = Clip(p);
        if (c.z + c.w <= 0.0f || c.w <= 1e-5f) return false;
        out = Project(c);
        return true;
    }
    void Line(ImDrawList* dl, const Vec3& wa, const Vec3& wb, ImU32 col, float th) const {
        Vec4 a = Clip(wa), b = Clip(wb);
        const float da = a.z + a.w, db = b.z + b.w;
        if (da <= 0.0f && db <= 0.0f) return;
        auto lerp = [](const Vec4& p, const Vec4& q, float t) {
            return Vec4{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t, p.w + (q.w - p.w) * t};
        };
        if (da <= 0.0f) a = lerp(a, b, (0.0f - da) / (db - da) + 1e-4f);
        else if (db <= 0.0f) b = lerp(b, a, (0.0f - db) / (da - db) + 1e-4f);
        if (a.w <= 1e-5f || b.w <= 1e-5f) return;
        dl->AddLine(Project(a), Project(b), col, th);
    }
};

bool MouseRay(const Mat4& vp, ImVec2 center, ImVec2 size, const ImGuiIO& io, Vec3& o, Vec3& d) {
    const float nx = (io.MousePos.x - center.x) / (size.x * 0.5f), ny = -(io.MousePos.y - center.y) / (size.y * 0.5f);
    const Mat4 inv = vp.Inverse();
    const Vec4 a = inv * Vec4{nx, ny, -1.0f, 1.0f}, b = inv * Vec4{nx, ny, 1.0f, 1.0f};
    if (std::fabs(a.w) < 1e-9f || std::fabs(b.w) < 1e-9f) return false;
    o = {a.x / a.w, a.y / a.w, a.z / a.w};
    const Vec3 f{b.x / b.w, b.y / b.w, b.z / b.w};
    if ((f - o).Magnitude() < 1e-9f) return false;
    d = (f - o).Normalized();
    return true;
}

struct Cast { bool ok = false; Vec3 pos, normal{0, 1, 0}; Terrain* terrain = nullptr; };

// The nearest surface along the ray: terrains by their height field (fast), everything else by triangles.
Cast CastScene(EditorState& ed, const Vec3& o, const Vec3& d) {
    Cast c;
    float best = 1e30f;
    for (const auto& up : ed.scene().Objects()) {
        if (!up || !up->active) continue;
        Terrain* t = up->GetComponent<Terrain>();
        Vec3 p;
        if (!t || !t->Raycast(o, d, 20000.0f, p)) continue;
        const float dist = Vec3::Dot(p - o, d);
        if (dist >= best) continue;
        best = dist; c.ok = true; c.pos = p; c.terrain = t;
        const Mat4 M = t->transform->LocalToWorldMatrix();
        const Vec3 lp = M.Inverse().MultiplyPoint(p);
        c.normal = M.MultiplyVector(t->NormalAt(lp.x, lp.z)).Normalized();
    }
    auto skip = [](const GameObject& g) {
        if (g.GetComponent<Terrain>() || g.GetComponent<Decal>()) return true;
        const Transform* par = g.transform ? g.transform->Parent() : nullptr;
        return par && par->gameObject && par->gameObject->GetComponent<Foliage>() != nullptr;   // a plant's chunk
    };
    SurfaceHit sh;
    if (RaycastSurfaces(ed.scene(), o, d, 20000.0f, sh, skip) && sh.distance < best) {
        c.ok = true; c.pos = sh.position; c.normal = sh.normal; c.terrain = nullptr;
    }
    return c;
}

Foliage::Surface GroundFor(EditorState& ed, Foliage& f, const Cast& c) {
    if (c.terrain) return Foliage::TerrainSurface(*c.terrain);
    return f.SceneSurface(ed.scene(), c.pos.y + 200.0f);
}

void DrawRing(ImDrawList* dl, const View& v, const Cast& c, float radius, const Foliage::Surface& ground, ImU32 col) {
    const int n = 48;
    Vec3 prev; bool havePrev = false, first = true;
    Vec3 firstP;
    for (int k = 0; k <= n; ++k) {
        const float a = 6.2831853f * (float)(k % n) / (float)n;
        const float wx = c.pos.x + std::cos(a) * radius, wz = c.pos.z + std::sin(a) * radius;
        Vec3 p, nrm;
        if (!ground || !ground(wx, wz, p, nrm)) { p = {wx, c.pos.y, wz}; nrm = {0, 1, 0}; }
        p = p + nrm * 0.06f;
        if (havePrev) v.Line(dl, prev, p, col, 2.0f);
        prev = p; havePrev = true;
        if (first) { firstP = p; first = false; }
    }
    ImVec2 s;
    if (v.ToScreen(c.pos + c.normal * 0.06f, s)) dl->AddCircleFilled(s, 3.0f, col);
}

GameObject* SelectedWith(EditorState& ed, bool wantFoliage) {
    GameObject* sel = ed.selected();
    if (!sel) return nullptr;
    if (wantFoliage) return sel->GetComponent<Foliage>() ? sel : nullptr;
    return sel->GetComponent<Terrain>() ? sel : nullptr;
}

// ---- Strokes -------------------------------------------------------------------------------------------
struct Stroke { bool active = false; Vec3 last{0, 0, 0}; double lastPublish = 0.0; };
Stroke s_foliageStroke, s_layerStroke;

void FoliageBrush(EditorState& ed, ImDrawList* dl, const View& v, const Mat4& vp, ImVec2 center, ImVec2 size, bool clickable, ImGuiIO& io) {
    GameObject* go = SelectedWith(ed, true);
    if (!go || ed.isPlaying()) { s_foliageStroke.active = false; return; }
    Foliage* f = go->GetComponent<Foliage>();
    Vec3 o, d;
    const bool haveRay = MouseRay(vp, center, size, io, o, d);
    const bool inView = io.MousePos.x >= center.x - size.x * 0.5f && io.MousePos.x <= center.x + size.x * 0.5f &&
                        io.MousePos.y >= center.y - size.y * 0.5f && io.MousePos.y <= center.y + size.y * 0.5f;
    if (!(clickable || s_foliageStroke.active) || !haveRay || !inView) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_foliageStroke.active = false;
        return;
    }
    const Cast c = CastScene(ed, o, d);
    if (!c.ok) { if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_foliageStroke.active = false; return; }
    const Foliage::Surface ground = GroundFor(ed, *f, c);
    const bool erase = s_tool == 1 || (s_tool == 0 && io.KeyShift);
    const ImU32 col = s_tool == 2 ? IM_COL32(255, 210, 90, 230) : erase ? IM_COL32(255, 90, 90, 230) : IM_COL32(110, 230, 120, 230);
    DrawRing(dl, v, c, f->radius, ground, col);
    if (!s_foliageStroke.active) {
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || io.KeyAlt) return;
        ed.PushUndo(erase ? "Erase Foliage" : s_tool == 2 ? "Rescale Foliage" : "Paint Foliage");
        s_foliageStroke.active = true;
        s_foliageStroke.last = {1e30f, 0, 1e30f};
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { s_foliageStroke.active = false; return; }
    g_uiHandled = true;
    const float dx = c.pos.x - s_foliageStroke.last.x, dz = c.pos.z - s_foliageStroke.last.z;
    if (dx * dx + dz * dz < f->radius * f->radius * 0.1225f) return;     // stamp every 0.35 radius of travel
    s_foliageStroke.last = c.pos;
    int n = 0;
    if (s_tool == 2) n = f->Rescale(c.pos.x, c.pos.z, f->radius);
    else if (erase) n = f->Erase(c.pos.x, c.pos.z, f->radius, 1.0f);
    else {
        const int room = f->Capacity() - f->Count();
        if (room <= 0) ConsoleLog("Foliage budget reached (" + std::to_string(f->Capacity()) + " copies): erase some, or raise Max Triangles.", 1);
        n = f->Paint(c.pos.x, c.pos.z, ground);
    }
    if (n) ed.dirty = true;
    f->EnsureBuilt();
}

void LayerBrush(EditorState& ed, ImDrawList* dl, const View& v, const Mat4& vp, ImVec2 center, ImVec2 size, bool clickable, ImGuiIO& io) {
    GameObject* go = SelectedWith(ed, false);
    Terrain* t = go ? go->GetComponent<Terrain>() : nullptr;
    if (!t || ed.isPlaying() || t->layers.empty()) { s_layerStroke.active = false; return; }
    s_layer = std::min(std::max(s_layer, 0), (int)t->layers.size() - 1);
    Vec3 o, d;
    const bool haveRay = MouseRay(vp, center, size, io, o, d);
    const bool inView = io.MousePos.x >= center.x - size.x * 0.5f && io.MousePos.x <= center.x + size.x * 0.5f &&
                        io.MousePos.y >= center.y - size.y * 0.5f && io.MousePos.y <= center.y + size.y * 0.5f;
    if (!(clickable || s_layerStroke.active) || !haveRay || !inView) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { if (s_layerStroke.active) t->PublishSplat(); s_layerStroke.active = false; }
        return;
    }
    Vec3 hit;
    if (!t->Raycast(o, d, 20000.0f, hit)) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { if (s_layerStroke.active) t->PublishSplat(); s_layerStroke.active = false; }
        return;
    }
    Cast c; c.ok = true; c.pos = hit; c.terrain = t;
    const Foliage::Surface ground = Foliage::TerrainSurface(*t);
    const bool erase = io.KeyShift;
    DrawRing(dl, v, c, s_layerRadius, ground, erase ? IM_COL32(255, 90, 90, 230) : IM_COL32(110, 200, 255, 230));
    if (!s_layerStroke.active) {
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || io.KeyAlt) return;
        ed.PushUndo("Paint Terrain Layer");
        s_layerStroke.active = true;
        s_layerStroke.lastPublish = 0.0;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { t->PublishSplat(); s_layerStroke.active = false; return; }
    g_uiHandled = true;
    const Vec3 lp = t->transform->LocalToWorldMatrix().Inverse().MultiplyPoint(hit);
    const float amount = std::min(1.0f, s_layerStrength * io.DeltaTime * 3.0f) * (erase ? -1.0f : 1.0f);
    t->PaintLayerAt(s_layer, lp.x, lp.z, s_layerRadius, amount, s_layerHardness);
    const double now = ImGui::GetTime();
    if (now - s_layerStroke.lastPublish > 0.08) { t->PublishSplat(); s_layerStroke.lastPublish = now; }
    ed.dirty = true;
}

void MeasureTool(EditorState& ed, ImDrawList* dl, const View& v, const Mat4& vp, ImVec2 center, ImVec2 size, bool clickable, ImGuiIO& io) {
    Vec3 o, d;
    const bool haveRay = MouseRay(vp, center, size, io, o, d);
    const bool inView = io.MousePos.x >= center.x - size.x * 0.5f && io.MousePos.x <= center.x + size.x * 0.5f &&
                        io.MousePos.y >= center.y - size.y * 0.5f && io.MousePos.y <= center.y + size.y * 0.5f;
    Vec3 cursor; bool haveCursor = false;
    if (haveRay && inView) {
        const Cast c = CastScene(ed, o, d);
        if (c.ok) { cursor = c.pos; haveCursor = true; }
        else if (std::fabs(d.y) > 1e-5f && -o.y / d.y > 0.0f) { cursor = o + d * (-o.y / d.y); haveCursor = true; }   // the ground plane
    }
    if (clickable && haveCursor && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
        if (s_mn >= 2) s_mn = 0;
        s_mp[s_mn++] = cursor;
        g_uiHandled = true;
    }
    auto label = [&](const Vec3& a, const Vec3& b, bool live) {
        const Vec3 dv = b - a;
        const float len = dv.Magnitude(), horiz = std::sqrt(dv.x * dv.x + dv.z * dv.z);
        char text[160];
        std::snprintf(text, sizeof(text), "%.2f m   (across %.2f, height %+.2f)", len, horiz, dv.y);
        ImVec2 s;
        if (!v.ToScreen((a + b) * 0.5f, s)) return;
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 p0(s.x - ts.x * 0.5f - 6, s.y - ts.y - 12), p1(s.x + ts.x * 0.5f + 6, s.y - 6);
        dl->AddRectFilled(p0, p1, IM_COL32(20, 22, 28, live ? 190 : 235), 4.0f);
        dl->AddText(ImVec2(p0.x + 6, p0.y + 2), live ? IM_COL32(200, 205, 215, 255) : IM_COL32(255, 235, 120, 255), text);
    };
    const ImU32 col = IM_COL32(255, 220, 80, 255);
    for (int i = 0; i < s_mn; ++i) { ImVec2 s; if (v.ToScreen(s_mp[i], s)) { dl->AddCircleFilled(s, 5.0f, col); dl->AddCircle(s, 7.0f, IM_COL32(20, 20, 20, 255), 16, 1.5f); } }
    if (s_mn == 2) { v.Line(dl, s_mp[0], s_mp[1], col, 2.5f); label(s_mp[0], s_mp[1], false); }
    else if (s_mn == 1 && haveCursor) { v.Line(dl, s_mp[0], cursor, IM_COL32(255, 220, 80, 150), 1.5f); label(s_mp[0], cursor, true); }
    if (haveCursor) { ImVec2 s; if (v.ToScreen(cursor, s)) dl->AddCircle(s, 4.0f, IM_COL32(255, 255, 255, 200), 12, 1.5f); }
    // A hint in the corner.
    const char* hint = s_mn == 0 ? "Measure: click the first point (Esc to leave)" : s_mn == 1 ? "Measure: click the second point" : "Measure: click to start a new one (Esc to leave)";
    dl->AddText(ImVec2(center.x - size.x * 0.5f + 12, center.y + size.y * 0.5f - 24), IM_COL32(255, 235, 140, 255), hint);
}

void SetView(EditorState& ed, float yaw, float pitch, const char* name) {
    ed.view3D = true;
    ed.camYaw = yaw; ed.camPitch = pitch;
    s_ortho = true; s_viewName = name;
}

} // namespace

// ---- Scene view ----------------------------------------------------------------------------------------------

Mat4 LevelEditorProjection(float fovDeg, float aspect, float nearZ, float farZ, float camDist) {
    if (!s_ortho) return Mat4::Perspective(fovDeg, aspect, nearZ, farZ);
    const float hh = std::max(0.05f, camDist * std::tan(fovDeg * 0.5f * 0.0174532925f));
    return Mat4::Ortho(-hh * aspect, hh * aspect, -hh, hh, -1000.0f, 3000.0f);
}

bool LevelBrushActive(EditorState& ed) {
    if (s_measure) return true;
    if (s_foliage && SelectedWith(ed, true)) return true;
    if (s_layerPaint) if (GameObject* g = SelectedWith(ed, false)) if (!g->GetComponent<Terrain>()->layers.empty()) return true;
    return false;
}

void DrawLevelOverlay(EditorState& ed, ImDrawList* dl, const Mat4& vp, ImVec2 center, ImVec2 canvasSize, bool clickable, ImGuiIO& io) {
    if (!ed.view3D) return;
    const View v{&vp, center, canvasSize};
    // Only one brush at a time, and none while the sculpt brush is on.
    if (TerrainSculptRef() && (s_foliage || s_layerPaint)) { s_foliage = false; s_layerPaint = false; }
    if (s_measure) { s_foliage = false; s_layerPaint = false; TerrainSculptRef() = false; }
    if (s_measure) MeasureTool(ed, dl, v, vp, center, canvasSize, clickable, io);
    else if (s_foliage) FoliageBrush(ed, dl, v, vp, center, canvasSize, clickable, io);
    else if (s_layerPaint) LayerBrush(ed, dl, v, vp, center, canvasSize, clickable, io);
    else { s_foliageStroke.active = false; s_layerStroke.active = false; }

    // The selected decal's box and projection direction.
    if (GameObject* sel = ed.selected()) {
        if (auto* dc = sel->GetComponent<Decal>()) {
            const Mat4 M = sel->transform->LocalToWorldMatrix();
            const Vec3 h{dc->size.x * 0.5f, dc->size.y * 0.5f, 0.0f};
            Vec3 c[8];
            for (int i = 0; i < 8; ++i) c[i] = M.MultiplyPoint({(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? dc->size.z : 0.0f});
            const ImU32 col = IM_COL32(255, 170, 60, 220);
            for (int i = 0; i < 8; ++i)
                for (int b = 0; b < 3; ++b) if (!(i & (1 << b))) v.Line(dl, c[i], c[i | (1 << b)], col, 1.5f);
            v.Line(dl, M.MultiplyPoint({0, 0, 0}), M.MultiplyPoint({0, 0, dc->size.z}), col, 2.5f);
        }
    }
    if (s_ortho) {
        const char* nm = s_viewName && s_viewName[0] ? s_viewName : "Orthographic";
        char t[64]; std::snprintf(t, sizeof(t), "%s  Orthographic", nm);
        const ImVec2 ts = ImGui::CalcTextSize(t);
        const ImVec2 p(center.x - ts.x * 0.5f, center.y - canvasSize.y * 0.5f + 12);   // top centre: the stats box owns the left corner
        dl->AddRectFilled(ImVec2(p.x - 6, p.y - 3), ImVec2(p.x + ts.x + 6, p.y + ts.y + 3), IM_COL32(20, 22, 28, 190), 4.0f);
        dl->AddText(p, IM_COL32(190, 215, 255, 255), t);
    }
}

void DrawLevelViewMenu(EditorState& ed) {
    ImGui::Separator();
    ImGui::TextDisabled("Projection");
    if (ImGui::MenuItem("Perspective", "Numpad 5", !s_ortho)) { s_ortho = false; s_viewName = ""; }
    Tip("The normal 3D view: things shrink with distance.");
    if (ImGui::MenuItem("Orthographic", nullptr, s_ortho)) { s_ortho = !s_ortho; if (!s_ortho) s_viewName = ""; else s_viewName = ""; }
    Tip("No perspective shrink: parallel lines stay parallel. Zoom with the wheel.");
    if (ImGui::BeginMenu("Orthographic view")) {
        if (ImGui::MenuItem("Top", "Numpad 7")) SetView(ed, 0.0f, 89.0f, "Top");
        if (ImGui::MenuItem("Bottom", "Ctrl+Numpad 7")) SetView(ed, 0.0f, -89.0f, "Bottom");
        if (ImGui::MenuItem("Front", "Numpad 1")) SetView(ed, 0.0f, 0.0f, "Front");
        if (ImGui::MenuItem("Back", "Ctrl+Numpad 1")) SetView(ed, 180.0f, 0.0f, "Back");
        if (ImGui::MenuItem("Right", "Numpad 3")) SetView(ed, 90.0f, 0.0f, "Right");
        if (ImGui::MenuItem("Left", "Ctrl+Numpad 3")) SetView(ed, -90.0f, 0.0f, "Left");
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Measure tool", "M", s_measure)) s_measure = !s_measure;
    Tip("Click two points on the surfaces in the 3D view to read the distance between them.");
    if (ImGui::MenuItem("Clear measurement", nullptr, false, s_mn > 0)) s_mn = 0;
}

void HandleLevelKeys(EditorState& ed, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    if (!hovered || io.WantTextInput || !ed.view3D) return;
    const bool ctrl = io.KeyCtrl;
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) SetView(ed, 0.0f, ctrl ? -89.0f : 89.0f, ctrl ? "Bottom" : "Top");
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) SetView(ed, ctrl ? 180.0f : 0.0f, 0.0f, ctrl ? "Back" : "Front");
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) SetView(ed, ctrl ? -90.0f : 90.0f, 0.0f, ctrl ? "Left" : "Right");
    if (ImGui::IsKeyPressed(ImGuiKey_Keypad5, false)) { s_ortho = !s_ortho; s_viewName = ""; }
    if (!ctrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_M, false)) s_measure = !s_measure;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (s_measure) s_measure = false;
        else if (s_foliage) s_foliage = false;
        else if (s_layerPaint) s_layerPaint = false;
    }
}

// ---- Inspectors -----------------------------------------------------------------------------------------------

static void FoliageInspector(EditorState& ed, Foliage* f) {
    bool shape = false;
    // What is planted: a built-in prop, a mesh file or a prefab (the last two win in that order).
    int prop = 0;
    for (int i = 0; i < kPropCount; ++i) if (f->mesh == kProps[i]) prop = i;
    ImGui::SeparatorText("What to plant");
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("Prop##fol", &prop, kProps, kPropCount)) { f->mesh = kProps[prop]; shape = true; }
    Tip("A built-in plant or rock. Used when no mesh file or prefab is set below.");
    if (PathField("Mesh File##fol", f->meshPath, "model")) shape = true;
    Tip("A .obj or .okaymesh file to plant instead of the prop.");
    if (PathField("Prefab##fol", f->prefabPath, "prefab")) shape = true;
    Tip("A prefab to plant (its meshes are merged into one shape). Wins over the prop and the mesh file.");
    if (Mesh::NameHasVariants(f->mesh) && f->meshPath.empty() && f->prefabPath.empty()) {
        if (ImGui::SliderInt("Shapes##fol", &f->variants, 1, 8)) shape = true;
        Tip("How many different shapes of the prop are mixed (each copy picks one).");
    }
    float tint[4] = {f->tint.r, f->tint.g, f->tint.b, f->tint.a};
    if (ImGui::ColorEdit4("Tint##fol", tint)) { f->tint = {tint[0], tint[1], tint[2], tint[3]}; shape = true; }
    if (!f->Problem().empty()) ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "%s", f->Problem().c_str());

    ImGui::SeparatorText("Brush");
    if (ImGui::Checkbox("Paint in the 3D view##fol", &s_foliage)) {
        if (s_foliage) { s_layerPaint = false; s_measure = false; TerrainSculptRef() = false; ed.view3D = true; }
    }
    Tip("Drag on a terrain or any surface in the Scene view. Shift while painting erases. Esc leaves the brush.");
    const char* tools[] = {"Paint", "Erase", "Rescale"};
    ImGui::SetNextItemWidth(170);
    ImGui::Combo("Tool##fol", &s_tool, tools, 3);
    Tip("Paint plants copies. Erase removes the copies under the brush. Rescale gives them new random sizes.");
    ImGui::SliderFloat("Radius##fol", &f->radius, 0.5f, 60.0f, "%.1f");
    ImGui::SliderFloat("Density##fol", &f->density, 0.005f, 4.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    Tip("Copies per square metre. Copies are also kept apart, so painting over the same spot cannot pile them up.");
    ImGui::SliderFloat("Spacing##fol", &f->spacing, 0.0f, 2.0f, "%.2f");
    Tip("How far apart copies are kept, as a share of the average gap at this density. 0 = they may touch or overlap.");
    ImGui::DragFloatRange2("Scale##fol", &f->minScale, &f->maxScale, 0.02f, 0.05f, 12.0f, "%.2f", "%.2f");
    ImGui::SliderFloat("Max Slope##fol", &f->maxSlope, 0.0f, 89.0f, "%.0f deg");
    Tip("Ground steeper than this gets no plants.");
    ImGui::DragFloatRange2("Height Band##fol", &f->minHeight, &f->maxHeight, 0.5f, -10000.0f, 10000.0f, "%.1f", "%.1f");
    Tip("Only ground between these world heights is planted.");
    ImGui::Checkbox("Random Turn##fol", &f->randomYaw);
    ImGui::SliderFloat("Follow Ground##fol", &f->alignToSurface, 0.0f, 1.0f, "%.2f");
    Tip("0 = every copy stands straight up; 1 = it leans fully with the slope it stands on.");

    ImGui::SeparatorText("Budget");
    ImGui::DragInt("Max Triangles##fol", &f->maxTriangles, 1000.0f, 1000, 20000000);
    Tip("Painting stops when the layer would draw more triangles than this. Keeps the Scene view fast.");
    if (ImGui::Checkbox("Cast Shadows##fol", &f->castShadows)) shape = true;
    Tip("Copies throw shadows. Off by default: with thousands of copies the shadow pass makes the Scene view slow.");
    if (ImGui::DragFloat("Chunk Size##fol", &f->chunkSize, 0.5f, 4.0f, 256.0f, "%.0f")) shape = true;
    Tip("Plants are merged one mesh per square of this size; whole squares out of view are skipped.");
    ImGui::TextDisabled("%d copies in %d chunks, %d triangles (room for %d)", f->Count(), f->ChunkCount(), f->TotalTriangles(), f->Capacity());
    if (ImGui::Button("Clear All##fol") && f->Count() > 0) { f->Clear(); f->EnsureBuilt(); ed.dirty = true; }
    Tip("Remove every copy in this layer.");
    ImGui::SameLine();
    if (ImGui::Button("Rebuild##fol")) { f->RebuildAll(); }
    Tip("Rebuild the merged meshes from the list of copies.");
    if (shape) { f->MarkAllDirty(); f->RebuildAll(); ed.dirty = true; }
}

static void DecalInspector(EditorState& ed, Decal* d) {
    bool redo = false;
    if (PathField("Texture##decal", d->texture, "image")) redo = true;
    float tint[4] = {d->tint.r, d->tint.g, d->tint.b, d->tint.a};
    if (ImGui::ColorEdit4("Tint##decal", tint)) { d->tint = {tint[0], tint[1], tint[2], tint[3]}; redo = true; }
    Tip("Multiplies the picture. Lower alpha fades the decal.");
    float sz[3] = {d->size.x, d->size.y, d->size.z};
    if (ImGui::DragFloat3("Size##decal", sz, 0.05f, 0.05f, 100.0f, "%.2f")) { d->size = {sz[0], sz[1], sz[2]}; redo = true; }
    Tip("Width, height and how far the projection reaches along the object's blue (forward) axis.");
    if (ImGui::SliderFloat("Max Angle##decal", &d->maxAngle, 1.0f, 89.0f, "%.0f deg")) redo = true;
    Tip("Surfaces turned further than this away from the projector get no decal (stops smearing on side walls).");
    if (ImGui::DragFloat("Lift##decal", &d->lift, 0.001f, 0.0f, 0.5f, "%.3f")) redo = true;
    Tip("How far the decal floats above the surface.");
    if (ImGui::Button("Project##decal")) redo = true;
    Tip("Cut the decal from the surfaces in the box now. Do this after moving the surfaces.");
    ImGui::SameLine();
    ImGui::TextDisabled("%d triangles", d->Triangles());
    if (d->Projected() && d->Triangles() == 0) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "No surface in the box: move the decal close to a wall or floor, pointing at it.");
    if (redo) { d->Project(); ed.dirty = true; }
}

static void CableInspector(EditorState& ed, Cable* c) {
    bool changed = false;
    changed |= ObjectCombo<Transform>(ed, "From##cable", c->startObject, "(this object)");
    changed |= ObjectCombo<Transform>(ed, "To##cable", c->endObject, "(a point below)");
    float so[3] = {c->startOffset.x, c->startOffset.y, c->startOffset.z}, eo[3] = {c->endOffset.x, c->endOffset.y, c->endOffset.z};
    if (ImGui::DragFloat3("From Offset##cable", so, 0.05f)) { c->startOffset = {so[0], so[1], so[2]}; changed = true; }
    if (ImGui::DragFloat3(c->endObject.empty() ? "To Offset (from start)##cable" : "To Offset##cable", eo, 0.05f)) { c->endOffset = {eo[0], eo[1], eo[2]}; changed = true; }
    changed |= ImGui::DragFloat("Sag##cable", &c->sag, 0.02f, 0.0f, 100.0f, "%.2f");
    Tip("How far the middle hangs below the straight line between the two ends.");
    changed |= ImGui::SliderInt("Segments##cable", &c->segments, 2, 200);
    changed |= ImGui::DragFloat("Width##cable", &c->width, 0.005f, 0.005f, 5.0f, "%.3f");
    float col[4] = {c->color.r, c->color.g, c->color.b, c->color.a};
    if (ImGui::ColorEdit4("Color##cable", col)) { c->color = {col[0], col[1], col[2], col[3]}; changed = true; }
    Vec3 a, b;
    if (!c->Ends(a, b)) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "An end object was not found in the scene.");
    else ImGui::TextDisabled("Length %.2f m", c->Length());
    if (changed) { c->Rebuild(); ed.dirty = true; }
}

void DrawLevelInspector(EditorState& ed, GameObject*, Component* comp, Component*& toRemove,
                        bool (*header)(const char*, Component*, Component**, bool)) {
    if (auto* f = dynamic_cast<Foliage*>(comp)) {
        if (header("Foliage", f, &toRemove, true)) FoliageInspector(ed, f);
    } else if (auto* d = dynamic_cast<Decal*>(comp)) {
        if (header("Decal Projector", d, &toRemove, true)) DecalInspector(ed, d);
    } else if (auto* c = dynamic_cast<Cable*>(comp)) {
        if (header("Cable", c, &toRemove, true)) CableInspector(ed, c);
    }
}

// ---- Terrain Inspector parts --------------------------------------------------------------------------------------

void DrawTerrainLayersUI(EditorState& ed, Terrain& t) {
    ImGui::SeparatorText("Texture layers (paint in the 3D view)");
    if (t.layers.empty()) {
        ImGui::TextDisabled("One ground texture today. Add layers to blend several (grass, dirt, rock...) with a brush.");
        if (ImGui::Button("Add Layers##tl")) {
            TerrainLayer a; a.name = "Grass"; a.texture = t.texture; a.tint = t.texture.empty() ? t.grassColor : Color(1, 1, 1, 1); a.tiling = t.textureTiling;
            TerrainLayer b; b.name = "Dirt"; b.tint = Color::FromBytes(130, 98, 66);
            t.AddLayer(a); t.AddLayer(b);
            s_layer = 1;
            t.Apply(); ed.dirty = true;
        }
        Tip("Starts two layers: the first covers the whole terrain, the second is ready to paint.");
        return;
    }
    s_layer = std::min(std::max(s_layer, 0), (int)t.layers.size() - 1);
    int removeAt = -1;
    for (int i = 0; i < (int)t.layers.size(); ++i) {
        TerrainLayer& L = t.layers[(std::size_t)i];
        ImGui::PushID(i);
        if (ImGui::RadioButton("##paintlayer", s_layer == i)) s_layer = i;
        Tip("Paint with this layer.");
        ImGui::SameLine();
        char nm[64]; std::strncpy(nm, L.name.c_str(), sizeof(nm) - 1); nm[sizeof(nm) - 1] = '\0';
        ImGui::SetNextItemWidth(110);
        if (ImGui::InputText("##lname", nm, sizeof(nm))) { L.name = nm; ed.dirty = true; }
        ImGui::SameLine();
        float c4[4] = {L.tint.r, L.tint.g, L.tint.b, 1.0f};
        if (ImGui::ColorEdit3("##ltint", c4, ImGuiColorEditFlags_NoInputs)) { L.tint = {c4[0], c4[1], c4[2], 1.0f}; t.MarkSplatDirty(); t.Apply(); ed.dirty = true; }
        Tip("Tint. With a picture it multiplies it; without one this is the layer's colour.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragFloat("##ltile", &L.tiling, 0.1f, 0.1f, 400.0f, "x%.1f")) { t.MarkSplatDirty(); t.Apply(); ed.dirty = true; }
        Tip("How many times the picture repeats across the whole terrain.");
        if (t.layers.size() > 1) { ImGui::SameLine(); if (ImGui::SmallButton("Remove")) removeAt = i; }
        if (PathField("Texture##ltex", L.texture, "image")) { t.MarkSplatDirty(); t.Apply(); ed.dirty = true; }
        ImGui::PopID();
    }
    if (removeAt >= 0) { t.RemoveLayer(removeAt); t.Apply(); ed.dirty = true; if (s_layer >= (int)t.layers.size()) s_layer = (int)t.layers.size() - 1; }
    if ((int)t.layers.size() < Terrain::kMaxLayers) {
        if (ImGui::Button("Add Layer##tl")) {
            TerrainLayer n; n.name = "Layer " + std::to_string(t.layers.size() + 1); n.tint = Color::FromBytes(150, 150, 150);
            t.AddLayer(n); s_layer = (int)t.layers.size() - 1; t.Apply(); ed.dirty = true;
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Fill With Selected##tl")) { t.FillLayer(s_layer); t.Apply(); ed.dirty = true; }
    Tip("Give the selected layer full weight everywhere.");
    if (ImGui::Button("Remove All Layers##tl")) { while (!t.layers.empty()) t.RemoveLayer(0); t.Apply(); ed.dirty = true; return; }
    Tip("Back to the single Ground Texture and the auto-colours.");
    int res = t.splatResolution;
    const int resv[] = {256, 512, 1024, 2048};
    int ri = 1;
    for (int i = 0; i < 4; ++i) if (resv[i] == res) ri = i;
    const char* rn[] = {"256", "512", "1024", "2048"};
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("Detail (px)##tl", &ri, rn, 4)) { t.splatResolution = resv[ri]; t.MarkSplatDirty(); t.Apply(); ed.dirty = true; }
    Tip("Size of the ground picture the layers are blended into. Bigger is sharper and uses more memory.");
    if (ImGui::Checkbox("Paint layers in the 3D view##tl", &s_layerPaint)) {
        if (s_layerPaint) { s_foliage = false; s_measure = false; TerrainSculptRef() = false; ed.view3D = true; }
    }
    Tip("Drag on the terrain in the Scene view to paint the selected layer. Shift erases. Esc leaves the brush.");
    ImGui::SliderFloat("Radius##tl", &s_layerRadius, 0.5f, 60.0f, "%.1f");
    ImGui::SliderFloat("Strength##tl", &s_layerStrength, 0.05f, 4.0f, "%.2f");
    ImGui::SliderFloat("Hardness##tl", &s_layerHardness, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("The layers replace the Ground Texture and the auto-colours above while any exist.");
}

void DrawTerrainScatterUI(EditorState& ed, Terrain& t) {
    ImGui::SeparatorText("Scatter props (trees, rocks, bushes)");
    ImGui::SetNextItemWidth(170);
    ImGui::Combo("Prop##scat", &s_scatterProp, kProps, kPropCount);
    ImGui::SliderInt("Count##scat", &s_scatterCount, 1, 5000);
    ImGui::DragFloatRange2("Scale##scat", &s_scatterMinScale, &s_scatterMaxScale, 0.02f, 0.1f, 6.0f, "%.2f", "%.2f");
    ImGui::SliderFloat("Max Slope##scat", &s_scatterSlope, 0.0f, 89.0f, "%.0f deg");
    ImGui::DragFloatRange2("Height Band##scat", &s_scatterMinH, &s_scatterMaxH, 0.2f, -100.0f, 100.0f, "%.1f", "%.1f");
    // The Foliage layer of this prop under the terrain, made when missing.
    auto layerFor = [&](bool create) -> Foliage* {
        const std::string nm = std::string("Scatter ") + kProps[s_scatterProp];
        for (Transform* ch : t.transform->Children())
            if (ch && ch->gameObject && ch->gameObject->name == nm) if (auto* f = ch->gameObject->GetComponent<Foliage>()) return f;
        if (!create) return nullptr;
        GameObject* g = ed.scene().CreateGameObject(nm);
        g->transform->SetParent(t.transform, false);
        auto* f = g->AddComponent<Foliage>();
        f->mesh = kProps[s_scatterProp];
        return f;
    };
    if (ImGui::Button("Scatter##scat")) {
        ed.PushUndo("Scatter Props");
        Foliage* f = layerFor(true);
        f->minScale = s_scatterMinScale; f->maxScale = s_scatterMaxScale; f->maxSlope = s_scatterSlope;
        f->minHeight = s_scatterMinH; f->maxHeight = s_scatterMaxH;
        const int n = f->ScatterOnTerrain(t, s_scatterCount, (unsigned)(ImGui::GetTime() * 1000.0) + 7u);
        f->EnsureBuilt();
        ConsoleLog("Scattered " + std::to_string(n) + " " + kProps[s_scatterProp] + "(s) into the layer '" + f->gameObject->name + "' (paint more with its brush)");
        ed.dirty = true;
    }
    Tip("Plants random copies over the whole terrain into a Foliage layer (one object per prop, not one per copy).\nSelect that layer to paint, erase and rescale with the brush.");
    ImGui::SameLine();
    if (ImGui::Button("Clear Scattered##scat")) {
        std::vector<GameObject*> kill;
        for (const auto& o : ed.scene().Objects()) {
            if (o->transient) continue;
            if (o->name.rfind("Scatter_", 0) == 0) kill.push_back(o.get());                        // older scenes: one object per copy
            else if (o->name.rfind("Scatter ", 0) == 0 && o->GetComponent<Foliage>()) kill.push_back(o.get());
        }
        if (!kill.empty()) ed.PushUndo("Clear Scattered");
        for (GameObject* k : kill) { ed.Deselect(k); ed.scene().Destroy(k); }
        ConsoleLog("Cleared " + std::to_string(kill.size()) + " scattered layer(s) / prop(s)");
        ed.dirty = true;
    }
    Tip("Remove the scattered Foliage layers (and props scattered by older versions).");
    if (ImGui::Button("Add Foliage Layer##scat")) {
        ed.PushUndo("Add Foliage Layer");
        GameObject* g = ed.scene().CreateGameObject("Foliage");
        g->transform->SetParent(t.transform, false);
        g->AddComponent<Foliage>();
        ed.Select(g);
        s_foliage = true; s_layerPaint = false; TerrainSculptRef() = false;
        ed.dirty = true;
    }
    Tip("A new Foliage layer under this terrain, selected with the brush on.");
}

// ---- Add Component and GameObject menus ---------------------------------------------------------------------------------

void LevelAddComponentItems(EditorState& ed, GameObject* go, const std::function<bool(bool, const char*)>& item) {
    if (item(!go->GetComponent<Foliage>(), "Foliage (paint plants)")) { ed.PushUndo(); go->AddComponent<Foliage>(); ed.view3D = true; ed.dirty = true; }
    if (item(!go->GetComponent<Decal>(), "Decal Projector")) { ed.PushUndo(); go->AddComponent<Decal>()->Project(); ed.view3D = true; ed.dirty = true; }
    if (item(!go->GetComponent<Cable>(), "Cable (rope with sag)")) { ed.PushUndo(); auto* c = go->AddComponent<Cable>(); c->endOffset = {4, 0, 0}; c->Rebuild(); ed.view3D = true; ed.dirty = true; }
}

void LevelCreateMenuItems(EditorState& ed, bool& created) {
    if (ImGui::MenuItem("Foliage Layer")) {
        GameObject* g = ed.CreateEmpty("Foliage");
        GameObject* sel = ed.selected();
        (void)sel;
        g->AddComponent<Foliage>();
        // Under the first terrain, so it moves with it and plants land in its space.
        for (const auto& o : ed.scene().Objects()) if (o.get() != g && !o->transient && o->GetComponent<Terrain>()) { g->transform->SetParent(o->transform, false); g->transform->localPosition = {0, 0, 0}; break; }
        s_foliage = true; s_layerPaint = false; s_measure = false; TerrainSculptRef() = false;
        ed.view3D = true;
        ConsoleLog("Created a Foliage layer: drag in the Scene view to paint plants (Shift erases)");
        created = true;
    }
    Tip("Paint trees, bushes and rocks onto a terrain or any surface with a brush.");
    if (ImGui::MenuItem("Decal Projector")) {
        GameObject* g = ed.CreateEmpty("Decal");
        auto* d = g->AddComponent<Decal>();
        d->size = {2.0f, 2.0f, 2.0f};
        // On the surface under the view's focus when there is one, aimed into it; else above the focus, aimed down.
        SurfaceHit hit;
        if (RaycastSurfaces(ed.scene(), ed.camTarget + Vec3{0, 300.0f, 0}, {0, -1, 0}, 2000.0f, hit,
                            [g](const GameObject& o) { return &o == g; })) {
            const Vec3 up = std::fabs(hit.normal.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            g->transform->localPosition = hit.position + hit.normal * 0.5f;
            g->transform->localRotation = Quat::LookRotation(hit.normal * -1.0f, up);
        } else {
            g->transform->localRotation = Quat::LookRotation({0, -1, 0}, {0, 0, 1});
            g->transform->localPosition = ed.camTarget + Vec3{0, 1.0f, 0};
        }
        d->Project();
        ed.view3D = true;
        ConsoleLog("Created a Decal Projector (" + std::to_string(d->Triangles()) + " triangles): set a Texture; aim its blue axis at a surface and press Project");
        created = true;
    }
    Tip("Projects a picture (stain, crack, sign) onto the surfaces in its box.");
    if (ImGui::MenuItem("Cable")) {
        GameObject* g = ed.CreateEmpty("Cable");
        auto* c = g->AddComponent<Cable>();
        SurfaceHit hit;
        float y = ed.camTarget.y + 2.0f;
        if (RaycastSurfaces(ed.scene(), ed.camTarget + Vec3{0, 300.0f, 0}, {0, -1, 0}, 2000.0f, hit,
                            [g](const GameObject& o) { return &o == g; })) y = hit.position.y + 3.0f;   // above the ground there
        g->transform->localPosition = {ed.camTarget.x - 3.0f, y, ed.camTarget.z};
        c->endOffset = {6, 0, 0};
        c->Rebuild();
        ed.view3D = true;
        ConsoleLog("Created a Cable: set its From and To objects in the Inspector");
        created = true;
    }
    Tip("A rope or wire hanging between two points. Pick the two objects it joins; Sag sets how much it droops.");
}

// ---- Edit-mode upkeep ----------------------------------------------------------------------------------------------------

void LevelEditTick(EditorState& ed, float) {
    static std::unordered_map<const Decal*, std::size_t> s_sig;
    for (const auto& up : ed.scene().Objects()) {
        GameObject* g = up.get();
        if (!g || !g->active) continue;
        if (auto* f = g->GetComponent<Foliage>()) f->EnsureBuilt();
        if (auto* c = g->GetComponent<Cable>()) c->Rebuild();
        if (auto* d = g->GetComponent<Decal>()) {
            // Projected when new (loaded from a file) and again when the decal itself is moved or changed.
            std::size_t h = std::hash<std::string>{}(d->texture);
            auto mix = [&](float x) { std::uint32_t u; std::memcpy(&u, &x, 4); h ^= u + 0x9e3779b9u + (h << 6) + (h >> 2); };
            const Mat4 M = g->transform->LocalToWorldMatrix();
            for (int i = 0; i < 16; ++i) mix(M.m[i]);
            mix(d->size.x); mix(d->size.y); mix(d->size.z); mix(d->maxAngle); mix(d->lift);
            mix(d->tint.r); mix(d->tint.g); mix(d->tint.b); mix(d->tint.a);
            auto it = s_sig.find(d);
            if (!d->Projected() || it == s_sig.end() || it->second != h) { d->Project(); s_sig[d] = h; }
        }
    }
}

// ---- Dev hooks (screenshots) ----------------------------------------------------------------------------------------------

void LevelDevHooks(EditorState& ed) {
    static int s_frame = 0;
    if (s_frame < 100000) ++s_frame;
    if (std::getenv("OKAY_UI_LEVEL") && s_frame == 33 && !ed.isPlaying()) {
        Scene& sc = ed.scene();
        GameObject* tg = sc.CreateGameObject("Terrain");
        auto* t = tg->AddComponent<Terrain>();
        t->Resize(64); t->size = 60.0f;
        t->Hills(6, 4.0f, 3u);
        t->Apply();
        GameObject* fg = sc.CreateGameObject("Pines");
        fg->transform->SetParent(tg->transform, false);
        auto* f = fg->AddComponent<Foliage>();
        f->mesh = "Pine"; f->radius = 8.0f; f->density = 0.12f; f->maxSlope = 35.0f;
        ed.view3D = true;
        ed.camTarget = {0, 1, 0}; ed.camYaw = 25.0f; ed.camPitch = 30.0f; ed.camDist = 32.0f;
        ed.Select(fg);
        if (const char* m = std::getenv("OKAY_UI_LEVEL"); m && std::strcmp(m, "preplanted") == 0) {
            f->Paint(-10, -6, Foliage::TerrainSurface(*t));
            f->Paint(8, 8, Foliage::TerrainSurface(*t));
        }
        if (const char* m = std::getenv("OKAY_UI_LEVEL"); m && std::strcmp(m, "forest") == 0) {
            // A dense forest to look at the cost: stamps over the whole terrain.
            f->radius = 14.0f; f->density = 0.8f; f->maxTriangles = 4000000;
            if (const char* dn = std::getenv("OKAY_UI_LEVELDENS")) f->density = (float)std::atof(dn);
            if (std::getenv("OKAY_UI_LEVELSHADOW")) f->castShadows = true;
            const auto ground = Foliage::TerrainSurface(*t);
            for (int gx = -2; gx <= 2; ++gx) for (int gz = -2; gz <= 2; ++gz) f->Paint(gx * 11.0f, gz * 11.0f, ground);
            f->EnsureBuilt();
            ConsoleLog("Forest: " + std::to_string(f->Count()) + " copies, " + std::to_string(f->TotalTriangles()) + " triangles in " + std::to_string(f->ChunkCount()) + " chunks");
        }
        ConsoleLog("Level tools demo: a hilly terrain with an empty Foliage layer");
    }
    if (const char* c = std::getenv("OKAY_UI_LEVELCAM"); c && s_frame >= 34 && s_frame <= 44) {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, dist = 10;
        if (std::sscanf(c, "%f,%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch, &dist) == 6) {
            ed.camTarget = {x, y, z}; ed.camYaw = yaw; ed.camPitch = pitch; ed.camDist = dist;
        }
    }
    if (const char* tool = std::getenv("OKAY_UI_LEVELTOOL"); tool && s_frame == 45) {
        if (std::strcmp(tool, "foliage") == 0) { s_foliage = true; }
        else if (std::strcmp(tool, "measure") == 0) { s_measure = true; }
        else if (std::strcmp(tool, "layers") == 0) {
            if (GameObject* g = ed.scene().Find("Terrain")) {
                auto* t = g->GetComponent<Terrain>();
                if (t && t->layers.empty()) {
                    TerrainLayer a; a.name = "Grass"; a.tint = Color::FromBytes(88, 150, 70);
                    TerrainLayer b; b.name = "Dirt"; b.tint = Color::FromBytes(140, 100, 62);
                    TerrainLayer c; c.name = "Rock"; c.tint = Color::FromBytes(130, 130, 136);
                    t->AddLayer(a); t->AddLayer(b); t->AddLayer(c);
                    t->Apply();
                }
                ed.Select(g); s_layerPaint = true; s_layer = 1;
            }
        }
    }
    if (const char* view = std::getenv("OKAY_UI_LEVELVIEW"); view && s_frame == 46) {
        if (std::strcmp(view, "top") == 0) SetView(ed, 0.0f, 89.0f, "Top");
        else if (std::strcmp(view, "front") == 0) SetView(ed, 0.0f, 0.0f, "Front");
        else if (std::strcmp(view, "right") == 0) SetView(ed, 90.0f, 0.0f, "Right");
    }
    if (const char* sel = std::getenv("OKAY_UI_LEVELSEL"); sel && s_frame == 47) {
        if (GameObject* g = ed.scene().Find(sel)) ed.Select(g);
    }
}

} // namespace oked
