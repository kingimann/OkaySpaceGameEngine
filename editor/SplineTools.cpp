// Splines in the editor: the Scene-view overlay (draw every spline, edit the
// selected one's points and handles with move-gizmo conventions), the inspectors
// and Add Component rows of the spline family and the line / trail renderers,
// GameObject > Spline, edit-mode upkeep and the line / trail drawing.
#include "SplineTools.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "EditorWidgets.hpp"    // AccentCol
#include "MainShared.hpp"       // g_snap, g_snapSize, g_gizmoLocal, GetThumb
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Components/SplineMesh.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
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
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

extern bool g_uiHandled;    // a widget consumed this frame's click (main.cpp)

namespace {

// ---- The picked point and the drag in progress ----------------------------------
struct Pick {
    GameObject* obj = nullptr;
    std::string objName;
    int point = -1;
    int drag = 0;            // 0 none, 1 point in the view plane, 2 point along an axis, 3 / 4 handle in / out
    int axis = -1;
    bool undoPushed = false;
    Vec3 raw{0, 0, 0};       // the unsnapped world position being dragged
    Vec3 grab{0, 0, 0};      // knob minus where the cursor's ray met its plane
};
Pick s_pick;

// The selected object's Spline, keeping the picked point while it is the same
// object (undo / redo rebuild the scene and select its new copy).
Spline* SelectedSpline(EditorState& ed) {
    GameObject* sel = ed.selected();
    Spline* sp = sel ? sel->GetComponent<Spline>() : nullptr;
    if (!sp) { s_pick.point = -1; s_pick.drag = 0; s_pick.obj = nullptr; return nullptr; }
    if (s_pick.obj != sel || s_pick.objName != sel->name) {
        if (sel->name != s_pick.objName) s_pick.point = -1;
        s_pick.obj = sel; s_pick.objName = sel->name; s_pick.drag = 0;
    }
    if (s_pick.point >= sp->Count()) s_pick.point = -1;
    return sp;
}

// Push one undo step before a field changes: a continuous edit of the same field
// (a drag, typing) is one step.
void BeforeChange(EditorState& ed, const void* field) {
    static const void* s_field = nullptr;
    static double s_at = -10.0;
    const double now = ImGui::GetTime();
    if (field != s_field || now - s_at > 0.35) ed.PushUndo();
    s_field = field; s_at = now;
}

void Tip(const char* text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text); }

// Inspector fields that take an undo step and mark the scene dirty.
bool DragF(EditorState& ed, const char* label, float& v, float speed, float lo, float hi, const char* fmt = "%.2f") {
    float t = v;
    if (!ImGui::DragFloat(label, &t, speed, lo, hi, fmt)) return false;
    BeforeChange(ed, &v); v = t; ed.dirty = true; return true;
}
bool DragI(EditorState& ed, const char* label, int& v, float speed, int lo, int hi) {
    int t = v;
    if (!ImGui::DragInt(label, &t, speed, lo, hi)) return false;
    BeforeChange(ed, &v); v = std::clamp(t, lo, hi); ed.dirty = true; return true;
}
bool DragV2(EditorState& ed, const char* label, Vec2& v, float speed, float lo = 0.0f, float hi = 0.0f) {
    float t[2] = {v.x, v.y};
    if (!ImGui::DragFloat2(label, t, speed, lo, hi, "%.2f")) return false;
    BeforeChange(ed, &v); v = {t[0], t[1]}; ed.dirty = true; return true;
}
bool DragV3(EditorState& ed, const char* label, Vec3& v, float speed, const char* fmt = "%.2f") {
    float t[3] = {v.x, v.y, v.z};
    if (!ImGui::DragFloat3(label, t, speed, 0.0f, 0.0f, fmt)) return false;
    BeforeChange(ed, &v); v = {t[0], t[1], t[2]}; ed.dirty = true; return true;
}
bool Check(EditorState& ed, const char* label, bool& v) {
    bool t = v;
    if (!ImGui::Checkbox(label, &t)) return false;
    BeforeChange(ed, &v); v = t; ed.dirty = true; return true;
}
template <class E>
bool EnumCombo(EditorState& ed, const char* label, E& e, const char* const* names, int n) {
    int i = (int)e;
    if (!ImGui::Combo(label, &i, names, n)) return false;
    BeforeChange(ed, &e); e = (E)i; ed.dirty = true; return true;
}
bool TextField(EditorState& ed, const char* label, std::string& s) {
    char buf[512];
    std::strncpy(buf, s.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
    BeforeChange(ed, &s); s = buf; ed.dirty = true; return true;
}
bool ColorField(EditorState& ed, const char* label, Color& c) {
    float f[4] = {c.r, c.g, c.b, c.a};
    if (!ImGui::ColorEdit4(label, f)) return false;
    BeforeChange(ed, &c); c = {f[0], f[1], f[2], f[3]}; ed.dirty = true; return true;
}
// A combo of the scene's objects that carry a component (blank = `none`).
template <class T>
bool ObjectCombo(EditorState& ed, const char* label, std::string& name, const char* none) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name.empty() ? none : name.c_str())) {
        if (ImGui::Selectable(none, name.empty())) { BeforeChange(ed, &name); name.clear(); changed = true; }
        for (const auto& up : ed.scene().Objects()) {
            if (!up || up->transient || !up->GetComponent<T>()) continue;
            ImGui::PushID(up.get());
            if (ImGui::Selectable(up->name.c_str(), up->name == name)) { BeforeChange(ed, &name); name = up->name; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (changed) ed.dirty = true;
    return changed;
}

// Curve / gradient rows (up to 8 keys / stops).
bool CurveRows(EditorState& ed, const char* id, ParticleCurve& c) {
    bool changed = false;
    ImGui::PushID(id);
    bool on = c.Active();
    if (ImGui::Checkbox("Curve", &on)) { BeforeChange(ed, &c); if (on) c.SetLinear(1.0f, 0.0f); else c.Clear(); changed = true; }
    if (c.Active()) {
        ImGui::SameLine();
        if (Check(ed, "Smooth", c.smooth)) changed = true;
        for (int i = 0; i < c.count; ++i) {
            ImGui::PushID(i);
            float kv[2] = {c.keys[i].time, c.keys[i].value};
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
            if (ImGui::DragFloat2("##k", kv, 0.01f, 0.0f, 10.0f, "%.2f")) {
                BeforeChange(ed, &c.keys[i]); c.keys[i].time = Mathf::Clamp01(kv[0]); c.keys[i].value = std::max(0.0f, kv[1]); changed = true;
            }
            Tip("Where along (0 = start, 1 = end) and the width multiplier there.");
            ImGui::SameLine();
            if (c.count > 1 && ImGui::SmallButton("x")) { ed.PushUndo(); c.Remove(i); changed = true; ImGui::PopID(); break; }
            ImGui::PopID();
        }
        if (c.count < ParticleCurve::kMaxKeys && ImGui::SmallButton("Add Key")) { ed.PushUndo(); c.Add(0.5f, c.Evaluate(0.5f)); changed = true; }
    }
    ImGui::PopID();
    if (changed) ed.dirty = true;
    return changed;
}
bool GradientRows(EditorState& ed, const char* id, ParticleGradient& g) {
    bool changed = false;
    ImGui::PushID(id);
    bool on = g.Active();
    if (ImGui::Checkbox("Gradient", &on)) {
        BeforeChange(ed, &g);
        if (on) g.SetTwo(Color{1, 1, 1, 1}, Color{1, 1, 1, 0}); else g.Clear();
        changed = true;
    }
    for (int i = 0; i < g.count; ++i) {
        ImGui::PushID(i);
        float f[4] = {g.stops[i].color.r, g.stops[i].color.g, g.stops[i].color.b, g.stops[i].color.a};
        if (ImGui::ColorEdit4("##c", f, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            BeforeChange(ed, &g.stops[i]); g.stops[i].color = {f[0], f[1], f[2], f[3]}; changed = true;
        }
        ImGui::SameLine();
        float t = g.stops[i].time;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
        if (ImGui::SliderFloat("##t", &t, 0.0f, 1.0f, "at %.2f")) { BeforeChange(ed, &g.stops[i].time); g.stops[i].time = t; changed = true; }
        ImGui::SameLine();
        if (g.count > 1 && ImGui::SmallButton("x")) { ed.PushUndo(); g.Remove(i); changed = true; ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (g.Active() && g.count < ParticleGradient::kMaxStops && ImGui::SmallButton("Add Stop")) {
        ed.PushUndo(); g.Add(0.5f, g.Evaluate(0.5f)); changed = true;
    }
    ImGui::PopID();
    if (changed) ed.dirty = true;
    return changed;
}

// ---- Scene-view projection ------------------------------------------------------
struct View {
    const Mat4* vp; ImVec2 center, size;
    Vec4 Clip(const Vec3& p) const { return (*vp) * Vec4{p, 1}; }
    ImVec2 Project(const Vec4& c) const {
        return ImVec2(center.x + (c.x / c.w) * size.x * 0.5f, center.y - (c.y / c.w) * size.y * 0.5f);
    }
    bool ToScreen(const Vec3& p, ImVec2& out) const {
        const Vec4 c = Clip(p);
        if (c.z + c.w <= 0.0f || c.w <= 1e-5f) return false;
        out = Project(c);
        return true;
    }
    // A world segment clipped to the near plane.
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

float SegDist(ImVec2 p, ImVec2 a, ImVec2 b, float* along = nullptr) {
    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    if (along) *along = t;
    const float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return std::sqrt(ex * ex + ey * ey);
}
float PtDist(ImVec2 a, ImVec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

// The world ray under the mouse.
void MouseRay(const Mat4& vp, ImVec2 canvasPos, ImVec2 canvasSize, ImVec2 mouse, Vec3& origin, Vec3& dir) {
    const float nx = (mouse.x - canvasPos.x) / canvasSize.x * 2.0f - 1.0f;
    const float ny = 1.0f - (mouse.y - canvasPos.y) / canvasSize.y * 2.0f;
    const Mat4 inv = vp.Inverse();
    origin = inv.MultiplyPoint({nx, ny, -1.0f});
    dir = (inv.MultiplyPoint({nx, ny, 1.0f}) - origin).Normalized();
}
// Where the ray meets the plane through `p` with normal `n` (false when parallel / behind).
bool RayPlane(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& n, Vec3& hit) {
    const float den = Vec3::Dot(d, n);
    if (std::fabs(den) < 1e-5f) return false;
    const float t = Vec3::Dot(p - o, n) / den;
    if (t <= 0.0f) return false;
    hit = o + d * t;
    return true;
}
Vec3 Snapped(const Vec3& p, bool on) {
    if (!on || g_snapSize <= 0.0f) return p;
    return {std::round(p.x / g_snapSize) * g_snapSize, std::round(p.y / g_snapSize) * g_snapSize,
            std::round(p.z / g_snapSize) * g_snapSize};
}

// Polyline of a spline's curve in world space, with each sample's parameter.
void CurvePoints(const Spline& sp, std::vector<Vec3>& pts, std::vector<float>& ts) {
    pts.clear(); ts.clear();
    const int segs = sp.SegmentCount();
    if (segs <= 0) return;
    const int per = std::clamp(2000 / segs, 2, 24);
    const int n = segs * per;
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n;
        pts.push_back(sp.EvaluatePosition(t));
        ts.push_back(t);
    }
}

// The ground under a world point (colliders and terrain), ignoring `self`.
bool GroundBelow(Scene& sc, const Vec3& at, GameObject* self, Vec3& out) {
    const RaycastHit3D h = sc.physics3D().Raycast(sc, at + Vec3{0, 60, 0}, {0, -1, 0}, 400.0f, self, Physics3D::kAllLayers, false);
    if (!h.hit) return false;
    out = h.point;
    return true;
}

void SnapToGround(EditorState& ed, Spline* sp) {
    if (!sp || !sp->transform) return;
    const Mat4 m = sp->LocalToWorld(), inv = m.Inverse();
    ed.PushUndo();
    int moved = 0;
    for (SplinePoint& p : sp->points) {
        Vec3 g;
        if (!GroundBelow(ed.scene(), m.MultiplyPoint(p.position), sp->gameObject, g)) continue;
        p.position = inv.MultiplyPoint(g);
        ++moved;
    }
    ed.dirty = true;
    ConsoleLog("Spline: snapped " + std::to_string(moved) + " of " + std::to_string(sp->Count()) + " points to the ground");
}

// A new point after `i`: halfway along the next segment, or past the end.
void InsertAfter(EditorState& ed, Spline* sp, int i) {
    ed.PushUndo();
    const int segs = sp->SegmentCount();
    if (i < segs) {
        s_pick.point = sp->SplitAt(((float)i + 0.5f) / (float)segs);
    } else if (sp->Count() >= 1) {
        const SplinePoint& last = sp->points.back();
        Vec3 dir = sp->Count() >= 2 ? (last.position - sp->points[(std::size_t)sp->Count() - 2].position) : Vec3{0, 0, 4};
        if (dir.SqrMagnitude() < 1e-6f) dir = {0, 0, 4};
        SplinePoint p = last;
        p.position = last.position + dir;
        p.tangentIn = last.tangentIn; p.tangentOut = last.tangentOut;
        s_pick.point = sp->InsertPoint(sp->Count(), p);
    }
    ed.dirty = true;
}

void RemovePoint(EditorState& ed, Spline* sp, int i) {
    if (!sp || i < 0 || i >= sp->Count()) return;
    ed.PushUndo();
    sp->RemovePoint(i);
    if (s_pick.point >= sp->Count()) s_pick.point = sp->Count() - 1;
    ed.dirty = true;
}

// ---- Defaults for new components -------------------------------------------------
void DefaultPoints(Spline* sp) {
    sp->points.clear();
    for (const Vec3& p : {Vec3{-4, 0, 0}, Vec3{0, 0, 3}, Vec3{4, 0, 0}}) sp->AddPoint(p);
}
Spline* EnsureSpline(GameObject* go) {
    Spline* sp = go->GetComponent<Spline>();
    if (!sp) { sp = go->AddComponent<Spline>(); DefaultPoints(sp); }
    return sp;
}

} // namespace

// ---- Scene-view overlay ---------------------------------------------------------

bool SplinePointPicked(EditorState& ed) {
    return SelectedSpline(ed) != nullptr && s_pick.point >= 0;
}

bool SplineDeletePickedPoint(EditorState& ed) {
    Spline* sp = SelectedSpline(ed);
    if (!sp || s_pick.point < 0) return false;
    RemovePoint(ed, sp, s_pick.point);
    return true;
}

void DrawSplineOverlay(EditorState& ed, ImDrawList* dl, const Mat4& vp, const Vec3& eye, ImVec2 center,
                       ImVec2 canvasSize, ImVec2 canvasPos, bool clickable, ImGuiIO& io) {
    const View v{&vp, center, canvasSize};
    Spline* sel = SelectedSpline(ed);
    const ImU32 curveCol = IM_COL32(110, 190, 255, 150), selCurveCol = IM_COL32(255, 205, 90, 255);
    std::vector<Vec3> pts;
    std::vector<float> ts;
    // Every spline's curve; a click on one that isn't selected selects it.
    GameObject* clickSelect = nullptr;
    for (const auto& up : ed.scene().Objects()) {
        if (!up || !up->active) continue;
        Spline* sp = up->GetComponent<Spline>();
        if (!sp || sp == sel) continue;
        CurvePoints(*sp, pts, ts);
        ImVec2 prev; bool prevOk = false;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            ImVec2 cur; const bool ok = v.ToScreen(pts[i], cur);
            if (i > 0) v.Line(dl, pts[i - 1], pts[i], curveCol, 1.5f);
            if (ok && prevOk && clickable && !clickSelect && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                SegDist(io.MousePos, prev, cur) < 6.0f)
                clickSelect = up.get();
            prev = cur; prevOk = ok;
        }
    }
    if (clickSelect) { ed.Select(clickSelect); g_uiHandled = true; return; }
    if (!sel) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && s_pick.point >= 0) { s_pick.point = -1; s_pick.drag = 0; }
    const Mat4 m = sel->LocalToWorld(), inv = m.Inverse();
    const int n = sel->Count();
    const bool bezier = sel->type == Spline::Type::Bezier;
    auto world = [&](int i) { return m.MultiplyPoint(sel->points[(std::size_t)i].position); };
    auto handleWorld = [&](int i, bool out) {
        const SplinePoint& p = sel->points[(std::size_t)i];
        return m.MultiplyPoint(p.position + (out ? sel->HandleOut(i) : sel->HandleIn(i)));
    };
    auto handleUsed = [&](int i, bool out) { return sel->closed || (out ? i < n - 1 : i > 0); };

    // The selected curve, with an up tick at every point (shows roll).
    CurvePoints(*sel, pts, ts);
    std::vector<ImVec2> scr(pts.size());
    std::vector<char> scrOk(pts.size(), 0);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        scrOk[i] = v.ToScreen(pts[i], scr[i]);
        if (i > 0) v.Line(dl, pts[i - 1], pts[i], selCurveCol, 2.5f);
    }
    const float tick = std::max(0.3f, ed.camDist * 0.04f);
    for (int i = 0; i < n; ++i) {
        const float t = sel->SegmentCount() > 0 ? std::min(1.0f, (float)i / (float)sel->SegmentCount()) : 0.0f;
        const Vec3 p = world(i);
        v.Line(dl, p, p + sel->EvaluateUp(t) * tick, IM_COL32(120, 230, 140, 200), 1.5f);
    }

    // Gizmo arms of the picked point (world axes, or the object's in Local mode).
    const float armLen = ed.camDist * 0.18f;
    Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (g_gizmoLocal && sel->transform) { axes[0] = sel->transform->Right(); axes[1] = sel->transform->Up(); axes[2] = sel->transform->Forward(); }
    ImVec2 pickScr, tip[3]; bool pickOk = false, tipOk[3] = {false, false, false};
    if (s_pick.point >= 0) {
        const Vec3 p = world(s_pick.point);
        pickOk = v.ToScreen(p, pickScr);
        for (int a = 0; a < 3; ++a) tipOk[a] = v.ToScreen(p + axes[a] * armLen, tip[a]);
    }

    // What is under the cursor (priority: gizmo arm, handle, point, curve).
    int hoverAxis = -1, hoverHandle = -1, hoverPoint = -1;   // hoverHandle: 0 in, 1 out (picked point)
    float hoverT = -1.0f;
    ImVec2 hoverCurve;
    if (clickable && s_pick.drag == 0) {
        const ImVec2 mp = io.MousePos;
        if (pickOk) {
            float best = 11.0f;
            for (int a = 0; a < 3; ++a)
                if (tipOk[a]) { const float d = SegDist(mp, pickScr, tip[a]); if (d < best) { best = d; hoverAxis = a; } }
        }
        if (hoverAxis < 0 && s_pick.point >= 0 && bezier) {
            float best = 8.0f;
            for (int h = 0; h < 2; ++h) {
                if (!handleUsed(s_pick.point, h == 1)) continue;
                ImVec2 hs;
                if (v.ToScreen(handleWorld(s_pick.point, h == 1), hs) && PtDist(mp, hs) < best) { best = PtDist(mp, hs); hoverHandle = h; }
            }
        }
        if (hoverAxis < 0 && hoverHandle < 0) {
            float best = 9.0f;
            for (int i = 0; i < n; ++i) {
                ImVec2 ps;
                if (v.ToScreen(world(i), ps) && PtDist(mp, ps) < best) { best = PtDist(mp, ps); hoverPoint = i; }
            }
        }
        if (hoverAxis < 0 && hoverHandle < 0 && hoverPoint < 0) {
            float best = 7.0f;
            for (std::size_t i = 1; i < pts.size(); ++i) {
                if (!scrOk[i - 1] || !scrOk[i]) continue;
                float along = 0.0f;
                const float d = SegDist(mp, scr[i - 1], scr[i], &along);
                if (d < best) {
                    best = d;
                    hoverT = ts[i - 1] + (ts[i] - ts[i - 1]) * along;
                    hoverCurve = ImVec2(scr[i - 1].x + (scr[i].x - scr[i - 1].x) * along, scr[i - 1].y + (scr[i].y - scr[i - 1].y) * along);
                }
            }
        }
    }

    // Handles (Bezier): faint for every point, bright for the picked one.
    if (bezier) {
        for (int i = 0; i < n; ++i) {
            const bool picked = i == s_pick.point;
            const Vec3 p = world(i);
            for (int h = 0; h < 2; ++h) {
                if (!handleUsed(i, h == 1)) continue;
                const Vec3 hw = handleWorld(i, h == 1);
                const ImU32 col = picked ? (hoverHandle == h || (s_pick.drag == 3 + h) ? IM_COL32(255, 240, 120, 255) : IM_COL32(240, 150, 255, 255))
                                         : IM_COL32(200, 140, 230, 110);
                v.Line(dl, p, hw, col, picked ? 2.0f : 1.0f);
                ImVec2 hs;
                if (v.ToScreen(hw, hs)) {
                    const float r = picked ? 5.0f : 3.0f;
                    dl->AddRectFilled(ImVec2(hs.x - r, hs.y - r), ImVec2(hs.x + r, hs.y + r), col);
                }
            }
        }
    }
    // Points.
    for (int i = 0; i < n; ++i) {
        ImVec2 ps;
        if (!v.ToScreen(world(i), ps)) continue;
        const bool picked = i == s_pick.point, hot = i == hoverPoint;
        const SplinePoint::Mode mode = sel->points[(std::size_t)i].mode;
        const ImU32 fill = picked ? IM_COL32(255, 230, 90, 255) : hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 205, 90, 230);
        const float r = picked ? 7.0f : 5.5f;
        if (mode == SplinePoint::Mode::Linear || mode == SplinePoint::Mode::Free)   // corners: diamonds
            dl->AddQuadFilled(ImVec2(ps.x, ps.y - r), ImVec2(ps.x + r, ps.y), ImVec2(ps.x, ps.y + r), ImVec2(ps.x - r, ps.y), fill);
        else
            dl->AddCircleFilled(ps, r, fill, 16);
        dl->AddCircle(ps, r, IM_COL32(40, 40, 40, 220), 16, 1.5f);
    }
    // The picked point's move gizmo.
    if (pickOk) {
        const ImU32 axc[3] = {IM_COL32(230, 80, 80, 255), IM_COL32(90, 210, 100, 255), IM_COL32(90, 150, 240, 255)};
        for (int a = 0; a < 3; ++a) {
            if (!tipOk[a]) continue;
            const ImU32 c = (hoverAxis == a || (s_pick.drag == 2 && s_pick.axis == a)) ? IM_COL32(255, 230, 90, 255) : axc[a];
            dl->AddLine(pickScr, tip[a], c, 3.0f);
            dl->AddCircleFilled(tip[a], 5.0f, c);
        }
    }
    if (hoverT >= 0.0f) {   // where a click adds a point
        dl->AddCircle(hoverCurve, 6.0f, IM_COL32(255, 255, 255, 230), 12, 2.0f);
        dl->AddLine(ImVec2(hoverCurve.x - 3, hoverCurve.y), ImVec2(hoverCurve.x + 3, hoverCurve.y), IM_COL32(255, 255, 255, 230), 1.5f);
        dl->AddLine(ImVec2(hoverCurve.x, hoverCurve.y - 3), ImVec2(hoverCurve.x, hoverCurve.y + 3), IM_COL32(255, 255, 255, 230), 1.5f);
    }

    // ---- Clicks ----
    if (clickable && s_pick.drag == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Vec3 ro, rd;
        MouseRay(vp, canvasPos, canvasSize, io.MousePos, ro, rd);
        auto startPlaneDrag = [&](int kind, const Vec3& knob) {
            s_pick.drag = kind; s_pick.undoPushed = false;
            Vec3 hit;
            s_pick.grab = RayPlane(ro, rd, knob, (eye - knob).Normalized(), hit) ? knob - hit : Vec3{0, 0, 0};
            s_pick.raw = knob;
        };
        if (hoverAxis >= 0) {
            s_pick.drag = 2; s_pick.axis = hoverAxis; s_pick.undoPushed = false; s_pick.raw = world(s_pick.point);
        } else if (hoverHandle >= 0) {
            startPlaneDrag(3 + hoverHandle, handleWorld(s_pick.point, hoverHandle == 1));
        } else if (hoverPoint >= 0) {
            s_pick.point = hoverPoint;
            startPlaneDrag(1, world(hoverPoint));
        } else if (hoverT >= 0.0f) {
            ed.PushUndo();
            const int idx = sel->SplitAt(hoverT);
            if (idx >= 0) { s_pick.point = idx; ed.dirty = true; ConsoleLog("Spline: added a point"); }
        } else if (s_pick.point >= 0) {
            s_pick.point = -1;   // first click off the spline drops the point, the next one the object
        } else {
            return;              // not ours: the Scene view picks objects
        }
        g_uiHandled = true;
    }

    // ---- Drags ----
    if (s_pick.drag != 0) {
        g_uiHandled = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || s_pick.point < 0 || s_pick.point >= n) {
            s_pick.drag = 0;
        } else if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            if (!s_pick.undoPushed) { ed.PushUndo(); s_pick.undoPushed = true; }
            const bool snap = g_snap || io.KeyCtrl;
            SplinePoint& p = sel->points[(std::size_t)s_pick.point];
            if (s_pick.drag == 2) {
                // Along one axis: the mouse motion projected on the arm, like the move gizmo.
                const int a = s_pick.axis;
                if (tipOk[a] && pickOk) {
                    const ImVec2 sd{tip[a].x - pickScr.x, tip[a].y - pickScr.y};
                    const float sl = std::sqrt(sd.x * sd.x + sd.y * sd.y);
                    if (sl > 1e-3f) {
                        const float along = (io.MouseDelta.x * sd.x + io.MouseDelta.y * sd.y) / sl;
                        s_pick.raw += axes[a] * (along * armLen / sl);
                        p.position = inv.MultiplyPoint(Snapped(s_pick.raw, snap));
                    }
                }
            } else {
                // Point or handle: in the plane through it facing the camera.
                Vec3 ro, rd, hit;
                MouseRay(vp, canvasPos, canvasSize, io.MousePos, ro, rd);
                if (RayPlane(ro, rd, s_pick.raw, (eye - s_pick.raw).Normalized(), hit)) {
                    s_pick.raw = hit + s_pick.grab;
                    const Vec3 local = inv.MultiplyPoint(Snapped(s_pick.raw, snap));
                    if (s_pick.drag == 1) p.position = local;
                    else sel->SetHandle(s_pick.point, s_pick.drag == 4, local - p.position, io.KeyShift);
                }
            }
            ed.dirty = true;
        }
    }

    // How to edit, along the bottom of the view.
    const char* hint = s_pick.point >= 0
        ? "Spline: drag the point or an arrow (Ctrl snaps), drag a handle (Shift breaks it), Delete removes, Esc deselects"
        : "Spline: click a point to edit it, click the curve to add one";
    const ImVec2 tsz = ImGui::CalcTextSize(hint);
    const ImVec2 hp(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y - tsz.y - 14.0f);
    dl->AddRectFilled(ImVec2(hp.x - 8, hp.y - 4), ImVec2(hp.x + tsz.x + 8, hp.y + tsz.y + 4), IM_COL32(0, 0, 0, 160), 5.0f);
    dl->AddText(hp, IM_COL32(255, 220, 130, 255), hint);
}

// ---- Lines and trails over the 3D image -------------------------------------------

void DrawLinesAndTrails(EditorState& ed, ImDrawList* dl, const Mat4& vp, const Vec3& eye, ImVec2 viewMin, ImVec2 viewMax,
                        const GameObject* ignore, int cullingMask, bool (*occluded)(float, float, float)) {
    static std::vector<LineDraw> draws;
    CollectLineDraws(ed.scene(), vp, eye, viewMin.x, viewMin.y, viewMax.x - viewMin.x, viewMax.y - viewMin.y, draws,
                     ignore, cullingMask, occluded);
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    for (const LineDraw& d : draws) {
        SDL_Texture* tex = d.texture.empty() ? nullptr : GetThumb(d.texture);
        if (tex) dl->PushTexture(ImTextureRef((ImTextureID)tex));
        const int count = (int)d.tris.size();
        dl->PrimReserve(count, count);
        for (const LineDrawVertex& q : d.tris) {
            const ImU32 col = IM_COL32((int)(Mathf::Clamp01(q.color.r) * 255.0f), (int)(Mathf::Clamp01(q.color.g) * 255.0f),
                                       (int)(Mathf::Clamp01(q.color.b) * 255.0f), (int)(Mathf::Clamp01(q.color.a) * 255.0f));
            dl->PrimVtx(ImVec2(q.x, q.y), tex ? ImVec2(q.u, q.v) : white, col);
        }
        if (tex) dl->PopTexture();
    }
}

// ---- Inspectors -------------------------------------------------------------------

void DrawSplineInspector(EditorState& ed, GameObject* go, Component* comp, Component*& toRemove, CompHeaderFn header) {
    if (!go || !comp) return;
    if (auto* sp = dynamic_cast<Spline*>(comp)) {
        if (header("Spline", sp, &toRemove, true)) {
            static const char* kTypes[] = {"Bezier (handles)", "Catmull-Rom (through points)"};
            EnumCombo(ed, "Type##spl", sp->type, kTypes, 2);
            Tip("Bezier: each point has in / out handles. Catmull-Rom: the curve runs smoothly through the points by itself.");
            Check(ed, "Closed##spl", sp->closed);
            Tip("Join the last point back to the first (a loop).");
            ImGui::SameLine();
            static const char* kUp[] = {"Minimal twist", "World up"};
            ImGui::SetNextItemWidth(150.0f);
            EnumCombo(ed, "Up##spl", sp->upMode, kUp, 2);
            Tip("How the up direction runs along the curve. Minimal twist never flips (loops, corkscrews); World up keeps it as level as it can (roads, fences).");
            ImGui::TextDisabled("%d points, %.2f m long", sp->Count(), sp->Length());
            if (ImGui::Button("Add Point##spl")) InsertAfter(ed, sp, sp->Count() - 1);
            Tip("Append a point past the end.");
            ImGui::SameLine();
            if (ImGui::Button("Snap Points To Ground##spl")) SnapToGround(ed, sp);
            Tip("Drop every point onto the collider or terrain below it.");
            ImGui::SameLine();
            if (ImGui::Button("Reverse##spl")) {
                ed.PushUndo();
                std::reverse(sp->points.begin(), sp->points.end());
                for (SplinePoint& p : sp->points) std::swap(p.tangentIn, p.tangentOut);
                ed.dirty = true;
            }
            Tip("Run the curve the other way (followers and meshes start from the other end).");
            if (ed.selected() == go) ImGui::TextDisabled("Scene view: click a point to edit it, click the curve to add one.");
            else ImGui::TextDisabled("Select the object to edit the points in the Scene view.");
            ImGui::SeparatorText("Points (local)");
            for (int i = 0; i < sp->Count(); ++i) {
                ImGui::PushID(i);
                const bool picked = ed.selected() == go && s_pick.point == i;
                char lbl[16]; std::snprintf(lbl, sizeof lbl, "%d", i);
                if (ImGui::Selectable(lbl, picked, 0, ImVec2(22, 0)) && ed.selected() == go) s_pick.point = picked ? -1 : i;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50.0f);
                DragV3(ed, "##pos", sp->points[(std::size_t)i].position, 0.05f);
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) { InsertAfter(ed, sp, i); ImGui::PopID(); break; }
                Tip("Insert a point after this one (on the curve, so the shape stays).");
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) { RemovePoint(ed, sp, i); ImGui::PopID(); break; }
                Tip("Remove this point.");
                ImGui::PopID();
            }
            const int pi = ed.selected() == go ? s_pick.point : -1;
            if (pi >= 0 && pi < sp->Count()) {
                ImGui::SeparatorText("Picked Point");
                SplinePoint& p = sp->points[(std::size_t)pi];
                static const char* kModes[] = {"Auto Smooth", "Aligned", "Free (corner)", "Linear (straight)"};
                int mi = (int)p.mode;
                if (ImGui::Combo("Handles##spl", &mi, kModes, 4)) { ed.PushUndo(); sp->SetMode(pi, (SplinePoint::Mode)mi); ed.dirty = true; }
                Tip("Auto Smooth: placed for you. Aligned: in and out stay in line. Free: independent (a corner). Linear: straight to the neighbours.");
                if (sp->type == Spline::Type::Bezier) {
                    Vec3 hi = sp->HandleIn(pi), ho = sp->HandleOut(pi);
                    float in3[3] = {hi.x, hi.y, hi.z}, out3[3] = {ho.x, ho.y, ho.z};
                    if (ImGui::DragFloat3("In##spl", in3, 0.05f)) { BeforeChange(ed, &p.tangentIn); sp->SetHandle(pi, false, {in3[0], in3[1], in3[2]}); ed.dirty = true; }
                    if (ImGui::DragFloat3("Out##spl", out3, 0.05f)) { BeforeChange(ed, &p.tangentOut); sp->SetHandle(pi, true, {out3[0], out3[1], out3[2]}); ed.dirty = true; }
                }
                DragF(ed, "Roll##spl", p.roll, 0.5f, -720.0f, 720.0f, "%.1f deg");
                Tip("Bank around the direction of travel; positive tilts the up direction towards the +X side of an object riding the curve.");
                DragV2(ed, "Scale##spl", p.scale, 0.01f, 0.0f, 100.0f);
                Tip("Width (x) and height (y) of a Spline Mesh's cross-section here.");
            }
        }
    }
    if (auto* f = dynamic_cast<SplineFollower*>(comp)) {
        if (header("Spline Follower", f, &toRemove, true)) {
            ObjectCombo<Spline>(ed, "Spline##sf", f->spline, "(none)");
            static const char* kDrive[] = {"Speed", "Time (one pass)"};
            EnumCombo(ed, "Drive##sf", f->drive, kDrive, 2);
            if (f->drive == SplineFollower::Drive::Speed) DragF(ed, "Speed##sf", f->speed, 0.05f, -100.0f, 100.0f, "%.2f m/s");
            else DragF(ed, "Duration##sf", f->duration, 0.05f, 0.05f, 3600.0f, "%.2f s");
            static const char* kWrap[] = {"Loop", "Ping-Pong", "Once"};
            EnumCombo(ed, "At The End##sf", f->wrap, kWrap, 3);
            Tip("Loop: start over. Ping-Pong: turn round. Once: stop. Each end sends on_spline_end to the object's scripts.");
            float start = f->startAt;
            if (ImGui::SliderFloat("Start At##sf", &start, 0.0f, 1.0f)) { BeforeChange(ed, &f->startAt); f->startAt = start; ed.dirty = true; }
            Check(ed, "Play On Start##sf", f->playOnStart);
            ImGui::SameLine();
            Check(ed, "Face Along##sf", f->orient);
            Tip("Turn its forward (+Z) the way it moves, with the spline's up. Cameras look down -Z: ride a camera on a child turned 180 degrees.");
            DragV3(ed, "Offset##sf", f->offset, 0.05f);
            Tip("From the curve, in the rider's own axes: x along its +X, y up, z forward.");
            if (ed.isPlaying()) {
                ImGui::Text("%.2f m along (%s)", f->Distance(), f->IsPlaying() ? "moving" : "stopped");
                if (f->IsPlaying()) { if (ImGui::SmallButton("Stop##sf")) f->Stop(); }
                else if (ImGui::SmallButton("Play##sf")) f->Play();
            }
            if (!f->Target()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Pick the object that holds the Spline.");
        }
    }
    if (auto* sm = dynamic_cast<SplineMesh*>(comp)) {
        if (header("Spline Mesh", sm, &toRemove, true)) {
            ObjectCombo<Spline>(ed, "Spline##sm", sm->spline, "(this object)");
            static const char* kModes[] = {"Extrude a profile", "Repeat a mesh"};
            EnumCombo(ed, "Mode##sm", sm->mode, kModes, 2);
            if (sm->mode == SplineMesh::Mode::Extrude) {
                static const char* kProf[] = {"Road", "Flat Strip", "Tube", "Rail / Curb", "Custom"};
                EnumCombo(ed, "Profile##sm", sm->profile, kProf, 5);
                DragF(ed, sm->profile == SplineMesh::Profile::Tube ? "Diameter##sm" : "Width##sm", sm->width, 0.02f, 0.01f, 1000.0f);
                if (sm->profile == SplineMesh::Profile::Road || sm->profile == SplineMesh::Profile::Rail)
                    DragF(ed, sm->profile == SplineMesh::Profile::Road ? "Thickness##sm" : "Height##sm", sm->height, 0.01f, 0.0f, 100.0f);
                if (sm->profile == SplineMesh::Profile::Tube) DragI(ed, "Sides##sm", sm->sides, 0.2f, 3, 64);
                if (sm->profile == SplineMesh::Profile::Custom) {
                    ImGui::TextDisabled("Points across (x) and up (y), left to right over the top:");
                    for (int i = 0; i < (int)sm->customProfile.size(); ++i) {
                        ImGui::PushID(i);
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                        DragV2(ed, "##cp", sm->customProfile[(std::size_t)i], 0.01f);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) { ed.PushUndo(); sm->customProfile.erase(sm->customProfile.begin() + i); ed.dirty = true; ImGui::PopID(); break; }
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton("Add Profile Point##sm")) {
                        ed.PushUndo();
                        sm->customProfile.push_back(sm->customProfile.empty() ? Vec2{0, 0} : sm->customProfile.back() + Vec2{0.5f, 0});
                        ed.dirty = true;
                    }
                    ImGui::SameLine();
                    Check(ed, "Closed Profile##sm", sm->closedProfile);
                }
                DragF(ed, "Section Spacing##sm", sm->segmentLength, 0.01f, 0.01f, 100.0f);
                Tip("Distance between cross-sections along the curve (smaller = smoother bends, more triangles).");
                DragF(ed, "UV Tile Length##sm", sm->uvTileLength, 0.05f, 0.01f, 1000.0f);
                Tip("The texture repeats every this many units along the length.");
            } else {
                static const char* kMeshes[] = {"Fence", "Cube", "Cylinder", "Barrel", "Crate", "Bench", "Pyramid", "Wedge", "Torus"};
                if (ImGui::BeginCombo("Mesh##sm", sm->sourceMesh.c_str())) {
                    for (const char* mname : kMeshes)
                        if (ImGui::Selectable(mname, sm->sourceMesh == mname)) { BeforeChange(ed, &sm->sourceMesh); sm->sourceMesh = mname; ed.dirty = true; }
                    ImGui::EndCombo();
                }
                TextField(ed, "Mesh File##sm", sm->sourceMesh);
                Tip("A built-in mesh name, or a .obj / .okaymesh file in the project.");
                static const char* kAxis[] = {"X", "Z"};
                int ai = sm->meshAxis == SplineMesh::Axis::X ? 0 : 1;
                if (ImGui::Combo("Along Axis##sm", &ai, kAxis, 2)) { BeforeChange(ed, &sm->meshAxis); sm->meshAxis = ai == 0 ? SplineMesh::Axis::X : SplineMesh::Axis::Z; ed.dirty = true; }
                Tip("Which axis of the mesh runs along the curve (the fence prop is long in X).");
                DragF(ed, "Spacing##sm", sm->spacing, 0.01f, 0.0f, 100.0f);
                DragV3(ed, "Mesh Scale##sm", sm->meshScale, 0.01f);
                Check(ed, "Stretch To Fit##sm", sm->stretchToFit);
                Tip("Stretch the copies a little so they fill the whole length.");
            }
            Check(ed, "Generate Collider##sm", sm->generateCollider);
            Tip("Keep a Mesh Collider on the object so players walk on it and bump into it.");
            if (auto* mr = go->GetComponent<MeshRenderer>()) {
                ImGui::TextDisabled("%d vertices, %d triangles%s", (int)mr->mesh.vertices.size(), mr->mesh.TriangleCount(),
                                    sm->mode == SplineMesh::Mode::Repeat ? (" in " + std::to_string(sm->Copies()) + " copies").c_str() : "");
                ImGui::TextDisabled("Colour, texture and shader: the Mesh Renderer.");
            }
            if (!Spline::Resolve(sm, sm->spline)) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "No Spline to build along.");
        }
    }
    if (auto* si = dynamic_cast<SplineInstancer*>(comp)) {
        if (header("Spline Instancer", si, &toRemove, true)) {
            ObjectCombo<Spline>(ed, "Spline##si", si->spline, "(this object)");
            ImGui::SeparatorText("What");
            ObjectCombo<Transform>(ed, "Copy Object##si", si->templateName, "(none)");
            Tip("Place copies of this object (a lamp post you built). Wins over the prefab and mesh below.");
            TextField(ed, "Prefab##si", si->prefabPath);
            Tip("Or a .okayprefab file.");
            static const char* kItems[] = {"Tree", "Pine", "PalmTree", "Bush", "Rock", "StreetLamp", "Lantern", "Barrel", "Crate", "Signpost", "Crystal", "Cube", "Sphere", "Cylinder"};
            if (ImGui::BeginCombo("Mesh##si", si->meshName.c_str())) {
                for (const char* mname : kItems)
                    if (ImGui::Selectable(mname, si->meshName == mname)) { BeforeChange(ed, &si->meshName); si->meshName = mname; ed.dirty = true; }
                ImGui::EndCombo();
            }
            Tip("Or a built-in mesh.");
            ImGui::SeparatorText("Where");
            DragF(ed, "Spacing##si", si->spacing, 0.05f, 0.05f, 1000.0f);
            DragF(ed, "Start Offset##si", si->startOffset, 0.05f, 0.0f, 10000.0f);
            DragF(ed, "Side Offset##si", si->sideOffset, 0.05f, -1000.0f, 1000.0f);
            Tip("Across the curve, towards the +X side of an object riding it (negative: the other side).");
            Check(ed, "Both Sides##si", si->bothSides);
            static const char* kAlign[] = {"Upright", "Follow Curve"};
            EnumCombo(ed, "Align##si", si->align, kAlign, 2);
            Tip("Upright: stand straight up, facing along the curve. Follow Curve: tilt and roll with it.");
            DragV3(ed, "Rotation##si", si->rotationOffset, 1.0f, "%.0f");
            ImGui::SeparatorText("Variation");
            DragF(ed, "Jitter##si", si->jitter, 0.01f, 0.0f, 100.0f);
            Tip("Random offset along and across the curve.");
            DragF(ed, "Turn Jitter##si", si->rotationJitter, 0.5f, 0.0f, 180.0f, "%.0f deg");
            DragF(ed, "Scale Min##si", si->scaleMin, 0.01f, 0.01f, 100.0f);
            DragF(ed, "Scale Max##si", si->scaleMax, 0.01f, 0.01f, 100.0f);
            int seed = (int)si->seed;
            if (ImGui::DragInt("Seed##si", &seed, 1.0f, 0, 1 << 30)) { BeforeChange(ed, &si->seed); si->seed = (std::uint32_t)std::max(0, seed); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("New##siseed")) { ed.PushUndo(); si->seed = (std::uint32_t)(std::rand() & 0x7fffffff); ed.dirty = true; }
            DragI(ed, "Max Items##si", si->maxCount, 1.0f, 0, 10000);
            ImGui::TextDisabled("%d items (placed again on load, not saved)", si->Count());
            if (ImGui::SmallButton("Rebuild##si")) { si->Clear(); si->Rebuild(); }
            Tip("Make the items again (after editing the object they copy).");
        }
    }
    if (auto* lr = dynamic_cast<LineRenderer*>(comp)) {
        if (header("Line Renderer", lr, &toRemove, true)) {
            ObjectCombo<Spline>(ed, "Draw Spline##lr", lr->spline, "(no: the points below)");
            if (!lr->spline.empty()) {
                DragI(ed, "Samples##lr", lr->splineSamples, 0.5f, 2, 4096);
            } else {
                Check(ed, "World Space##lr", lr->useWorldSpace);
                Tip("Off: the points move with the object.");
                ImGui::SameLine();
                Check(ed, "Loop##lr", lr->loop);
                for (int i = 0; i < lr->PositionCount(); ++i) {
                    ImGui::PushID(i);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                    DragV3(ed, "##p", lr->positions[(std::size_t)i], 0.05f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) { ed.PushUndo(); lr->positions.erase(lr->positions.begin() + i); ed.dirty = true; ImGui::PopID(); break; }
                    ImGui::PopID();
                }
                if (ImGui::SmallButton("Add Point##lr")) {
                    ed.PushUndo();
                    const Vec3 last = lr->positions.empty() ? Vec3{0, 0, 0} : lr->positions.back();
                    lr->positions.push_back(last + Vec3{0, 0, 1});
                    ed.dirty = true;
                }
                ImGui::TextDisabled("Scripts: line_set_points(...) / LineRenderer.SetPositions(...).");
            }
            DragF(ed, "Width##lr", lr->width, 0.005f, 0.0f, 100.0f, "%.3f");
            CurveRows(ed, "lrw", lr->widthCurve);
            ColorField(ed, "Colour##lr", lr->color);
            GradientRows(ed, "lrg", lr->colorGradient);
            static const char* kAlign[] = {"Face Camera", "Flat (object up)"};
            EnumCombo(ed, "Alignment##lr", lr->alignment, kAlign, 2);
            DragI(ed, "Corner Vertices##lr", lr->cornerVertices, 0.1f, 0, 32);
            Tip("Round each bend with this many extra steps (0 = mitred).");
            DragI(ed, "End Cap Vertices##lr", lr->capVertices, 0.1f, 0, 32);
            Tip("Round both ends with this many steps (0 = square).");
            TextField(ed, "Texture##lr", lr->texture);
            static const char* kTex[] = {"Stretch", "Tile"};
            EnumCombo(ed, "Texture Mode##lr", lr->textureMode, kTex, 2);
            if (lr->textureMode == LineTextureMode::Tile) DragF(ed, "Tile Length##lr", lr->tileLength, 0.01f, 0.01f, 1000.0f);
        }
    }
    if (auto* tr = dynamic_cast<TrailRenderer*>(comp)) {
        if (header("Trail Renderer", tr, &toRemove, true)) {
            DragF(ed, "Time##tr", tr->time, 0.01f, 0.01f, 60.0f, "%.2f s");
            Tip("How long a point of the trail lives.");
            DragF(ed, "Min Vertex Distance##tr", tr->minVertexDistance, 0.005f, 0.001f, 100.0f, "%.3f");
            DragF(ed, "Width##tr", tr->width, 0.005f, 0.0f, 100.0f, "%.3f");
            CurveRows(ed, "trw", tr->widthCurve);
            ColorField(ed, "Colour##tr", tr->color);
            GradientRows(ed, "trg", tr->colorGradient);
            Check(ed, "Emitting##tr", tr->emitting);
            static const char* kAlign[] = {"Face Camera", "Flat (object up)"};
            EnumCombo(ed, "Alignment##tr", tr->alignment, kAlign, 2);
            DragF(ed, "Teleport Distance##tr", tr->teleportDistance, 0.05f, 0.0f, 10000.0f);
            Tip("A jump longer than this in one frame starts a new trail instead of a streak (0 = never).");
            TextField(ed, "Texture##tr", tr->texture);
            static const char* kTex[] = {"Stretch", "Tile"};
            EnumCombo(ed, "Texture Mode##tr", tr->textureMode, kTex, 2);
            if (tr->textureMode == LineTextureMode::Tile) DragF(ed, "Tile Length##tr", tr->tileLength, 0.01f, 0.01f, 1000.0f);
            ImGui::TextDisabled("%d points. Move the object to preview; scripts: trail_clear().", tr->PointCount());
            if (ImGui::SmallButton("Clear##tr")) tr->Clear();
        }
    }
}

void SplineAddComponentItems(EditorState& ed, GameObject* go, const std::function<bool(bool, const char*)>& item, bool rendering) {
    if (!go) return;
    if (rendering) {
        if (item(!go->GetComponent<LineRenderer>(), "Line Renderer (lasers, ropes, arcs)")) {
            ed.PushUndo();
            auto* lr = go->AddComponent<LineRenderer>();
            const Vec3 p = go->transform->Position();
            lr->positions = {p, p + go->transform->Forward() * 4.0f};
            ed.dirty = true;
        }
        if (item(!go->GetComponent<TrailRenderer>(), "Trail Renderer (ribbon behind it)")) {
            ed.PushUndo(); go->AddComponent<TrailRenderer>(); ed.dirty = true;
        }
        if (item(!go->GetComponent<SplineMesh>(), "Spline Mesh (road, pipe, fence)")) {
            ed.PushUndo();
            EnsureSpline(go);
            if (!go->GetComponent<MeshRenderer>()) go->AddComponent<MeshRenderer>(Mesh{})->color = Color::FromBytes(78, 80, 88);
            go->AddComponent<SplineMesh>()->Rebuild();
            ed.view3D = true; ed.dirty = true;
        }
        return;
    }
    if (item(!go->GetComponent<Spline>(), "Spline (curve with handles)")) {
        ed.PushUndo(); DefaultPoints(go->AddComponent<Spline>()); ed.dirty = true;
    }
    if (item(!go->GetComponent<SplineFollower>(), "Spline Follower (move along a spline)")) {
        ed.PushUndo();
        auto* f = go->AddComponent<SplineFollower>();
        for (const auto& up : ed.scene().Objects())   // the scene's first spline that isn't this object's
            if (up && up.get() != go && up->GetComponent<Spline>()) { f->spline = up->name; break; }
        ed.dirty = true;
    }
    if (item(!go->GetComponent<SplineInstancer>(), "Spline Instancer (place objects along a spline)")) {
        ed.PushUndo();
        EnsureSpline(go);
        go->AddComponent<SplineInstancer>()->Rebuild();
        ed.dirty = true;
    }
}

void CreateSplineFromMenu(EditorState& ed) {
    // In front of the camera: across its view, at the ground under its target.
    const float yaw = ed.camYaw * Mathf::Deg2Rad;
    const Vec3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Vec3 right = Vec3::Cross(Vec3{0, 1, 0}, fwd);
    Vec3 at = ed.camTarget;
    Vec3 ground;
    if (GroundBelow(ed.scene(), at, nullptr, ground)) at = ground;
    GameObject* go = ed.CreateEmpty("Spline");
    go->transform->localPosition = at;
    auto* sp = go->AddComponent<Spline>();
    const float w = std::max(2.0f, ed.camDist * 0.3f);
    for (const Vec3& p : {right * -w, fwd * (w * 0.75f), right * w}) sp->AddPoint(p);
    ed.Select(go);
    s_pick.obj = go; s_pick.objName = go->name; s_pick.point = 1;
    ed.view3D = true;
    ed.dirty = true;
    ConsoleLog("Created a Spline: drag its points, click the curve to add more");
}

// ---- Edit mode ------------------------------------------------------------------

void SplineEditTick(EditorState& ed, float dt) {
    Scene& sc = ed.scene();
    // Collect first: rebuilding an instancer adds objects to the scene.
    std::vector<SplineMesh*> meshes;
    std::vector<SplineInstancer*> instancers;
    std::vector<TrailRenderer*> trails;
    for (const auto& up : sc.Objects()) {
        if (!up) continue;
        for (auto* m : up->GetComponents<SplineMesh>()) meshes.push_back(m);
        for (auto* i : up->GetComponents<SplineInstancer>()) instancers.push_back(i);
        if (up->active)
            for (auto* t : up->GetComponents<TrailRenderer>()) if (t->enabled) trails.push_back(t);
    }
    for (SplineMesh* m : meshes) m->RebuildIfChanged();
    for (SplineInstancer* i : instancers) i->RebuildIfChanged();
    for (TrailRenderer* t : trails) t->Update(dt);
}

// ---- Dev hooks ------------------------------------------------------------------

namespace {

GameObject* MakeMeshObject(Scene& sc, const char* name, const char* mesh, Vec3 pos, Vec3 scale, Color col) {
    GameObject* g = sc.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->transform->localScale = scale;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::FromName(mesh);
    mr->color = col;
    return g;
}

void BuildDemo(Scene& sc) {
    // A road across the ground with a centre line and street lamps along one side.
    GameObject* road = sc.CreateGameObject("Road");
    auto* rs = road->AddComponent<Spline>();
    for (const Vec3& p : {Vec3{-16, 0, 2}, Vec3{-8, 0, 7}, Vec3{0, 0, 9}, Vec3{8, 0, 6}, Vec3{14, 0, 12}, Vec3{12, 0, 18}})
        rs->AddPoint(p);
    road->AddComponent<MeshRenderer>(Mesh{})->color = Color::FromBytes(70, 72, 80);
    auto* rm = road->AddComponent<SplineMesh>();
    rm->profile = SplineMesh::Profile::Road; rm->width = 4.0f; rm->height = 0.3f; rm->generateCollider = true;
    rm->Rebuild();
    GameObject* line = sc.CreateGameObject("Road Centre Line");
    line->AddComponent<MeshRenderer>(Mesh{})->color = Color::FromBytes(245, 220, 90);
    line->GetComponent<MeshRenderer>()->unlit = true;
    auto* lm = line->AddComponent<SplineMesh>();
    lm->spline = "Road"; lm->profile = SplineMesh::Profile::Custom;
    lm->customProfile = {{-0.09f, 0.04f}, {0.09f, 0.04f}};
    lm->Rebuild();
    GameObject* lamps = sc.CreateGameObject("Street Lamps");
    auto* li = lamps->AddComponent<SplineInstancer>();
    li->spline = "Road"; li->meshName = "StreetLamp"; li->spacing = 7.0f; li->startOffset = 2.0f;
    li->sideOffset = 2.8f; li->rotationOffset = {0, 180, 0};
    li->Rebuild();
    // A fence repeated along a curve behind the field.
    GameObject* fence = sc.CreateGameObject("Fence");
    auto* fs = fence->AddComponent<Spline>();
    for (const Vec3& p : {Vec3{-17, 0, -4}, Vec3{-10, 0, -9}, Vec3{-1, 0, -12}, Vec3{8, 0, -10}, Vec3{15, 0, -5}})
        fs->AddPoint(p);
    fence->AddComponent<MeshRenderer>(Mesh{});
    auto* fm = fence->AddComponent<SplineMesh>();
    fm->mode = SplineMesh::Mode::Repeat; fm->sourceMesh = "Fence"; fm->meshAxis = SplineMesh::Axis::X;
    fm->Rebuild();
    // A platform riding a looped rail (the rail drawn as a thin pipe).
    GameObject* rail = sc.CreateGameObject("Platform Rail");
    auto* ps = rail->AddComponent<Spline>();
    ps->closed = true;
    for (const Vec3& p : {Vec3{3, 1.5f, 3}, Vec3{7, 2.5f, 4}, Vec3{8, 1.5f, -1}, Vec3{4, 0.8f, -3}})
        ps->AddPoint(p);
    rail->AddComponent<MeshRenderer>(Mesh{})->color = Color::FromBytes(150, 155, 165);
    auto* pm = rail->AddComponent<SplineMesh>();
    pm->profile = SplineMesh::Profile::Tube; pm->width = 0.12f; pm->sides = 8;
    pm->Rebuild();
    GameObject* plat = MakeMeshObject(sc, "Platform", "Cube", {3, 1.5f, 3}, {2.2f, 0.25f, 2.2f}, Color::FromBytes(200, 120, 60));
    auto* pf = plat->AddComponent<SplineFollower>();
    pf->spline = "Platform Rail"; pf->speed = 2.5f; pf->orient = false; pf->offset = {0, -0.2f, 0};
    plat->AddComponent<BoxCollider3D>();
    // A laser from an emitter to a crate, with a soft glow, and an aiming arc.
    MakeMeshObject(sc, "Laser Emitter", "Cylinder", {-5, 1.0f, 2}, {0.3f, 0.6f, 0.3f}, Color::FromBytes(60, 60, 70));
    const Vec3 from{-5, 1.25f, 2}, to{3, 0.6f, -2.6f};
    GameObject* glow = sc.CreateGameObject("Laser Glow");
    auto* gl = glow->AddComponent<LineRenderer>();
    gl->positions = {from, to}; gl->width = 0.35f; gl->color = Color{1.0f, 0.15f, 0.1f, 0.3f}; gl->capVertices = 6;
    GameObject* laser = sc.CreateGameObject("Laser");
    auto* lr = laser->AddComponent<LineRenderer>();
    lr->positions = {from, to}; lr->width = 0.08f; lr->color = Color{1.0f, 0.55f, 0.45f, 1.0f}; lr->capVertices = 4;
    GameObject* arc = sc.CreateGameObject("Aim Arc");
    auto* al = arc->AddComponent<LineRenderer>();
    for (int i = 0; i <= 24; ++i) {
        const float t = (float)i / 24.0f;
        al->positions.push_back({-2.0f + 7.0f * t, 0.05f + 4.0f * t * (1.0f - t), -6.0f + 2.0f * t});
    }
    al->width = 0.12f; al->color = Color{0.4f, 0.9f, 1.0f, 1.0f};
    al->colorGradient.SetTwo(Color{1, 1, 1, 1}, Color{1, 1, 1, 0.15f});
    al->widthCurve.SetLinear(1.0f, 0.4f);
    al->cornerVertices = 2;
    // A comet looping round the scene, leaving a trail.
    GameObject* path = sc.CreateGameObject("Comet Path");
    auto* cs = path->AddComponent<Spline>();
    cs->closed = true;
    for (const Vec3& p : {Vec3{-3, 2.0f, 4}, Vec3{0, 3.0f, 7}, Vec3{3, 2.0f, 4}, Vec3{0, 1.2f, 1.5f}})
        cs->AddPoint(p);
    GameObject* comet = MakeMeshObject(sc, "Comet", "Sphere", {-3, 2, 4}, {0.5f, 0.5f, 0.5f}, Color::FromBytes(255, 230, 160));
    comet->GetComponent<MeshRenderer>()->emissive = Color::FromBytes(255, 180, 80);
    auto* cf = comet->AddComponent<SplineFollower>();
    cf->spline = "Comet Path"; cf->speed = 3.5f;
    auto* ct = comet->AddComponent<TrailRenderer>();
    ct->time = 1.2f; ct->width = 0.35f; ct->minVertexDistance = 0.05f;
    ct->colorGradient.Clear();
    ct->colorGradient.Add(0.0f, Color{1.0f, 0.95f, 0.6f, 1.0f});
    ct->colorGradient.Add(0.4f, Color{1.0f, 0.5f, 0.15f, 0.8f});
    ct->colorGradient.Add(1.0f, Color{0.8f, 0.1f, 0.05f, 0.0f});
}

} // namespace

void SplineDevHooks(EditorState& ed) {
    static int s_frame = 0;
    if (s_frame < 100000) ++s_frame;
    if (const char* d = std::getenv("OKAY_UI_SPLINE"); d && s_frame == 33 && !ed.isPlaying()) {
        BuildDemo(ed.scene());
        ed.view3D = true;
        ConsoleLog("Spline demo: road, lamps, fence, platform rail, laser, aim arc, comet");
    }
    if (const char* c = std::getenv("OKAY_UI_SPLINECAM"); c && s_frame >= 34 && s_frame <= 36) {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, dist = 10;
        if (std::sscanf(c, "%f,%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch, &dist) == 6) {
            ed.camTarget = {x, y, z}; ed.camYaw = yaw; ed.camPitch = pitch; ed.camDist = dist;
        }
    }
    if (const char* s = std::getenv("OKAY_UI_SPLINESEL"); s && s_frame == 45) {
        std::string name = s;
        int point = -1;
        const std::size_t colon = name.rfind(':');
        if (colon != std::string::npos) { point = std::atoi(name.c_str() + colon + 1); name.resize(colon); }
        if (GameObject* g = ed.scene().Find(name)) {
            ed.Select(g);
            s_pick.obj = g; s_pick.objName = g->name; s_pick.point = point;
        }
    }
}

} // namespace oked
