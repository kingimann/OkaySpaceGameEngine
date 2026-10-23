#pragma once
// Layout tools for scene UI (the editor's UI view and inspector): wrap widgets in a
// panel, fit a panel around its children, match sizes, stack a selection into a
// row or column, and place a new HUD piece where it doesn't cover another one.
// All work in a widget's "local" frame: its top-left inside its UI parent (or the
// canvas), at design size, so they behave the same at any window size.
#include "okay/Components/UIElement.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <vector>

namespace okay { namespace UILayout {

// The frame a widget is laid out in: its UI parent's size, else the canvas.
inline Vec2 RefSize(GameObject* go) {
    if (GameObject* p = OwningUIParent(go)) {
        if (GetUIRect(p).valid) return UIResolveSize(p, UICanvas::Width(), UICanvas::Height());
    }
    return Vec2{UICanvas::Width(), UICanvas::Height()};
}

// Top-left + size of a widget inside its reference frame. False for non-UI objects.
inline bool LocalRect(GameObject* go, Vec2& o, Vec2& sz) {
    UIRect r = GetUIRect(go);
    if (!r.valid || !r.position) return false;
    const Vec2 ref = RefSize(go);
    ResolveAnchorRect(r.anchor, *r.position, r.size, ref.x, ref.y, o, sz);
    return true;
}

// Move a widget so its top-left sits at `o` in its reference frame (anchor kept).
inline void SetLocalOrigin(GameObject* go, const Vec2& o) {
    UIRect r = GetUIRect(go);
    if (!r.valid || !r.position || AnchorIsStretch(r.anchor)) return;
    const Vec2 ref = RefSize(go);
    const Vec2 base = ResolveAnchor(r.anchor, Vec2{0, 0}, r.size, ref.x, ref.y);
    *r.position = Vec2{o.x - base.x, o.y - base.y};
}

// Set a widget's size, keeping its top-left where it is.
inline void SetSizeKeepOrigin(GameObject* go, const Vec2& sz) {
    UIRect r = GetUIRect(go);
    if (!r.valid || !r.sizePtr || AnchorIsStretch(r.anchor)) return;
    Vec2 o, cur;
    if (!LocalRect(go, o, cur)) return;
    *r.sizePtr = sz;
    SetLocalOrigin(go, o);
}

// The widgets' shared UI parent (as a Transform), or false if they don't share one.
inline bool CommonParent(const std::vector<GameObject*>& items, Transform*& parent) {
    parent = nullptr;
    bool first = true;
    for (GameObject* g : items) {
        if (!g || !g->transform) return false;
        Transform* p = g->transform->Parent();
        if (first) { parent = p; first = false; }
        else if (p != parent) return false;
    }
    return !first;
}

// Move widgets under a new card panel that fits around them (+ padding). They
// must share a parent; they keep their place on screen. Returns the panel.
inline GameObject* WrapInPanel(Scene& scene, const std::vector<GameObject*>& items, float pad = 16.0f) {
    Transform* parent = nullptr;
    if (items.empty() || !CommonParent(items, parent)) return nullptr;
    Vec2 lo{1e9f, 1e9f}, hi{-1e9f, -1e9f};
    std::vector<Vec2> origins;
    for (GameObject* g : items) {
        Vec2 o, sz;
        if (!LocalRect(g, o, sz)) return nullptr;
        origins.push_back(o);
        lo.x = std::min(lo.x, o.x); lo.y = std::min(lo.y, o.y);
        hi.x = std::max(hi.x, o.x + sz.x); hi.y = std::max(hi.y, o.y + sz.y);
    }
    GameObject* panel = scene.CreateGameObject("Panel");
    auto* p = panel->AddComponent<UIPanel>();
    p->anchor = UIAnchor::TopLeft;
    p->size = {hi.x - lo.x + pad * 2.0f, hi.y - lo.y + pad * 2.0f};
    if (parent) panel->transform->SetParent(parent, false);
    SetLocalOrigin(panel, Vec2{lo.x - pad, lo.y - pad});
    for (std::size_t i = 0; i < items.size(); ++i) {
        items[i]->transform->SetParent(panel->transform, false);
        SetLocalOrigin(items[i], Vec2{origins[i].x - (lo.x - pad), origins[i].y - (lo.y - pad)});
    }
    return panel;
}

// Resize a panel (or any sized widget) to wrap its UI children (+ padding), moving it
// so the children stay where they are on screen. Returns false with no children.
inline bool FitToChildren(GameObject* box, float pad = 16.0f) {
    UIRect br = GetUIRect(box);
    if (!br.valid || !br.sizePtr || !br.position || AnchorIsStretch(br.anchor)) return false;
    std::vector<GameObject*> kids;
    for (Transform* c : box->transform->Children())
        if (c && c->gameObject && GetUIRect(c->gameObject).valid) kids.push_back(c->gameObject);
    if (kids.empty()) return false;
    Vec2 lo{1e9f, 1e9f}, hi{-1e9f, -1e9f};
    std::vector<Vec2> origins;
    for (GameObject* k : kids) {
        Vec2 o, sz; LocalRect(k, o, sz);
        origins.push_back(o);
        lo.x = std::min(lo.x, o.x); lo.y = std::min(lo.y, o.y);
        hi.x = std::max(hi.x, o.x + sz.x); hi.y = std::max(hi.y, o.y + sz.y);
    }
    Vec2 bo, bsz; LocalRect(box, bo, bsz);
    const Vec2 shift{lo.x - pad, lo.y - pad};   // where the box's new top-left lands, in its own frame
    *br.sizePtr = Vec2{hi.x - lo.x + pad * 2.0f, hi.y - lo.y + pad * 2.0f};
    SetLocalOrigin(box, Vec2{bo.x + shift.x, bo.y + shift.y});
    for (std::size_t i = 0; i < kids.size(); ++i)
        SetLocalOrigin(kids[i], Vec2{origins[i].x - shift.x, origins[i].y - shift.y});
    return true;
}

// Give every widget the reference widget's width and/or height.
inline int MatchSize(const std::vector<GameObject*>& items, GameObject* ref, bool w, bool h) {
    UIRect rr = GetUIRect(ref);
    if (!rr.valid) return 0;
    int n = 0;
    for (GameObject* g : items) {
        if (g == ref) continue;
        UIRect r = GetUIRect(g);
        if (!r.valid || !r.sizePtr) continue;
        Vec2 sz = r.size;
        if (w) sz.x = rr.size.x;
        if (h) sz.y = rr.size.y;
        SetSizeKeepOrigin(g, sz);
        ++n;
    }
    return n;
}

// Line widgets up in a column (or row) `spacing` apart, in their current top-to-bottom
// (left-to-right) order, starting where the first one is. They must share a parent.
inline bool Stack(std::vector<GameObject*> items, bool vertical, float spacing = 12.0f) {
    Transform* parent = nullptr;
    if (items.size() < 2 || !CommonParent(items, parent)) return false;
    struct It { GameObject* g; Vec2 o, sz; };
    std::vector<It> v;
    for (GameObject* g : items) { It it{g, {}, {}}; if (!LocalRect(g, it.o, it.sz)) return false; v.push_back(it); }
    std::stable_sort(v.begin(), v.end(), [&](const It& a, const It& b) { return vertical ? a.o.y < b.o.y : a.o.x < b.o.x; });
    float at = vertical ? v[0].o.y : v[0].o.x;
    for (const It& it : v) {
        SetLocalOrigin(it.g, vertical ? Vec2{v[0].o.x, at} : Vec2{at, v[0].o.y});
        at += (vertical ? it.sz.y : it.sz.x) + spacing;
    }
    return true;
}

// Screen rect of a widget at the canvas size (design pixels), for overlap tests.
inline bool CanvasRect(GameObject* go, Vec2& o, Vec2& sz) {
    return GetUIScreenRect(go, UICanvas::Width(), UICanvas::Height(), o, sz);
}

// A HUD piece (anchored to a top or bottom edge) that would cover another visible
// widget on the canvas moves away from its edge until it doesn't, so two HUD screens
// added to one scene don't sit on top of each other. Full-screen and centred widgets
// (backdrops, menus) are ignored. Returns how far it moved (design px).
inline float AvoidOverlap(Scene& scene, GameObject* piece, float gap = 12.0f) {
    UIRect r = GetUIRect(piece);
    if (!r.valid || !r.position || AnchorIsStretch(r.anchor)) return 0.0f;
    const int row = (int)r.anchor / 3;   // 0 top, 1 middle, 2 bottom
    if (row == 1) return 0.0f;
    const float W = UICanvas::Width(), H = UICanvas::Height();
    auto others = [&](std::vector<std::pair<Vec2, Vec2>>& out) {
        out.clear();
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (!g || g == piece || !g->active || g->IsSelfOrDescendantOf(piece)) continue;
            if (piece->IsSelfOrDescendantOf(g)) continue;
            UIRect gr = GetUIRect(g);
            if (!gr.valid || GetUIRect(g).anchor == UIAnchor::Center) continue;
            if (OwningUIParent(g)) continue;   // compare top-level pieces only
            Vec2 o, sz;
            if (!CanvasRect(g, o, sz)) continue;
            if (sz.x >= W * 0.9f && sz.y >= H * 0.9f) continue;   // backdrops
            out.push_back({o, sz});
        }
    };
    std::vector<std::pair<Vec2, Vec2>> rects;
    others(rects);
    float moved = 0.0f;
    for (int iter = 0; iter < 12; ++iter) {
        Vec2 o, sz;
        if (!CanvasRect(piece, o, sz)) break;
        float push = 0.0f;
        for (const auto& rc : rects) {
            const Vec2& qo = rc.first; const Vec2& qs = rc.second;
            const bool hit = o.x < qo.x + qs.x && qo.x < o.x + sz.x && o.y < qo.y + qs.y && qo.y < o.y + sz.y;
            if (!hit) continue;
            push = std::max(push, row == 0 ? (qo.y + qs.y + gap) - o.y : (o.y + sz.y) - (qo.y - gap));
        }
        if (push <= 0.0f) break;
        float k = UIScaleFor(piece, W, H);   // rects are in screen px; the offset is design px
        if (k <= 1e-4f) k = 1.0f;
        r.position->y += (row == 0 ? push : -push) / k;
        moved += push / k;
    }
    return moved;
}

} } // namespace okay::UILayout
