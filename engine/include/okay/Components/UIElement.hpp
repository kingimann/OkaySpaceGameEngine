#pragma once
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/TextRenderer.hpp"
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

} // namespace okay
