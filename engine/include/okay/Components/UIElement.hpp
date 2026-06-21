#pragma once
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Math/Vec2.hpp"

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
    else if (auto* tg = go->GetComponent<UIToggle>())     { r.valid = true; r.anchor = tg->anchor; r.anchorPtr = &tg->anchor; r.position = &tg->position; r.sizePtr = &tg->size; r.size = tg->size; }
    else if (auto* pb = go->GetComponent<UIProgressBar>()){ r.valid = true; r.anchor = pb->anchor; r.anchorPtr = &pb->anchor; r.position = &pb->position; r.sizePtr = &pb->size; r.size = pb->size; }
    else if (auto* in = go->GetComponent<UIInputField>()) { r.valid = true; r.anchor = in->anchor; r.anchorPtr = &in->anchor; r.position = &in->position; r.sizePtr = &in->size; r.size = in->size; }
    else if (auto* dd = go->GetComponent<UIDropdown>())   { r.valid = true; r.anchor = dd->anchor; r.anchorPtr = &dd->anchor; r.position = &dd->position; r.sizePtr = &dd->size; r.size = dd->size; }
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
    if (auto* d = go->GetComponent<UIDropdown>()) return d->IsFocused();
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

/// Whether a point (screen pixels) falls inside a widget's scaled rect.
inline bool UIScreenContains(GameObject* go, const Vec2& p, float screenW, float screenH) {
    Vec2 o, sz;
    if (!GetUIScreenRect(go, screenW, screenH, o, sz)) return false;
    return p.x >= o.x && p.y >= o.y && p.x <= o.x + sz.x && p.y <= o.y + sz.y;
}

} // namespace okay
