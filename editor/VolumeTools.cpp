// Volumes in the editor: the Scene-view overlay (a translucent shape per volume, a colour per kind), the six face
// handles that resize the selected one, the tool that draws a volume by dragging on the ground, the Volume
// inspector and the menu rows. The geometry (what a volume contains, how a handle drag changes its size, what
// a ground drag makes) is okay::VolumeGeometry, so what is drawn here is exactly the region the game tests.
#include "VolumeTools.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Components/Volume.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

bool     g_showVolumes = true;
bool     g_showVolumeLabels = true;
unsigned g_volumeKinds = kVolumeKindsAll;

namespace {

// ---- Tool state ------------------------------------------------------------------------------------------
int   s_drawKind = -1;                 // the Volume kind the ground-drag tool draws (-1: not armed)
int   s_drawShape = 0;                 // VolumeShape of the next volume
float s_drawHeight = 3.0f;             // how tall a dragged volume is
bool  s_dragging = false;              // a ground drag is in progress
Vec3  s_dragStart{0, 0, 0};
Vec3  s_dragNow{0, 0, 0};
int   s_handleAxis = -1, s_handleSign = 0;   // the face handle being dragged

constexpr int kLogicBit = 5;

struct View {
    Mat4 vp;
    ImVec2 center, size, canvasPos;
    ImDrawList* dl = nullptr;
    bool Project(const Vec3& w, ImVec2& out) const {
        const Vec4 c = vp * Vec4{w, 1.0f};
        if (c.w <= 0.05f) return false;
        out = ImVec2(center.x + (c.x / c.w) * size.x * 0.5f, center.y - (c.y / c.w) * size.y * 0.5f);
        return true;
    }
    // A line clipped to the near plane, so a volume you stand inside still draws its far edges.
    void Line(const Vec3& a, const Vec3& b, ImU32 col, float th) const {
        Vec4 p = vp * Vec4{a, 1.0f}, q = vp * Vec4{b, 1.0f};
        const float da = p.z + p.w, db = q.z + q.w;
        if (da <= 0.0f && db <= 0.0f) return;
        auto lerp = [](const Vec4& u, const Vec4& v, float t) {
            return Vec4{u.x + (v.x - u.x) * t, u.y + (v.y - u.y) * t, u.z + (v.z - u.z) * t, u.w + (v.w - u.w) * t};
        };
        const float t = da / (da - db);
        if (da <= 0.0f) p = lerp(p, q, t); else if (db <= 0.0f) q = lerp(p, q, t);
        auto scr = [&](const Vec4& c) { return ImVec2(center.x + (c.x / c.w) * size.x * 0.5f, center.y - (c.y / c.w) * size.y * 0.5f); };
        dl->AddLine(scr(p), scr(q), col, th);
    }
    // A filled convex polygon, when every corner is in front of the camera.
    void Fill(const std::vector<Vec3>& pts, ImU32 col) const {
        std::vector<ImVec2> s(pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) if (!Project(pts[i], s[i])) return;
        dl->AddConvexPolyFilled(s.data(), (int)s.size(), col);
    }
};

// One region to draw: the geometry, where it is, what colour, and which fields a handle drag changes.
struct Region {
    VolumeGeometry g;
    GameObject* go = nullptr;
    Color color;
    std::string label;
    unsigned bit = 0;
    Vec3* size = nullptr;
    Vec3* offset = nullptr;
};

ImU32 Col32(const Color& c, int a) {
    auto b = [](float v) { return (int)(Mathf::Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(b(c.r), b(c.g), b(c.b), a);
}

void CollectRegions(Scene& scene, std::vector<Region>& out) {
    out.clear();
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go->IsDestroyed() || !go->transform) continue;
        for (Volume* v : go->GetComponents<Volume>()) {
            if (!v->enabled) continue;
            Region r;
            r.g = v->Geometry(); r.go = go; r.color = Volume::KindColor(v->kind);
            r.label = std::string(Volume::KindName(v->kind)) + ": " + go->name;
            r.bit = 1u << (int)v->kind;
            r.size = &v->size; r.offset = &v->offset;
            out.push_back(r);
        }
        for (LogicSource* ls : go->GetComponents<LogicSource>()) {
            if (!ls->enabled) continue;
            if (ls->kind != LogicSource::Kind::PressurePlate && ls->kind != LogicSource::Kind::TriggerZone) continue;
            Region r;
            r.g = {VolumeShape::Box, ls->zoneSize, ls->zoneOffset}; r.go = go; r.color = Color::FromBytes(70, 205, 205);
            r.label = std::string(LogicSource::KindName(ls->kind)) + ": " + go->name;
            r.bit = 1u << kLogicBit;
            r.size = &ls->zoneSize; r.offset = &ls->zoneOffset;
            out.push_back(r);
        }
    }
}

// The point of a region at unit coordinates `u` (+-1 on a box's faces), in world space.
Vec3 UnitToWorld(const Region& r, const Vec3& u) {
    return r.go->transform->TransformPoint({r.g.offset.x + u.x * r.g.size.x * 0.5f, r.g.offset.y + u.y * r.g.size.y * 0.5f,
                                            r.g.offset.z + u.z * r.g.size.z * 0.5f});
}

void DrawRegion(const View& v, const Region& r, bool selected) {
    if (!r.g.Valid()) return;
    const int fillA = selected ? 70 : 38, lineA = selected ? 255 : 190;
    const ImU32 fill = Col32(r.color, fillA), line = Col32(r.color, lineA);
    const float th = selected ? 2.0f : 1.4f;
    auto P = [&](float x, float y, float z) { return UnitToWorld(r, {x, y, z}); };
    switch (r.g.shape) {
        case VolumeShape::Box: {
            Vec3 c[8];
            for (int i = 0; i < 8; ++i) c[i] = P((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
            static const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
            for (auto& f : faces) v.Fill({c[f[0]], c[f[1]], c[f[2]], c[f[3]]}, fill);
            static const int e[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (auto& ed2 : e) v.Line(c[ed2[0]], c[ed2[1]], line, th);
            break;
        }
        case VolumeShape::Sphere: {
            constexpr int N = 36;
            for (int plane = 0; plane < 3; ++plane) {
                std::vector<Vec3> ring;
                for (int i = 0; i < N; ++i) {
                    const float a = (float)i / N * 6.2831853f, ca = std::cos(a), sa = std::sin(a);
                    ring.push_back(plane == 0 ? P(ca, 0, sa) : plane == 1 ? P(ca, sa, 0) : P(0, ca, sa));
                }
                v.Fill(ring, Col32(r.color, fillA / 2));
                for (int i = 0; i < N; ++i) v.Line(ring[i], ring[(i + 1) % N], line, plane == 0 ? th : th * 0.7f);
            }
            break;
        }
        case VolumeShape::Cylinder: {
            constexpr int N = 32;
            std::vector<Vec3> top, bot;
            for (int i = 0; i < N; ++i) {
                const float a = (float)i / N * 6.2831853f;
                top.push_back(P(std::cos(a), 1, std::sin(a)));
                bot.push_back(P(std::cos(a), -1, std::sin(a)));
            }
            v.Fill(top, fill); v.Fill(bot, fill);
            for (int i = 0; i < N; ++i) {
                const int j = (i + 1) % N;
                v.Fill({bot[i], bot[j], top[j], top[i]}, Col32(r.color, fillA / 2));
                v.Line(top[i], top[j], line, th); v.Line(bot[i], bot[j], line, th);
                if (i % 4 == 0) v.Line(bot[i], top[i], line, th * 0.8f);
            }
            break;
        }
    }
    if (g_showVolumeLabels) {
        ImVec2 sp;
        if (v.Project(UnitToWorld(r, {0, 1, 0}), sp)) {
            const ImVec2 ts = ImGui::CalcTextSize(r.label.c_str());
            const ImVec2 tp(sp.x - ts.x * 0.5f, sp.y - ts.y - 4.0f);
            v.dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 1), ImVec2(tp.x + ts.x + 3, tp.y + ts.y + 1), IM_COL32(0, 0, 0, 140), 3.0f);
            v.dl->AddText(tp, Col32(r.color, 255), r.label.c_str());
        }
    }
}

// The region the selected object's handles edit: its first enabled Volume, else its logic zone.
bool SelectedRegion(Scene& scene, GameObject* sel, Region& out) {
    if (!sel || !sel->transform) return false;
    std::vector<Region> all;
    CollectRegions(scene, all);
    for (const Region& r : all) if (r.go == sel) { out = r; return true; }
    return false;
}

Vec3 AxisOf(int a) { return a == 0 ? Vec3{1, 0, 0} : a == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}; }

// Mouse ray in world space.
void MouseRay(const View& v, const ImVec2& mouse, Vec3& o, Vec3& d) {
    const float nx = (mouse.x - v.canvasPos.x) / v.size.x * 2.0f - 1.0f;
    const float ny = 1.0f - (mouse.y - v.canvasPos.y) / v.size.y * 2.0f;
    const Mat4 inv = v.vp.Inverse();
    const Vec3 p0 = inv.MultiplyPoint({nx, ny, -1.0f});
    const Vec3 p1 = inv.MultiplyPoint({nx, ny, 1.0f});
    o = p0; d = (p1 - p0).Normalized();
}

// The ground point under the mouse: the first thing hit, else the plane y = planeY.
bool GroundPoint(EditorState& ed, const View& v, const ImVec2& mouse, bool useHits, float planeY, Vec3& out) {
    Vec3 o, d;
    MouseRay(v, mouse, o, d);
    if (useHits) {
        const RaycastHit3D rh = ed.scene().physics3D().Raycast(ed.scene(), o, d, 800.0f, nullptr);
        if (rh.hit) { out = rh.point; return true; }
    }
    if (std::fabs(d.y) < 1e-4f) return false;
    const float t = (planeY - o.y) / d.y;
    if (t < 0.0f) return false;
    out = o + d * t;
    return true;
}

GameObject* MakeVolumeObject(EditorState& ed, int kind, int shape, const Vec3& position, const Vec3& size) {
    GameObject* go = ed.CreateEmpty(std::string(Volume::KindName((Volume::Kind)kind)) + " Volume");
    go->transform->SetPosition(position);
    auto* vol = go->AddComponent<Volume>();
    vol->kind = (Volume::Kind)kind;
    vol->shape = (VolumeShape)shape;
    vol->size = size;
    if (vol->kind == Volume::Kind::Ambience) { auto* a = go->AddComponent<AudioSource>(); a->loop = true; a->spatial = false; }
    ed.view3D = true;
    ed.dirty = true;
    return go;
}

} // namespace

bool VolumeDrawActive() { return s_drawKind >= 0; }

// ===========================================================================
// Overlay, handles and the draw tool
// ===========================================================================

bool DrawVolumeOverlay(EditorState& ed, ImDrawList* dl, const Mat4& vp, const Vec3&, ImVec2 center, ImVec2 canvasSize,
                       ImVec2 canvasPos, bool clickable, ImGuiIO& io) {
    View v;
    v.vp = vp; v.center = center; v.size = canvasSize; v.canvasPos = canvasPos; v.dl = dl;
    bool consumed = false;

    // ---- Draw every region ----
    std::vector<Region> all;
    CollectRegions(ed.scene(), all);
    GameObject* sel = ed.selected();
    const unsigned mask = g_volumeKinds;
    for (const Region& r : all) {
        const bool selected = r.go == sel;
        if (!selected && (!g_showVolumes || !(mask & r.bit))) continue;
        DrawRegion(v, r, selected);
    }

    // ---- Face handles on the selected region ----
    Region rs;
    const bool haveSel = SelectedRegion(ed.scene(), sel, rs) && rs.g.Valid() && !VolumeDrawActive();
    if (!haveSel) { if (s_handleAxis >= 0) s_handleAxis = -1; }
    else {
        const Transform& t = *rs.go->transform;
        const ImU32 axc[3] = {IM_COL32(230, 90, 90, 255), IM_COL32(100, 220, 110, 255), IM_COL32(100, 160, 240, 255)};
        struct H { int axis, sign; ImVec2 sp; bool ok; };
        H hs[6]; int n = 0;
        for (int a = 0; a < 3; ++a)
            for (int s = -1; s <= 1; s += 2) {
                Vec3 u{0, 0, 0};
                (a == 0 ? u.x : a == 1 ? u.y : u.z) = (float)s;
                hs[n].axis = a; hs[n].sign = s; hs[n].ok = v.Project(UnitToWorld(rs, u), hs[n].sp);
                ++n;
            }
        const Vec3 centre = t.TransformPoint(rs.g.offset);
        if (s_handleAxis < 0) {
            if (clickable && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                float best = 11.0f * 11.0f; int bi = -1;
                for (int i = 0; i < 6; ++i) {
                    if (!hs[i].ok) continue;
                    const float dx = io.MousePos.x - hs[i].sp.x, dy = io.MousePos.y - hs[i].sp.y;
                    if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; bi = i; }
                }
                if (bi >= 0) { ed.PushUndo(); s_handleAxis = hs[bi].axis; s_handleSign = hs[bi].sign; consumed = true; }
            }
        } else {
            // Dragging: how far the mouse moved along the axis on screen, in world units, then in the object's own units.
            const int a = s_handleAxis;
            const Vec3 axisWorld = t.Rotation() * AxisOf(a);
            ImVec2 c1, c2;
            if (v.Project(centre, c1) && v.Project(centre + axisWorld, c2)) {
                const float sdx = c2.x - c1.x, sdy = c2.y - c1.y, sl = std::sqrt(sdx * sdx + sdy * sdy);
                if (sl > 1e-3f) {
                    const float dpx = (io.MouseDelta.x * sdx + io.MouseDelta.y * sdy) / sl;
                    const float world = dpx / sl;
                    const Vec3 sc = t.LossyScale();
                    float scv = a == 0 ? sc.x : a == 1 ? sc.y : sc.z;
                    if (std::fabs(scv) < 1e-4f) scv = 1.0f;
                    VolumeGeometry g = rs.g;
                    g.DragFace(a, s_handleSign, world / std::fabs(scv));
                    *rs.size = g.size; *rs.offset = g.offset;
                    ed.dirty = true;
                }
            }
            consumed = true;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_handleAxis = -1;
        }
        for (int i = 0; i < 6; ++i) {
            if (!hs[i].ok) continue;
            const bool cur = s_handleAxis == hs[i].axis && s_handleSign == hs[i].sign;
            dl->AddCircleFilled(hs[i].sp, cur ? 7.0f : 5.0f, axc[hs[i].axis]);
            dl->AddCircle(hs[i].sp, cur ? 7.0f : 5.0f, IM_COL32(255, 255, 255, 220), 12, 1.5f);
        }
        if (s_handleAxis >= 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f x %.2f x %.2f", rs.size->x, rs.size->y, rs.size->z);
            ImVec2 sp;
            if (v.Project(centre, sp)) dl->AddText(ImVec2(sp.x + 10, sp.y + 10), IM_COL32(255, 255, 255, 255), buf);
        }
    }

    // ---- Draw tool: drag on the ground ----
    if (s_drawKind >= 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { s_drawKind = -1; s_dragging = false; return consumed; }
        const Color col = Volume::KindColor((Volume::Kind)s_drawKind);
        char hint[160];
        std::snprintf(hint, sizeof(hint), "Drawing a %s volume (%s): drag on the ground.  Esc to stop.",
                      Volume::KindName((Volume::Kind)s_drawKind), VolumeGeometry::ShapeName((VolumeShape)s_drawShape));
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        const ImVec2 hp(canvasPos.x + 12, canvasPos.y + canvasSize.y - hs.y - 14);
        dl->AddRectFilled(ImVec2(hp.x - 6, hp.y - 4), ImVec2(hp.x + hs.x + 6, hp.y + hs.y + 4), IM_COL32(0, 0, 0, 170), 4.0f);
        dl->AddText(hp, Col32(col, 255), hint);
        if (!s_dragging && clickable && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            Vec3 p;
            if (GroundPoint(ed, v, io.MousePos, true, 0.0f, p)) { s_dragging = true; s_dragStart = p; s_dragNow = p; }
            consumed = true;
        } else if (s_dragging) {
            consumed = true;
            Vec3 p;
            if (GroundPoint(ed, v, io.MousePos, false, s_dragStart.y, p)) s_dragNow = p;
            Vec3 pos, size;
            VolumeGeometry::FromGroundDrag(s_dragStart, s_dragNow, s_drawHeight, pos, size);
            // A preview of the region as it will be made (an upright, unrotated one).
            const VolumeGeometry pg{(VolumeShape)s_drawShape, size, {0, 0, 0}};
            auto W = [&](float x, float y, float z) { return Vec3{pos.x + x * size.x * 0.5f, pos.y + y * size.y * 0.5f, pos.z + z * size.z * 0.5f}; };
            const ImU32 line = Col32(col, 255);
            const ImU32 fill = Col32(col, 60);
            if (pg.shape == VolumeShape::Box) {
                Vec3 c[8];
                for (int i = 0; i < 8; ++i) c[i] = W((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
                v.Fill({c[0], c[1], c[3], c[2]}, fill); v.Fill({c[4], c[5], c[7], c[6]}, fill);
                static const int e[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto& ee : e) v.Line(c[ee[0]], c[ee[1]], line, 2.0f);
            } else {
                std::vector<Vec3> top, bot;
                for (int i = 0; i < 32; ++i) {
                    const float a = (float)i / 32 * 6.2831853f;
                    top.push_back(W(std::cos(a), pg.shape == VolumeShape::Sphere ? 0.0f : 1.0f, std::sin(a)));
                    bot.push_back(W(std::cos(a), -1.0f, std::sin(a)));
                }
                v.Fill(top, fill);
                for (int i = 0; i < 32; ++i) {
                    v.Line(top[i], top[(i + 1) % 32], line, 2.0f);
                    if (pg.shape == VolumeShape::Cylinder) { v.Line(bot[i], bot[(i + 1) % 32], line, 2.0f); if (i % 4 == 0) v.Line(bot[i], top[i], line, 1.5f); }
                }
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.1f x %.1f x %.1f", size.x, size.y, size.z);
            ImVec2 sp;
            if (v.Project(W(0, 1, 0), sp)) dl->AddText(ImVec2(sp.x - 40, sp.y - 20), IM_COL32(255, 255, 255, 255), buf);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                s_dragging = false;
                const float dx = std::fabs(s_dragNow.x - s_dragStart.x), dz = std::fabs(s_dragNow.z - s_dragStart.z);
                if (dx < 0.3f && dz < 0.3f) {   // a click, not a drag: a default-sized one there
                    pos = {s_dragStart.x, s_dragStart.y + s_drawHeight * 0.5f, s_dragStart.z};
                    size = {4.0f, s_drawHeight, 4.0f};
                }
                GameObject* go = MakeVolumeObject(ed, s_drawKind, s_drawShape, pos, size);
                ed.Select(go);
                ConsoleLog(std::string("Drew a ") + Volume::KindName((Volume::Kind)s_drawKind) + " volume");
            }
        }
    }
    return consumed;
}

// ===========================================================================
// Inspector
// ===========================================================================

void DrawVolumeInspector(EditorState& ed, GameObject* go, Component* comp, Component*& toRemove, CompHeaderFn header) {
    auto* c = dynamic_cast<Volume*>(comp);
    if (!c) return;
    ImGui::PushID(c);
    if (header("Volume (zone)", c, &toRemove, true)) {
        const Volume::Kind k = c->kind;
        const Color kc = Volume::KindColor(k);
        ImGui::TextColored(ImVec4(kc.r, kc.g, kc.b, 1.0f), "%s", Volume::KindHint(k));
        auto tip = [](const char* t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t); };
        int kind = (int)c->kind;
        const char* kinds[] = {"Checkpoint", "Kill", "Force", "Atmosphere", "Ambience"};
        if (ImGui::Combo("Kind", &kind, kinds, IM_ARRAYSIZE(kinds))) {
            ed.PushUndo();
            c->kind = (Volume::Kind)kind;
            if (c->kind == Volume::Kind::Ambience && !go->GetComponent<AudioSource>()) { go->AddComponent<AudioSource>()->loop = true; }
            ed.dirty = true;
        }
        int shape = (int)c->shape;
        const char* shapes[] = {"Box", "Sphere", "Cylinder"};
        if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes))) { c->shape = (VolumeShape)shape; ed.dirty = true; }
        tip("Box, the ellipsoid inside the box, or the cylinder standing on the Y axis.\nDrag the dots on the faces in the Scene view to resize.");
        float sz[3] = {c->size.x, c->size.y, c->size.z};
        if (ImGui::DragFloat3("Size", sz, 0.05f, VolumeGeometry::kMinSize, 100000.0f)) { c->size = {sz[0], sz[1], sz[2]}; ed.dirty = true; }
        tip("The box the shape fills, in local units (the object's scale multiplies it).");
        float of[3] = {c->offset.x, c->offset.y, c->offset.z};
        if (ImGui::DragFloat3("Offset", of, 0.05f)) { c->offset = {of[0], of[1], of[2]}; ed.dirty = true; }
        tip("Where the centre sits, in local units.");
        if (!c->Geometry().Valid()) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Every size must be above zero or nothing is ever inside.");

        auto text = [&](const char* label, std::string& s, const char* hint) {
            char buf[96]; std::strncpy(buf, s.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText(label, buf, sizeof(buf))) { s = buf; ed.dirty = true; }
            tip(hint);
        };
        switch (k) {
            case Volume::Kind::Checkpoint:
                text("Who", c->activatorTag, "A tag (or name text) that counts as a body. A body with a Rigidbody 3D and a matching name counts too.");
                text("Event", c->eventName, "Sent to scripts as an On Logic Signal when a body reaches it. Blank: none.");
                ImGui::TextDisabled("The respawn point is the middle of the volume's floor: put its bottom on the ground.");
                break;
            case Volume::Kind::Kill: {
                text("Who", c->activatorTag, "A tag (or name text) that counts as a body.");
                int act = c->killAction;
                const char* acts[] = {"Respawn at checkpoint", "Destroy"};
                if (ImGui::Combo("Action", &act, acts, IM_ARRAYSIZE(acts))) { c->killAction = act; ed.dirty = true; }
                if (ImGui::DragFloat("Damage", &c->killDamage, 0.5f, 0.0f, 100000.0f)) ed.dirty = true;
                tip("Health taken from the body before the action (needs a Health Stat). 0: none.");
                text("Event", c->eventName, "Sent to scripts as an On Logic Signal each time it fires. Blank: none.");
                break;
            }
            case Volume::Kind::Force: {
                text("Who", c->activatorTag, "A tag (or name text) that counts as a body. Blank: every body with a Rigidbody 3D.");
                int fs = c->forceShape;
                const char* fss[] = {"Direction", "Radial (from centre)"};
                if (ImGui::Combo("Push", &fs, fss, IM_ARRAYSIZE(fss))) { c->forceShape = fs; ed.dirty = true; }
                if (fs == (int)Volume::ForceShape::Direction) {
                    float d[3] = {c->forceDirection.x, c->forceDirection.y, c->forceDirection.z};
                    if (ImGui::DragFloat3("Direction", d, 0.02f, -1.0f, 1.0f)) { c->forceDirection = {d[0], d[1], d[2]}; ed.dirty = true; }
                    if (ImGui::Checkbox("Turns with the object", &c->forceLocal)) ed.dirty = true;
                }
                if (ImGui::DragFloat("Strength", &c->forceStrength, 0.1f, -1000.0f, 1000.0f, "%.1f m/s\xc2\xb2")) ed.dirty = true;
                tip(fs == (int)Volume::ForceShape::Radial ? "Positive pushes away from the centre, negative pulls toward it." : "Acceleration along the direction.");
                ImGui::TextDisabled("Pushes bodies with a dynamic Rigidbody 3D.");
                break;
            }
            case Volume::Kind::Atmosphere: {
                float col[3] = {c->fogColor.r, c->fogColor.g, c->fogColor.b};
                if (ImGui::ColorEdit3("Fog colour", col)) { c->fogColor = Color{col[0], col[1], col[2], 1.0f}; ed.dirty = true; }
                if (ImGui::DragFloat("Fog start", &c->fogStart, 0.5f, 0.0f, 100000.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Fog end", &c->fogEnd, 0.5f, 0.0f, 100000.0f)) ed.dirty = true;
                if (ImGui::SliderFloat("Vignette", &c->vignette, 0.0f, 1.0f)) ed.dirty = true;
                if (ImGui::SliderFloat("Blend", &c->blend, 0.0f, 1.0f)) ed.dirty = true;
                tip("How far in from the edge it takes to reach full strength. 0: at once.");
                ImGui::TextDisabled("Takes over the scene's fog and vignette while the camera is inside (Play).");
                break;
            }
            case Volume::Kind::Ambience: {
                if (ImGui::DragFloat("Fade time", &c->fadeTime, 0.05f, 0.0f, 60.0f, "%.2f s")) ed.dirty = true;
                if (!go->GetComponent<AudioSource>()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "No Audio Source on this object: nothing to play.");
                    if (ImGui::SmallButton("Add Audio Source")) { ed.PushUndo(); go->AddComponent<AudioSource>()->loop = true; ed.dirty = true; }
                } else {
                    ImGui::TextDisabled("Plays this object's Audio Source while the camera is inside. Set its clip, and Loop.");
                }
                break;
            }
        }
        if (ed.isPlaying()) {
            if (k == Volume::Kind::Ambience) {
                const auto* src = go->GetComponent<AudioSource>();
                ImGui::TextDisabled("Play: camera weight %.2f   sound %s, level %.2f", c->ViewerWeight(),
                                    src && src->IsPlaying() ? "playing" : "stopped", src ? src->FadeLevel() : 0.0f);
            }
            else if (k == Volume::Kind::Atmosphere) ImGui::TextDisabled("Play: camera weight %.2f", c->ViewerWeight());
            else ImGui::TextDisabled("Play: %d inside", c->Occupants());
        }
        if (ImGui::SmallButton("Remove##vol")) toRemove = c;
    }
    ImGui::PopID();
}

// ===========================================================================
// Menus
// ===========================================================================

void DrawVolumeGameObjectMenu(EditorState& ed, bool& created) {
    if (!ImGui::BeginMenu("Volume")) return;
    const unsigned n = Volume::kKindCount;
    ImGui::TextDisabled("Draw by dragging in the Scene view:");
    for (unsigned k = 0; k < n; ++k) {
        const Volume::Kind kind = (Volume::Kind)k;
        char label[96];
        std::snprintf(label, sizeof(label), "Draw %s", Volume::KindName(kind));
        if (ImGui::MenuItem(label, nullptr, s_drawKind == (int)k)) {
            s_drawKind = s_drawKind == (int)k ? -1 : (int)k;
            s_dragging = false;
            ed.view3D = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Volume::KindHint(kind));
    }
    if (s_drawKind >= 0 && ImGui::MenuItem("Stop drawing")) s_drawKind = -1;
    ImGui::Separator();
    ImGui::TextDisabled("Shape of the next one:");
    for (int s = 0; s < kVolumeShapeCount; ++s)
        if (ImGui::MenuItem(VolumeGeometry::ShapeName((VolumeShape)s), nullptr, s_drawShape == s)) s_drawShape = s;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Height", &s_drawHeight, 0.1f, 0.2f, 200.0f, "%.1f");
    ImGui::Separator();
    ImGui::TextDisabled("Or create one where the Scene view looks:");
    for (unsigned k = 0; k < n; ++k) {
        const Volume::Kind kind = (Volume::Kind)k;
        char label[96];
        std::snprintf(label, sizeof(label), "New %s Volume", Volume::KindName(kind));
        if (ImGui::MenuItem(label)) {
            const Vec3 pos{ed.camTarget.x, ed.camTarget.y + s_drawHeight * 0.5f, ed.camTarget.z};
            ed.Select(MakeVolumeObject(ed, (int)k, s_drawShape, pos, {4.0f, s_drawHeight, 4.0f}));
            ConsoleLog(std::string("Created a ") + Volume::KindName(kind) + " volume");
            created = true;
        }
    }
    ImGui::EndMenu();
}

void DrawVolumeOverlayMenu(std::function<void()> onChange) {
    if (!ImGui::BeginMenu("Volumes")) return;
    if (ImGui::MenuItem("Show volumes", nullptr, &g_showVolumes) && onChange) onChange();
    if (ImGui::MenuItem("Labels", nullptr, &g_showVolumeLabels) && onChange) onChange();
    ImGui::Separator();
    for (int k = 0; k < Volume::kKindCount; ++k) {
        bool on = (g_volumeKinds & (1u << k)) != 0;
        const Color c = Volume::KindColor((Volume::Kind)k);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r, c.g, c.b, 1.0f));
        const bool hit = ImGui::MenuItem(Volume::KindName((Volume::Kind)k), nullptr, &on);
        ImGui::PopStyleColor();
        if (hit) { g_volumeKinds = on ? (g_volumeKinds | (1u << k)) : (g_volumeKinds & ~(1u << k)); if (onChange) onChange(); }
    }
    bool logic = (g_volumeKinds & (1u << kLogicBit)) != 0;
    if (ImGui::MenuItem("Logic zones (plates, trigger zones)", nullptr, &logic)) {
        g_volumeKinds = logic ? (g_volumeKinds | (1u << kLogicBit)) : (g_volumeKinds & ~(1u << kLogicBit));
        if (onChange) onChange();
    }
    if (ImGui::MenuItem("Show all")) { g_showVolumes = true; g_volumeKinds = kVolumeKindsAll; if (onChange) onChange(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The selected volume is always drawn, so its handles stay usable.");
    ImGui::EndMenu();
}

void VolumeAddComponentItems(EditorState& ed, GameObject* go, const std::function<bool(bool, const char*)>& item) {
    if (!go) return;
    static const char* labels[Volume::kKindCount] = {
        "Volume: Checkpoint (respawn point)", "Volume: Kill (respawn / destroy)", "Volume: Force (wind, updraft, current)",
        "Volume: Atmosphere (fog + vignette zone)", "Volume: Ambience (sound zone)"};
    for (int k = 0; k < Volume::kKindCount; ++k)
        if (item(true, labels[k])) {
            ed.PushUndo();
            auto* v = go->AddComponent<Volume>();
            v->kind = (Volume::Kind)k;
            if (v->kind == Volume::Kind::Ambience && !go->GetComponent<AudioSource>()) go->AddComponent<AudioSource>()->loop = true;
            ed.view3D = true; ed.dirty = true;
        }
}

} // namespace oked
