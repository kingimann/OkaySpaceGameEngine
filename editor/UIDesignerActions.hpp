#pragma once
// Canvas actions of the UI designer that change the scene, without ImGui, so a test can check that each one is a
// single undo step. The window and main.cpp call these.
#include "EditorState.hpp"
#include "UIDesignerModel.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Scene/UILayoutTools.hpp"
#include "okay/Components/UIUserStyle.hpp"

namespace oked::uid {

/// Arrow keys on the canvas: move every selected widget (skipping those `isLocked` says no to) by 1 pixel, or by the
/// grid step with Shift. One undo step. Returns how many widgets moved.
template <class LockedFn>
inline int NudgeSelection(okay::editor::EditorState& ed, int ax, int ay, bool shift, float gridStep, LockedFn isLocked) {
    float dx = 0, dy = 0;
    if (!Nudge(ax, ay, shift, gridStep, dx, dy)) return 0;
    std::vector<okay::GameObject*> targets;
    for (okay::GameObject* g : ed.MultiSelection()) {
        if (!g || isLocked(g)) continue;
        okay::UIRect r = okay::GetUIRect(g);
        if (r.valid && r.position) targets.push_back(g);
    }
    if (targets.empty()) return 0;
    ed.PushUndo("Nudge UI");
    for (okay::GameObject* g : targets) { okay::UIRect r = okay::GetUIRect(g); r.position->x += dx; r.position->y += dy; }
    ed.dirty = true;
    return (int)targets.size();
}

// ---- selection tools -----------------------------------------------------------------------------------------------
inline int WidgetKind(okay::GameObject* g) {
    if (g->GetComponent<okay::UIButton>()) return 1;
    if (g->GetComponent<okay::UIPanel>()) return 2;
    if (g->GetComponent<okay::TextRenderer>()) return 3;
    if (g->GetComponent<okay::UIImage>()) return 4;
    return 0;
}
template <class LockedFn>
inline std::vector<Item> SceneItems(okay::editor::EditorState& ed, LockedFn isLocked) {
    std::vector<Item> v;
    for (const auto& up : ed.scene().Objects()) {
        okay::GameObject* g = up.get();
        if (!g || !okay::GetUIRect(g).valid) continue;
        v.push_back({(int)v.size(), WidgetKind(g), isLocked(g), !g->active || okay::UIHidden(g)});
    }
    return v;
}
inline std::vector<okay::GameObject*> SceneWidgets(okay::editor::EditorState& ed) {
    std::vector<okay::GameObject*> w;
    for (const auto& up : ed.scene().Objects()) if (up && okay::GetUIRect(up.get()).valid) w.push_back(up.get());
    return w;
}
enum class SelOp { All, Invert, SameKind };
/// Ctrl+A / invert / select the same kind. Returns the new selection size. (Selection is not an undo step.)
template <class LockedFn>
inline int SelectTool(okay::editor::EditorState& ed, SelOp op, LockedFn isLocked) {
    const auto items = SceneItems(ed, isLocked); const auto widgets = SceneWidgets(ed);
    std::vector<int> cur;
    for (okay::GameObject* s : ed.MultiSelection()) for (size_t i = 0; i < widgets.size(); ++i) if (widgets[i] == s) cur.push_back((int)i);
    const std::vector<int> ids = op == SelOp::All ? SelectAllIds(items) : op == SelOp::Invert ? InvertIds(items, cur) : SameTypeIds(items, cur);
    ed.Select(nullptr);
    for (int id : ids) { if (!ed.selected()) ed.Select(widgets[id]); else ed.ToggleSelect(widgets[id]); }
    return (int)ids.size();
}

// ---- layout tools on the selection (one undo step each) ------------------------------------------------------------
struct Sel { std::vector<okay::GameObject*> go; std::vector<Rect> r; size_t key = 0; };
inline Sel CurrentSel(okay::editor::EditorState& ed) {
    Sel s;
    for (okay::GameObject* g : ed.MultiSelection()) {
        okay::Vec2 o, sz;
        if (g && okay::UILayout::LocalRect(g, o, sz)) { if (g == ed.selected()) s.key = s.go.size(); s.go.push_back(g); s.r.push_back({o.x, o.y, sz.x, sz.y}); }
    }
    return s;
}
inline int Commit(okay::editor::EditorState& ed, const Sel& s, const std::vector<Rect>& out, const char* undoName) {
    int n = 0;
    for (size_t i = 0; i < s.go.size() && i < out.size(); ++i)
        if (out[i].x != s.r[i].x || out[i].y != s.r[i].y || out[i].w != s.r[i].w || out[i].h != s.r[i].h) { if (!n) ed.PushUndo(undoName); ++n;
            if (out[i].w != s.r[i].w || out[i].h != s.r[i].h) okay::UILayout::SetSizeKeepOrigin(s.go[i], {out[i].w, out[i].h});
            okay::UILayout::SetLocalOrigin(s.go[i], {out[i].x, out[i].y}); }
    if (n) ed.dirty = true;
    return n;
}
/// Align to the primary (key) widget when two or more are selected; to the screen when only one is.
inline int AlignSelection(okay::editor::EditorState& ed, AlignMode m) {
    Sel s = CurrentSel(ed); if (s.go.empty()) return 0;
    if (s.go.size() == 1) return Commit(ed, s, AlignTo(s.r, m, {0, 0, okay::UICanvas::Width(), okay::UICanvas::Height()}), "Align UI");
    return Commit(ed, s, AlignToKey(s.r, s.key, m), "Align UI to key");
}
inline int DistributeSelection(okay::editor::EditorState& ed, bool horizontal, float gap /* < 0: equal spacing across the span */) {
    Sel s = CurrentSel(ed); if (s.go.size() < 2) return 0;
    return Commit(ed, s, gap < 0 ? Distribute(s.r, horizontal) : DistributeGap(s.r, horizontal, gap), "Distribute UI");
}
inline int MatchSelection(okay::editor::EditorState& ed, bool w, bool h) { Sel s = CurrentSel(ed); return s.go.size() < 2 ? 0 : Commit(ed, s, MatchToKey(s.r, s.key, w, h), "Match UI size to key"); }
inline int TidySelection(okay::editor::EditorState& ed, int cols, float gap) { Sel s = CurrentSel(ed); return s.go.size() < 2 ? 0 : Commit(ed, s, TidyGrid(s.r, cols, gap), "Tidy UI grid"); }
inline int FlipSelection(okay::editor::EditorState& ed, bool horizontal) { Sel s = CurrentSel(ed); return s.go.size() < 2 ? 0 : Commit(ed, s, Flip(s.r, horizontal), "Flip UI"); }

// ---- find and replace: colours and styles ----------------------------------------------------------------------------
inline bool ColorsClose(const okay::Color& a, const okay::Color& b, float tol) {
    return std::fabs(a.r - b.r) <= tol && std::fabs(a.g - b.g) <= tol && std::fabs(a.b - b.b) <= tol && std::fabs(a.a - b.a) <= tol;
}
/// Every fill and text colour of every UI widget that is within `tol` of `from` becomes `to`. One undo step. Returns how many.
inline int ReplaceColor(okay::editor::EditorState& ed, const okay::Color& from, const okay::Color& to, float tol) {
    int n = 0; bool pushed = false;
    auto swap = [&](okay::Color& c) { if (ColorsClose(c, from, tol)) { if (!pushed) { ed.PushUndo("Replace UI colour"); pushed = true; } c = to; ++n; } };
    for (const auto& up : ed.scene().Objects()) {
        if (!up) continue;
        if (auto* b = up->GetComponent<okay::UIButton>()) { swap(b->color); swap(b->textColor); swap(b->borderColor); }
        if (auto* p = up->GetComponent<okay::UIPanel>()) { swap(p->color); swap(p->borderColor); }
        if (auto* t = up->GetComponent<okay::TextRenderer>()) if (t->screenSpace) swap(t->color);
    }
    if (n) ed.dirty = true;
    return n;
}
/// Point every widget that uses shared style `from` at style `to`, and apply it. One undo step. Returns how many.
inline int ReplaceStyle(okay::editor::EditorState& ed, const std::string& from, const std::string& to) {
    int n = 0;
    for (const auto& up : ed.scene().Objects()) {
        auto* v = up ? up->GetComponent<okay::UIStyleVariant>() : nullptr;
        if (v && !from.empty() && v->userStyle == from) { if (!n) ed.PushUndo("Replace UI style"); v->userStyle = to; ++n; }
    }
    if (n) { okay::ApplyUserStyles(ed.scene()); ed.dirty = true; }
    return n;
}

} // namespace oked::uid
