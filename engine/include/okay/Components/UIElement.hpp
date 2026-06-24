#pragma once
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIRating.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Math/Vec2.hpp"
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cstddef>

namespace okay {

/// A uniform view of whatever screen-space UI widget lives on a GameObject: its
/// anchor, its editable pixel offset, and its size. Every UI component
/// (button, panel, image, slider, toggle, progress bar, screen-space text)
/// exposes the same three things differently; this collapses them into one
/// handle so tools — the editor's click-to-select and drag, the EventSystem's
/// pointer raycast — can work on any widget without a type switch at each site.
struct UIRect {
    bool      valid = false;
    UIAnchor  anchor = UIAnchor::TopLeft;
    UIAnchor* anchorPtr = nullptr; // the widget's editable anchor (for the editor)
    Vec2*     position = nullptr;   // the widget's editable offset (pixels)
    Vec2*     sizePtr  = nullptr;   // the widget's editable size, or null (text)
    Vec2      size{0.0f, 0.0f};

    /// Absolute top-left pixel of the widget on a canvas of the given size.
    Vec2 Origin(float canvasW, float canvasH) const {
        return ResolveAnchor(anchor, *position, size, canvasW, canvasH);
    }
    /// Whether a point (canvas pixels) falls inside the widget's rect.
    bool Contains(const Vec2& p, float canvasW, float canvasH) const {
        Vec2 o = Origin(canvasW, canvasH);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }
};

/// Resolve the UI rect of a GameObject, if it carries any screen-space widget.
/// Box widgets are checked before text so a labelled control reports its box.
inline UIRect GetUIRect(GameObject* go) {
    UIRect r;
    if (!go) return r;
    if (auto* b = go->GetComponent<UIButton>())          { r.valid = true; r.anchor = b->anchor; r.anchorPtr = &b->anchor;  r.position = &b->position;  r.sizePtr = &b->size;  r.size = b->size; }
    else if (auto* p = go->GetComponent<UIPanel>())       { r.valid = true; r.anchor = p->anchor; r.anchorPtr = &p->anchor;  r.position = &p->position;  r.sizePtr = &p->size;  r.size = p->size; }
    else if (auto* im = go->GetComponent<UIImage>())      { r.valid = true; r.anchor = im->anchor; r.anchorPtr = &im->anchor; r.position = &im->position; r.sizePtr = &im->size; r.size = im->size; }
    else if (auto* sl = go->GetComponent<UISlider>())     { r.valid = true; r.anchor = sl->anchor; r.anchorPtr = &sl->anchor; r.position = &sl->position; r.sizePtr = &sl->size; r.size = sl->size; }
    else if (auto* sp = go->GetComponent<UIStepper>())    { r.valid = true; r.anchor = sp->anchor; r.anchorPtr = &sp->anchor; r.position = &sp->position; r.sizePtr = &sp->size; r.size = sp->size; }
    else if (auto* rt = go->GetComponent<UIRating>())     { r.valid = true; r.anchor = rt->anchor; r.anchorPtr = &rt->anchor; r.position = &rt->position; r.sizePtr = &rt->size; r.size = rt->size; }
    else if (auto* tg = go->GetComponent<UIToggle>())     { r.valid = true; r.anchor = tg->anchor; r.anchorPtr = &tg->anchor; r.position = &tg->position; r.sizePtr = &tg->size; r.size = tg->size; }
    else if (auto* pb = go->GetComponent<UIProgressBar>()){ r.valid = true; r.anchor = pb->anchor; r.anchorPtr = &pb->anchor; r.position = &pb->position; r.sizePtr = &pb->size; r.size = pb->size; }
    else if (auto* rp = go->GetComponent<UIRadialProgress>()){ r.valid = true; r.anchor = rp->anchor; r.anchorPtr = &rp->anchor; r.position = &rp->position; r.sizePtr = &rp->size; r.size = rp->size; }
    else if (auto* in = go->GetComponent<UIInputField>()) { r.valid = true; r.anchor = in->anchor; r.anchorPtr = &in->anchor; r.position = &in->position; r.sizePtr = &in->size; r.size = in->size; }
    else if (auto* dd = go->GetComponent<UIDropdown>())   { r.valid = true; r.anchor = dd->anchor; r.anchorPtr = &dd->anchor; r.position = &dd->position; r.sizePtr = &dd->size; r.size = dd->size; }
    else if (auto* tb = go->GetComponent<UITabs>())       { r.valid = true; r.anchor = tb->anchor; r.anchorPtr = &tb->anchor; r.position = &tb->position; r.sizePtr = &tb->size; r.size = tb->size; }
    else if (auto* sv = go->GetComponent<UIScrollView>()) { r.valid = true; r.anchor = sv->anchor; r.anchorPtr = &sv->anchor; r.position = &sv->position; r.sizePtr = &sv->size; r.size = sv->size; }
    else if (auto* lg = go->GetComponent<UILayoutGroup>()){ // a controller (no size): movable by its origin, not resizable
        r.valid = true; r.anchor = lg->anchor; r.anchorPtr = &lg->anchor; r.position = &lg->origin; r.sizePtr = nullptr;
        float ext = lg->ContentSize() > 24.0f ? lg->ContentSize() : 24.0f;
        r.size = (lg->direction == UILayoutGroup::Direction::Vertical) ? Vec2{160.0f, ext} : Vec2{ext, 40.0f};
    }
    else if (auto* tr = go->GetComponent<TextRenderer>()) {
        if (tr->screenSpace) {
            r.valid = true; r.anchor = tr->anchor; r.anchorPtr = &tr->anchor; r.position = &tr->screenPos;
            r.sizePtr = &tr->size; r.size = tr->size;   // box: selectable + resizable
        }
    }
    return r;
}

/// Does this GameObject carry any screen-space UI widget?
inline bool IsUIElement(GameObject* go) { return GetUIRect(go).valid; }

/// Whether a widget currently holds keyboard/gamepad menu focus (NavigateUI).
inline bool IsUIFocused(GameObject* go) {
    if (!go) return false;
    if (auto* b = go->GetComponent<UIButton>())   return b->IsFocused();
    if (auto* t = go->GetComponent<UIToggle>())   return t->IsFocused();
    if (auto* s = go->GetComponent<UISlider>())   return s->IsFocused();
    if (auto* p = go->GetComponent<UIStepper>())  return p->IsFocused();
    if (auto* r = go->GetComponent<UIRating>())   return r->IsFocused();
    if (auto* d = go->GetComponent<UIDropdown>()) return d->IsFocused();
    if (auto* tb = go->GetComponent<UITabs>())    return tb->IsFocused();
    return false;
}

/// The Canvas a widget belongs to: the nearest Canvas on itself or an ancestor
/// (Unity's rule that UI lives under a Canvas). nullptr if it isn't parented to
/// one — legacy UI then renders against the raw screen at scale 1.
inline Canvas* OwningCanvas(GameObject* go) {
    for (Transform* t = go ? go->transform : nullptr; t; t = t->Parent())
        if (t->gameObject)
            if (auto* cv = t->gameObject->GetComponent<Canvas>()) return cv;
    return nullptr;
}

/// The pixel scale a widget is drawn at: its owning Canvas's scale factor for
/// the current screen, or 1 if it has no Canvas.
inline float UIScaleFor(GameObject* go, float screenW, float screenH) {
    Canvas* cv = OwningCanvas(go);
    return cv ? cv->ScaleFactor(screenW, screenH) : 1.0f;
}

/// True when the widget's owning Canvas is hidden (Canvas.visible == false) —
/// the renderer skips drawing it. No Canvas / no flag = visible.
inline bool UIHidden(GameObject* go) {
    Canvas* cv = OwningCanvas(go);
    return cv && !cv->visible;
}

/// The sort order of a widget's owning Canvas (0 if it has none). Higher draws on
/// top — renderers iterate UI in this order so a popup/HUD Canvas can sit above the
/// rest. A stable sort by this keeps same-Canvas widgets in their authored order.
inline int CanvasSortOrder(GameObject* go) {
    Canvas* cv = OwningCanvas(go);
    return cv ? cv->sortOrder : 0;
}

/// A hierarchy PRE-ORDER rank per object (index into `objects`): a parent comes
/// before its children, earlier siblings before later ones. Walking the transform
/// tree from each root (in scene order) gives every object a rank; orphaned
/// transforms (shouldn't happen) get ranks after the rest. This is the tie-breaker
/// the UI draw order uses so nested widgets layer like Unity and bring-to-front /
/// send-to-back (sibling reordering) takes effect. `objects` is the scene's
/// `std::vector<std::unique_ptr<GameObject>>`.
template <class ObjVec>
inline std::vector<std::size_t> BuildUIPreOrder(const ObjVec& objects) {
    const std::size_t n = objects.size();
    std::unordered_map<GameObject*, std::size_t> pos;
    pos.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) pos[objects[i].get()] = i;

    std::vector<std::size_t> pre(n, n);   // n = "unset"
    std::size_t counter = 0;
    // Iterative DFS to avoid deep recursion on large hierarchies.
    std::vector<Transform*> stack;
    auto visitRoot = [&](Transform* root) {
        stack.clear();
        stack.push_back(root);
        while (!stack.empty()) {
            Transform* t = stack.back(); stack.pop_back();
            if (!t || !t->gameObject) continue;
            auto it = pos.find(t->gameObject);
            if (it != pos.end() && pre[it->second] == n) pre[it->second] = counter++;
            // Push children in reverse so the first child is processed first.
            const auto& ch = t->Children();
            for (auto rit = ch.rbegin(); rit != ch.rend(); ++rit) stack.push_back(*rit);
        }
    };
    for (std::size_t i = 0; i < n; ++i) {
        Transform* t = objects[i]->transform;
        if (t && !t->Parent()) visitRoot(t);
    }
    for (std::size_t i = 0; i < n; ++i)
        if (pre[i] == n) pre[i] = counter++;
    return pre;
}

/// The order UI should be drawn in: primary key the owning Canvas's sortOrder
/// (higher draws on top), secondary key the scene's hierarchy PRE-ORDER. Returns
/// indices into `objects`. Renderers that still use per-type passes iterate this so
/// nested widgets layer like Unity — a child panel above its parent — and sibling
/// reordering takes effect. The single-pass renderer instead uses BuildUIPreOrder
/// directly together with each widget's layer (see BuildUIDrawList).
template <class ObjVec>
inline std::vector<std::size_t> BuildUIDrawOrder(const ObjVec& objects) {
    const std::size_t n = objects.size();
    std::vector<std::size_t> pre = BuildUIPreOrder(objects);
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        int sa = CanvasSortOrder(objects[a].get()), sb = CanvasSortOrder(objects[b].get());
        if (sa != sb) return sa < sb;
        return pre[a] < pre[b];
    });
    return order;
}

/// One queued UI draw: which object (index into the scene list), an opaque renderer
/// `kind` tag identifying the draw block to run, and the widget's default type
/// `layer` (its historic per-type pass position). A single object can produce more
/// than one item (e.g. a drop target draws a background then a highlight).
struct UIDrawItem {
    std::size_t index;   // index into the scene object list
    int         kind;    // renderer-defined draw-block selector
    int         layer;   // default type layer (pass order) + final tie-breaker
};

/// Sort queued UI draw items into a single drawing pass. The key is, in order:
///   1. owning Canvas sortOrder (higher draws later / on top),
///   2. the object's uiDrawOrder override if non-zero, else the item's type layer,
///   3. hierarchy pre-order (parent before child, sibling order),
///   4. the type layer again (so an object's own multi-layer parts keep their order,
///      and an overridden object's widgets keep type order among themselves).
/// With every uiDrawOrder left at 0 this reproduces the historic per-type pass order
/// exactly (key 2 == type layer), so existing scenes are unchanged; a non-zero
/// uiDrawOrder lets a widget layer freely against widgets of any other type.
template <class ObjVec>
inline std::vector<UIDrawItem> SortUIDrawItems(const ObjVec& objects,
                                               std::vector<UIDrawItem> items) {
    std::vector<std::size_t> pre = BuildUIPreOrder(objects);
    auto eff = [&](const UIDrawItem& it) {
        int o = objects[it.index]->uiDrawOrder;
        return o != 0 ? o : it.layer;
    };
    std::stable_sort(items.begin(), items.end(), [&](const UIDrawItem& a, const UIDrawItem& b) {
        int ca = CanvasSortOrder(objects[a.index].get()), cb = CanvasSortOrder(objects[b.index].get());
        if (ca != cb) return ca < cb;
        int ea = eff(a), eb = eff(b);
        if (ea != eb) return ea < eb;
        if (pre[a.index] != pre[b.index]) return pre[a.index] < pre[b.index];
        return a.layer < b.layer;
    });
    return items;
}

/// The master opacity [0,1] the widget should be drawn at (its Canvas's opacity,
/// or 1 if none). Multiply each widget's color alpha by this.
inline float UIOpacity(GameObject* go) {
    Canvas* cv = OwningCanvas(go);
    return cv ? (cv->opacity < 0.0f ? 0.0f : (cv->opacity > 1.0f ? 1.0f : cv->opacity)) : 1.0f;
}

/// The Scroll View a widget lives in (nearest ancestor), or nullptr — used to
/// offset and clip the widget so it scrolls with its container.
inline UIScrollView* OwningScrollView(GameObject* go) {
    if (!go) return nullptr;
    for (Transform* t = go->transform ? go->transform->Parent() : nullptr; t; t = t->Parent())
        if (t->gameObject)
            if (auto* sv = t->gameObject->GetComponent<UIScrollView>()) return sv;
    return nullptr;
}

/// The nearest ancestor that is itself a UI widget (so a child can be positioned
/// RELATIVE to its parent, Unity-style). Scroll Views are skipped — they manage
/// their own content offset/clip — so this is for panels/images/buttons/etc.
inline GameObject* OwningUIParent(GameObject* go) {
    if (!go || !go->transform) return nullptr;
    for (Transform* t = go->transform->Parent(); t; t = t->Parent()) {
        GameObject* p = t->gameObject;
        if (!p) continue;
        if (p->GetComponent<UIScrollView>()) continue;
        if (GetUIRect(p).valid) return p;
    }
    return nullptr;
}

/// A button's text child: the first active direct child carrying a screen-space
/// TextRenderer. When present, the button draws its label through that child
/// (Unity's Button→Text object) instead of its built-in `label`, so the text can be
/// styled/moved independently. Returns nullptr for legacy buttons with no child.
inline GameObject* UIButtonTextChild(GameObject* go) {
    if (!go || !go->transform) return nullptr;
    for (Transform* c : go->transform->Children()) {
        if (!c || !c->gameObject || !c->gameObject->active) continue;
        if (auto* tr = c->gameObject->GetComponent<TextRenderer>())
            if (tr->screenSpace) return c->gameObject;
    }
    return nullptr;
}

/// Resolve a widget to its final screen rect. Offsets/sizes scale by the owning
/// Canvas factor. If the widget is parented under another UI widget, its anchor
/// resolves WITHIN the parent's rect (so moving/resizing the parent moves the
/// child — like Unity); otherwise it anchors against the whole screen. This is the
/// single source of truth shared by rendering, hit-testing and the editor.
inline bool GetUIScreenRect(GameObject* go, float screenW, float screenH,
                            Vec2& origin, Vec2& size, float* outScale = nullptr) {
    UIRect r = GetUIRect(go);
    if (!r.valid) return false;
    float s = UIScaleFor(go, screenW, screenH);
    size = r.size * s;
    if (GameObject* parent = OwningUIParent(go)) {
        Vec2 po, ps;                                  // resolve inside the parent's rect
        if (GetUIScreenRect(parent, screenW, screenH, po, ps)) {
            Vec2 local = ResolveAnchor(r.anchor, *r.position * s, size, ps.x, ps.y);
            origin = po + local;
            if (UIScrollView* sv = OwningScrollView(go)) origin.y -= sv->scroll * s;
            if (outScale) *outScale = s;
            return true;
        }
    }
    origin = ResolveAnchor(r.anchor, *r.position * s, size, screenW, screenH);
    // Widgets inside a Scroll View move up as it scrolls.
    if (UIScrollView* sv = OwningScrollView(go)) origin.y -= sv->scroll * s;
    if (outScale) *outScale = s;
    return true;
}

/// Total height of a Scroll View's content: the furthest bottom edge of its direct
/// UI children (resolved within the viewport), so `contentHeight` can track the
/// actual content instead of being hand-tuned. Returns 0 if it has no UI children.
inline float ScrollViewContentHeight(GameObject* sv) {
    UIRect svr = GetUIRect(sv);
    if (!svr.valid || !sv || !sv->transform) return 0.0f;
    float maxBottom = 0.0f;
    for (Transform* c : sv->transform->Children()) {
        if (!c || !c->gameObject || !c->gameObject->active) continue;
        UIRect r = GetUIRect(c->gameObject);
        if (!r.valid || !r.position) continue;
        Vec2 local = ResolveAnchor(r.anchor, *r.position, r.size, svr.size.x, svr.size.y);
        maxBottom = local.y + r.size.y > maxBottom ? local.y + r.size.y : maxBottom;
    }
    return maxBottom;
}

/// The top-left screen pixel of a widget, resolved WITHIN its UI parent when it has
/// one (so moving/resizing the parent moves the child — Unity-style), else against
/// the whole screen. Unscaled (Canvas scale = 1); the renderers that don't apply a
/// Canvas scale use this so children follow their parent. Mirrors GetUIScreenRect's
/// anchor math without the scale factor.
inline Vec2 UIResolveOrigin(GameObject* go, float screenW, float screenH) {
    UIRect r = GetUIRect(go);
    if (!r.valid || !r.position) return Vec2{0.0f, 0.0f};
    if (GameObject* parent = OwningUIParent(go)) {
        UIRect pr = GetUIRect(parent);
        if (pr.valid)
            return UIResolveOrigin(parent, screenW, screenH) +
                   ResolveAnchor(r.anchor, *r.position, r.size, pr.size.x, pr.size.y);
    }
    return ResolveAnchor(r.anchor, *r.position, r.size, screenW, screenH);
}

/// Whether a point (screen pixels) falls inside a widget's scaled rect.
inline bool UIScreenContains(GameObject* go, const Vec2& p, float screenW, float screenH) {
    Vec2 o, sz;
    if (!GetUIScreenRect(go, screenW, screenH, o, sz)) return false;
    return p.x >= o.x && p.y >= o.y && p.x <= o.x + sz.x && p.y <= o.y + sz.y;
}

} // namespace okay
