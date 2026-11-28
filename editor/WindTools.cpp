// Wind in the editor. See WindTools.hpp and docs/gameplay/wind.md.
#include "WindTools.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/WindZone.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Foliage.hpp"
#include "okay/Components/Cable.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

bool g_showWind = true;

namespace {

struct View {
    Mat4 vp; ImVec2 center, size; ImDrawList* dl = nullptr;
    bool Project(const Vec3& w, ImVec2& out) const {
        const Vec4 c = vp * Vec4{w, 1.0f};
        if (c.w <= 0.05f) return false;
        out = ImVec2(center.x + (c.x / c.w) * size.x * 0.5f, center.y - (c.y / c.w) * size.y * 0.5f);
        return true;
    }
    void Line(const Vec3& a, const Vec3& b, ImU32 col, float th) const {
        ImVec2 p, q;
        if (Project(a, p) && Project(b, q)) dl->AddLine(p, q, col, th);
    }
};

// The time the arrows use: the game's wind clock while it runs, a live clock while editing (so gusts move in the Scene view).
float PreviewTime(const EditorState& ed) { return ed.isPlaying() ? WindField::Clock() : (float)ImGui::GetTime(); }

// Blue (calm) -> white -> red (strong).
ImU32 StrengthColor(float speed, int alpha) {
    const float t = Mathf::Clamp01(speed / 14.0f);
    const float r = 0.35f + 0.65f * t, g = 0.75f - 0.25f * t, b = 1.0f - 0.7f * t;
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
}

void Arrow(const View& v, const Vec3& from, const Vec3& wind, int alpha, float th) {
    const float speed = wind.Magnitude();
    if (speed < 0.05f) return;
    const float len = std::min(2.5f, 0.12f * speed + 0.15f);
    const Vec3 dir = wind / speed;
    const Vec3 to = from + dir * len;
    const ImU32 col = StrengthColor(speed, alpha);
    v.Line(from, to, col, th);
    // arrow head: two short strokes turned from the shaft, in the screen-ish plane spanned by dir and world up (or right)
    Vec3 side = Vec3::Cross(dir, Vec3{0, 1, 0});
    if (side.SqrMagnitude() < 1e-4f) side = Vec3::Cross(dir, Vec3{1, 0, 0});
    side = side.Normalized();
    const float h = len * 0.3f;
    v.Line(to, to - dir * h + side * (h * 0.5f), col, th);
    v.Line(to, to - dir * h - side * (h * 0.5f), col, th);
}

void Ring(const View& v, const Vec3& c, const Vec3& ax, const Vec3& ay, float rx, float ry, ImU32 col) {
    const int n = 28;
    Vec3 prev = c + ax * rx;
    for (int i = 1; i <= n; ++i) {
        const float a = 6.2831853f * (float)i / (float)n;
        const Vec3 p = c + ax * (std::cos(a) * rx) + ay * (std::sin(a) * ry);
        v.Line(prev, p, col, 1.2f);
        prev = p;
    }
}

void DrawShape(const View& v, const WindSource& s, ImU32 col) {
    const Vec3 ax = s.axisX * s.half.x, ay = s.axisY * s.half.y, az = s.axisZ * s.half.z;
    if (s.shape == WindSource::Shape::Box) {
        auto corner = [&](int i) { return s.center + ax * (i & 1 ? 1.f : -1.f) + ay * (i & 2 ? 1.f : -1.f) + az * (i & 4 ? 1.f : -1.f); };
        for (int i = 0; i < 8; ++i)
            for (int b = 0; b < 3; ++b)
                if (!(i & (1 << b))) v.Line(corner(i), corner(i | (1 << b)), col, 1.3f);
    } else if (s.shape == WindSource::Shape::Sphere) {
        Ring(v, s.center, s.axisX, s.axisY, s.half.x, s.half.y, col);
        Ring(v, s.center, s.axisX, s.axisZ, s.half.x, s.half.z, col);
        Ring(v, s.center, s.axisY, s.axisZ, s.half.y, s.half.z, col);
    } else {
        for (int sgn = -1; sgn <= 1; sgn += 2) Ring(v, s.center + ay * (float)sgn, s.axisX, s.axisZ, s.half.x, s.half.z, col);
        for (int i = 0; i < 4; ++i) {
            const Vec3 d = i == 0 ? ax : i == 1 ? -ax : i == 2 ? az : -az;
            v.Line(s.center + d + ay, s.center + d - ay, col, 1.2f);
        }
    }
}

GameObject* NewObject(EditorState& ed, const char* name, const Vec3& pos) {
    GameObject* go = ed.CreateEmpty(name);
    go->transform->SetPosition(pos);
    ed.view3D = true; ed.dirty = true;
    return go;
}

Vec3 SpawnPoint(EditorState& ed) { return {ed.camTarget.x, ed.camTarget.y, ed.camTarget.z}; }

WindZone* FindGlobal(Scene& scene) {
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || go->IsDestroyed()) continue;
        for (WindZone* z : go->GetComponents<WindZone>()) if (z && z->mode == WindZone::Mode::Everywhere) return z;
    }
    return nullptr;
}

void Tip(const char* t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t); }

// Yaw in degrees (0 = blowing toward +Z, the object's forward; 90 = toward +X) of an object.
float YawOf(const Transform& t) {
    const Vec3 f = t.Forward();
    return std::atan2(f.x, f.z) * 57.29578f;
}

} // namespace

// ===========================================================================
// Scene view
// ===========================================================================

void DrawWindOverlay(EditorState& ed, ImDrawList* dl, const Mat4& vp, ImVec2 center, ImVec2 canvasSize, ImVec2) {
    View v; v.vp = vp; v.center = center; v.size = canvasSize; v.dl = dl;
    GameObject* sel = ed.selected();
    const float time = PreviewTime(ed);
    for (const auto& up : ed.scene().Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go->IsDestroyed() || !go->transform) continue;
        for (WindZone* z : go->GetComponents<WindZone>()) {
            if (!z || !z->enabled) continue;
            const bool selected = go == sel;
            if (!selected && !g_showWind) continue;
            const WindSource s = z->Snapshot();
            const int alpha = selected ? 255 : 110;
            if (!s.global) DrawShape(v, s, IM_COL32(120, 200, 255, alpha));
            // A field of arrows of the real wind (this zone together with everything else that blows there), so gusts show.
            std::vector<Vec3> spots;
            if (s.global) {
                const Vec3 c = SpawnPoint(ed);
                for (int ix = -2; ix <= 2; ++ix) for (int iz = -2; iz <= 2; ++iz) spots.push_back({c.x + ix * 3.0f, c.y + 1.5f, c.z + iz * 3.0f});
            } else {
                const int n = 3;
                for (int ix = 0; ix < n; ++ix) for (int iy = 0; iy < 2; ++iy) for (int iz = 0; iz < n; ++iz) {
                    const float u = (ix + 0.5f) / n * 2.0f - 1.0f, w = (iz + 0.5f) / n * 2.0f - 1.0f, h = (iy + 0.5f) / 2 * 2.0f - 1.0f;
                    spots.push_back(s.center + s.axisX * (s.half.x * u * 0.8f) + s.axisY * (s.half.y * h * 0.8f) + s.axisZ * (s.half.z * w * 0.8f));
                }
            }
            for (const Vec3& p : spots) Arrow(v, p, WindField::At(p, time), alpha, selected ? 2.0f : 1.2f);
            if (selected && !s.global) {   // the zone's own aim, big, from the middle
                Arrow(v, s.center, s.direction * std::max(1.0f, std::fabs(s.strength)), 255, 3.0f);
            }
            ImVec2 lp;
            if (selected && v.Project(s.center, lp)) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s: %.1f m/s", WindZone::ModeName(z->mode), WindField::At(s.center, time).Magnitude());
                dl->AddText(ImVec2(lp.x + 8, lp.y + 8), IM_COL32(190, 230, 255, 255), buf);
            }
        }
    }
}

// ===========================================================================
// Inspector
// ===========================================================================

void DrawWindInspector(EditorState& ed, GameObject* go, Component* comp, Component*& toRemove, WindCompHeaderFn header) {
    if (auto* z = dynamic_cast<WindZone*>(comp)) {
        ImGui::PushID(z);
        if (header("Wind Zone", z, &toRemove, true)) {
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", WindZone::ModeHint(z->mode));
            int mode = (int)z->mode;
            const char* modes[] = {"Everywhere (the whole scene)", "Direction (inside a region)", "Pushes outward (fan, blast)"};
            if (ImGui::Combo("Kind", &mode, modes, 3)) { ed.PushUndo(); z->mode = (WindZone::Mode)mode; ed.dirty = true; }
            Tip("Everywhere is the scene's own wind. The other two only blow inside their shape.");
            if (z->mode != WindZone::Mode::Everywhere) {
                int shape = z->shape;
                const char* shapes[] = {"Box", "Sphere", "Cylinder"};
                if (ImGui::Combo("Shape", &shape, shapes, 3)) { z->shape = shape; ed.dirty = true; }
                float sz[3] = {z->size.x, z->size.y, z->size.z};
                if (ImGui::DragFloat3("Size", sz, 0.1f, 0.05f, 100000.0f)) { z->size = {sz[0], sz[1], sz[2]}; ed.dirty = true; }
                Tip("The box the shape fills, in local units (the object's scale multiplies it).");
                float of[3] = {z->offset.x, z->offset.y, z->offset.z};
                if (ImGui::DragFloat3("Offset", of, 0.1f)) { z->offset = {of[0], of[1], of[2]}; ed.dirty = true; }
                Tip("Where the middle of the region sits, in local units.");
                if (ImGui::SliderFloat("Soft edge", &z->edge, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
                Tip("How softly the wind fades in from the edge. 0 is a hard edge.");
                if (ImGui::SliderFloat("Weaker toward the edge", &z->falloff, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
                Tip("0 = the same strength all through the region. 1 = full in the middle, nothing at the edge.");
            }
            if (z->mode != WindZone::Mode::Outward) {
                float yaw = YawOf(*z->transform);
                if (ImGui::SliderFloat("Aim (compass)", &yaw, -180.0f, 180.0f, "%.0f deg")) {
                    ed.PushUndo();
                    z->transform->SetRotation(Quat::Euler(0.0f, yaw, 0.0f));
                    ed.dirty = true;
                }
                Tip("Which way the wind blows, seen from above. 0 = forward (the blue axis), 90 = to the right.\nYou can also just turn the object with the Rotate tool: the arrows follow.");
                float d[3] = {z->direction.x, z->direction.y, z->direction.z};
                if (ImGui::DragFloat3("Direction (advanced)", d, 0.02f, -1.0f, 1.0f)) { z->direction = {d[0], d[1], d[2]}; ed.dirty = true; }
                Tip("The wind's direction in the object's own space. The default 0, 0, 1 means \"the way the object faces\".");
            }
            if (ImGui::DragFloat("Strength", &z->strength, 0.1f, -200.0f, 200.0f, "%.1f m/s")) ed.dirty = true;
            Tip(z->mode == WindZone::Mode::Outward ? "How hard it blows away from the middle. Negative sucks in."
                                                    : "Wind speed. About 3 is a breeze, 10 a strong wind, 20 a storm.");
            if (ImGui::SliderFloat("Gusts", &z->gustAmount, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
            Tip("How much the wind swells and drops. 0 = steady, 1 = from calm up to double.");
            if (z->gustAmount > 0.0f) {
                if (ImGui::DragFloat("Gust every (s)", &z->gustFrequency, 0.01f, 0.01f, 10.0f, "%.2f gusts/s")) ed.dirty = true;
                Tip("How often a gust comes. 0.2 = one every 5 seconds.");
                if (ImGui::DragFloat("Gust travel speed", &z->gustSpeed, 0.5f, 0.0f, 200.0f, "%.1f m/s")) ed.dirty = true;
                Tip("How fast a gust sweeps across the land. 0 = everything is hit at the same moment.");
            }
            if (ImGui::SliderFloat("Swirl", &z->turbulence, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
            Tip("Random swirling on top of the wind, so no two trees move together.");
            if (z->turbulence > 0.0f) {
                if (ImGui::DragFloat("Swirl size", &z->turbulenceScale, 0.005f, 0.001f, 10.0f, "%.3f")) ed.dirty = true;
                Tip("Smaller numbers make bigger swirls.");
                if (ImGui::DragInt("Seed", &z->seed, 1.0f, 0, 1000000)) ed.dirty = true;
                Tip("The same number always gives the same wind.");
            }
            if (ImGui::TreeNode("Wind over time")) {
                char buf[256]; std::snprintf(buf, sizeof(buf), "%s", z->curveKeys.c_str());
                if (ImGui::InputText("Curve", buf, sizeof(buf))) { z->curveKeys = buf; ed.dirty = true; }
                Tip("Pairs of seconds:multiplier, like  0:0.2, 10:1.5, 20:0.2  . The strength is multiplied by this curve. Empty = steady.");
                if (ImGui::DragFloat("Repeats every (s)", &z->curveLength, 0.5f, 0.1f, 100000.0f)) ed.dirty = true;
                if (ImGui::SmallButton("Calm - storm - calm")) { z->curveKeys = "0:0.2, 15:1.6, 30:0.2"; z->curveLength = 30.0f; ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) { z->curveKeys.clear(); ed.dirty = true; }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Where winds overlap")) {
                if (ImGui::DragInt("Priority", &z->priority, 0.2f, -1000, 1000)) ed.dirty = true;
                Tip("When two winds cover the same spot, the higher number is applied last and wins.");
                if (ImGui::Checkbox("Add to the wind underneath", &z->additive)) ed.dirty = true;
                Tip("Off: inside this zone only this wind blows (a calm zone shelters you). On: it adds to the other winds.");
                ImGui::TreePop();
            }
            const Vec3 here = WindField::At(z->transform->Position(), PreviewTime(ed));
            ImGui::TextDisabled("Wind at this object right now: %.1f m/s", here.Magnitude());
        }
        ImGui::PopID();
        return;
    }
    if (auto* d = dynamic_cast<WindDrag*>(comp)) {
        ImGui::PushID(d);
        if (header("Wind Drag", d, &toRemove, true)) {
            ImGui::TextDisabled("The wind pushes this object's Rigidbody 3D. Light and wide things blow about; heavy ones barely move.");
            if (!go->GetComponent<Rigidbody3D>() && !d->includeChildren) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "No Rigidbody 3D here: nothing to push.");
                if (ImGui::SmallButton("Add Rigidbody 3D")) { ed.PushUndo(); go->AddComponent<Rigidbody3D>(); ed.dirty = true; }
            }
            if (ImGui::DragFloat("Size to the wind", &d->area, 0.05f, 0.0f, 1000.0f, "%.2f m\xc2\xb2")) ed.dirty = true;
            Tip("How big the object looks from the side, in square metres. A sheet of paper is tiny, a sail is big.");
            if (ImGui::DragFloat("Drag", &d->dragCoefficient, 0.02f, 0.0f, 5.0f, "%.2f")) ed.dirty = true;
            Tip("How easily it catches the wind. 0.5 = slippery, 1 = a flat box, 2 = a parachute.");
            if (ImGui::SliderFloat("Strength", &d->strength, 0.0f, 10.0f, "%.2f")) ed.dirty = true;
            Tip("A volume knob for the whole push.");
            if (ImGui::Checkbox("Also push the bodies below", &d->includeChildren)) ed.dirty = true;
            Tip("Turn this on for a ragdoll: every bone body is pushed, sharing the size above.");
        }
        ImGui::PopID();
        return;
    }
    if (auto* w = dynamic_cast<WindSway*>(comp)) {
        ImGui::PushID(w);
        if (header("Wind Sway", w, &toRemove, true)) {
            ImGui::TextDisabled("This object's mesh leans and shivers in the wind (trees, bushes, grass, flags).");
            if (!go->GetComponent<MeshRenderer>()) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "No Mesh Renderer here: nothing to bend.");
            int wt = std::min((int)w->weight, 1);
            const char* wts[] = {"Tree / bush / grass (the top moves)", "Flag (the far edge waves)"};
            if (ImGui::Combo("Looks like", &wt, wts, 2)) { w->weight = (WindSway::Weight)wt; ed.dirty = true; }
            Tip("Tree: the bottom stays put and the top bends. Flag: the side along the pole stays, the other side waves and the flag turns downwind.");
            if (ImGui::DragFloat("How far", &w->amount, 0.01f, 0.0f, 20.0f, "%.2f m")) ed.dirty = true;
            Tip("How far the top moves in a strong wind (12 m/s). Smaller winds move it less.");
            if (ImGui::SliderFloat("Leaf shiver", &w->flutter, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
            if (w->weight == WindSway::Weight::Height) {
                if (ImGui::SliderFloat("Stiff trunk", &w->stiffness, 0.5f, 6.0f, "%.1f")) ed.dirty = true;
                Tip("1 = bends evenly all the way up. 3 = only the top bends.");
            } else {
                if (ImGui::Checkbox("Turn to face downwind", &w->turnToWind)) ed.dirty = true;
            }
            if (ImGui::TreeNode("Speed and cost")) {
                if (ImGui::DragFloat("Updates per second", &w->updateRate, 0.5f, 1.0f, 60.0f, "%.0f")) ed.dirty = true;
                Tip("Lower is lighter on the computer. 20 looks smooth.");
                if (ImGui::DragFloat("Stop when farther than", &w->maxDistance, 1.0f, 0.0f, 100000.0f, "%.0f m")) ed.dirty = true;
                Tip("Far away it stands still. 0 = always moves.");
                ImGui::TreePop();
            }
        }
        ImGui::PopID();
        return;
    }
    if (auto* a = dynamic_cast<WindAmbience*>(comp)) {
        ImGui::PushID(a);
        if (header("Wind Sound", a, &toRemove, true)) {
            ImGui::TextDisabled("The sound of the wind, made by the engine (no sound file). It gets louder as the wind gets stronger.");
            if (ImGui::SliderFloat("Loudest", &a->volume, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
            if (ImGui::DragFloat("Loudest at wind speed", &a->fullAt, 0.1f, 0.1f, 100.0f, "%.1f m/s")) ed.dirty = true;
            if (ImGui::DragFloat("Silent below", &a->quietBelow, 0.05f, 0.0f, 50.0f, "%.1f m/s")) ed.dirty = true;
            if (ImGui::DragFloat("Follows changes in", &a->smoothing, 0.05f, 0.0f, 20.0f, "%.1f s")) ed.dirty = true;
            Tip("A little time makes gusts swell instead of clicking.");
            if (ed.isPlaying()) ImGui::TextDisabled("Play: loudness now %.2f", a->Current());
        }
        ImGui::PopID();
        return;
    }
}

// ===========================================================================
// Menus
// ===========================================================================

GameObject* MakeGlobalWind(EditorState& ed) {
    GameObject* go = NewObject(ed, "Wind", {0, 0, 0});
    auto* z = go->AddComponent<WindZone>();
    z->mode = WindZone::Mode::Everywhere;
    z->strength = 4.0f; z->gustAmount = 0.35f; z->gustFrequency = 0.15f; z->turbulence = 0.2f;
    z->additive = true;
    return go;
}

void DrawWindGameObjectMenu(EditorState& ed, bool& created) {
    if (!ImGui::BeginMenu("Environment")) return;
    if (ImGui::MenuItem("Wind (whole scene)")) {
        ed.PushUndo(); ed.Select(MakeGlobalWind(ed)); ConsoleLog("Created the scene's wind: turn the object to aim it, set the Strength in the Inspector"); created = true;
    }
    Tip("The scene's wind: trees, grass, flags, particles and anything with Wind Drag lean with it. Turn the object to aim it.");
    if (ImGui::MenuItem("Wind Zone (box)")) {
        ed.PushUndo();
        GameObject* go = NewObject(ed, "Wind Zone", SpawnPoint(ed));
        auto* z = go->AddComponent<WindZone>(); z->mode = WindZone::Mode::Direction; z->strength = 8.0f; z->gustAmount = 0.3f;
        ed.Select(go); created = true;
    }
    Tip("Wind that only blows inside a box, the way the arrow points.");
    if (ImGui::MenuItem("Fan (blows outward)")) {
        ed.PushUndo();
        GameObject* go = NewObject(ed, "Fan", SpawnPoint(ed));
        auto* z = go->AddComponent<WindZone>(); z->mode = WindZone::Mode::Outward; z->shape = 1; z->size = {8, 8, 8}; z->strength = 10.0f; z->falloff = 0.7f;
        ed.Select(go); created = true;
    }
    Tip("Wind that blows away from the middle of a ball, weaker toward the edge. A negative strength sucks things in.");
    if (ImGui::MenuItem("Calm Zone (no wind)")) {
        ed.PushUndo();
        GameObject* go = NewObject(ed, "Calm Zone", SpawnPoint(ed));
        auto* z = go->AddComponent<WindZone>(); z->mode = WindZone::Mode::Direction; z->strength = 0.0f; z->priority = 10;
        ed.Select(go); created = true;
    }
    Tip("A shelter: inside it the wind is gone (a cave mouth, a house).");
    ImGui::Separator();
    if (ImGui::MenuItem("Wind Sound")) {
        ed.PushUndo();
        GameObject* go = NewObject(ed, "Wind Sound", SpawnPoint(ed));
        go->AddComponent<WindAmbience>(); ed.Select(go); created = true;
    }
    Tip("The sound of wind, made by the engine. Louder as the wind gets stronger.");
    ImGui::EndMenu();
}

void DrawWindOverlayMenu(std::function<void()> onChange) {
    if (ImGui::MenuItem("Wind zones and arrows", nullptr, &g_showWind) && onChange) onChange();
    Tip("Draw every wind zone and arrows showing the wind. The selected zone is always drawn.");
}

void WindAddComponentItems(EditorState& ed, GameObject* go, const std::function<bool(bool, const char*)>& item) {
    if (!go) return;
    if (item(true, "Wind Zone")) { ed.PushUndo(); auto* z = go->AddComponent<WindZone>(); z->strength = 6.0f; ed.view3D = true; ed.dirty = true; }
    if (item(!go->GetComponent<WindDrag>(), "Wind Drag (the wind pushes it)")) {
        ed.PushUndo(); if (!go->GetComponent<Rigidbody3D>()) go->AddComponent<Rigidbody3D>();
        go->AddComponent<WindDrag>(); ed.view3D = true; ed.dirty = true;
    }
    if (item(!go->GetComponent<WindSway>(), "Wind Sway (trees, grass, flags)")) { ed.PushUndo(); go->AddComponent<WindSway>(); ed.view3D = true; ed.dirty = true; }
    if (item(!go->GetComponent<WindAmbience>(), "Wind Sound")) { ed.PushUndo(); go->AddComponent<WindAmbience>(); ed.dirty = true; }
}

// ===========================================================================
// Lighting window: Wind
// ===========================================================================

void DrawWindSettingsSection(EditorState& ed) {
    if (!ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen)) return;
    Scene& scene = ed.scene();
    WindZone* z = FindGlobal(scene);
    ImGui::TextDisabled("Trees, grass, flags, cables, particles and Wind Drag bodies follow this wind.");
    if (!z) {
        if (ImGui::Button("Add the scene's wind")) { ed.PushUndo(); ed.Select(MakeGlobalWind(ed)); ed.dirty = true; }
        Tip("Makes a \"Wind\" object. It is saved with the scene. Weather's own Wind (above) keeps working without it.");
        return;
    }
    ImGui::PushID("globalwind");
    float yaw = YawOf(*z->transform);
    if (ImGui::SliderFloat("Direction (compass)", &yaw, -180.0f, 180.0f, "%.0f deg")) {
        ed.PushUndo(); z->transform->SetRotation(Quat::Euler(0.0f, yaw, 0.0f)); ed.dirty = true;
    }
    Tip("Seen from above: 0 = toward the forward (blue) axis, 90 = to the right.");
    if (ImGui::DragFloat("Strength", &z->strength, 0.1f, 0.0f, 100.0f, "%.1f m/s")) ed.dirty = true;
    Tip("About 3 is a breeze, 10 a strong wind, 20 a storm.");
    if (ImGui::SliderFloat("Gusts", &z->gustAmount, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
    if (ImGui::DragFloat("Gust every (s)", &z->gustFrequency, 0.01f, 0.01f, 10.0f, "%.2f gusts/s")) ed.dirty = true;
    if (ImGui::SliderFloat("Swirl", &z->turbulence, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
    if (ImGui::Button("Select the Wind object")) ed.Select(z->gameObject);
    // Live preview: a compass seen from above (up on screen = +Z), the arrow follows the real wind at the Scene view's middle.
    const Vec3 at = SpawnPoint(ed);
    const Vec3 wind = WindField::At(at, PreviewTime(ed));
    const float speed = wind.Magnitude();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float R = 38.0f;
    const ImVec2 c(p0.x + R + 8.0f, p0.y + R + 6.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircle(c, R, IM_COL32(120, 140, 160, 255), 40, 1.5f);
    dl->AddText(ImVec2(c.x - 4, c.y - R - 2), IM_COL32(170, 180, 190, 255), "N");
    if (speed > 0.02f) {
        const float len = R * std::min(1.0f, 0.15f + speed / 16.0f);
        const ImVec2 d(wind.x / speed, -wind.z / speed);
        const ImVec2 tip(c.x + d.x * len, c.y + d.y * len);
        const ImU32 col = StrengthColor(speed, 255);
        dl->AddLine(c, tip, col, 3.0f);
        const ImVec2 perp(-d.y, d.x);
        dl->AddTriangleFilled(tip, ImVec2(tip.x - d.x * 9 + perp.x * 5, tip.y - d.y * 9 + perp.y * 5),
                              ImVec2(tip.x - d.x * 9 - perp.x * 5, tip.y - d.y * 9 - perp.y * 5), col);
    }
    ImGui::Dummy(ImVec2(2 * R + 16.0f, 2 * R + 12.0f));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("Wind here now:");
    ImGui::Text("%.1f m/s", speed);
    ImGui::TextDisabled("%s", ed.isPlaying() ? "live (Play)" : "preview");
    ImGui::EndGroup();
    ImGui::PopID();
}

} // namespace oked
