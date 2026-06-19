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
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/Canvas.hpp"
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
    Vec2*     position = nullptr;   // the widget's editable offset (pixels)
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
    if (auto* b = go->GetComponent<UIButton>())          { r.valid = true; r.anchor = b->anchor;  r.position = &b->position;  r.size = b->size; }
    else if (auto* p = go->GetComponent<UIPanel>())       { r.valid = true; r.anchor = p->anchor;  r.position = &p->position;  r.size = p->size; }
    else if (auto* im = go->GetComponent<UIImage>())      { r.valid = true; r.anchor = im->anchor; r.position = &im->position; r.size = im->size; }
    else if (auto* sl = go->GetComponent<UISlider>())     { r.valid = true; r.anchor = sl->anchor; r.position = &sl->position; r.size = sl->size; }
    else if (auto* tg = go->GetComponent<UIToggle>())     { r.valid = true; r.anchor = tg->anchor; r.position = &tg->position; r.size = tg->size; }
    else if (auto* pb = go->GetComponent<UIProgressBar>()){ r.valid = true; r.anchor = pb->anchor; r.position = &pb->position; r.size = pb->size; }
    else if (auto* tr = go->GetComponent<TextRenderer>()) {
        if (tr->screenSpace) {
            r.valid = true; r.anchor = tr->anchor; r.position = &tr->screenPos;
            r.size = {(float)tr->PixelWidth() * tr->pixelSize,
                      (float)tr->PixelHeight() * tr->pixelSize};
        }
    }
    return r;
}

/// Does this GameObject carry any screen-space UI widget?
inline bool IsUIElement(GameObject* go) { return GetUIRect(go).valid; }

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

/// Resolve a widget to its final screen rect, accounting for the owning Canvas's
/// scale: offsets and sizes scale by the canvas factor, then anchor against the
/// screen. This is the single source of truth shared by rendering, hit-testing
/// and the editor's pick/drag so they always agree.
inline bool GetUIScreenRect(GameObject* go, float screenW, float screenH,
                            Vec2& origin, Vec2& size, float* outScale = nullptr) {
    UIRect r = GetUIRect(go);
    if (!r.valid) return false;
    float s = UIScaleFor(go, screenW, screenH);
    size = r.size * s;
    origin = ResolveAnchor(r.anchor, *r.position * s, size, screenW, screenH);
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
