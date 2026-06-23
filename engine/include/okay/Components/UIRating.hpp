#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"

namespace okay {

/// A star-rating control: `count` stars, `value` of them filled. The player clicks
/// a star to set the rating (left half = a half-star when allowHalf); read-only
/// mode just displays a score. Fires the sibling ScriptComponent's on_change().
/// Position/size are window pixels; stars are laid out across the width.
class UIRating : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{180.0f, 32.0f};
    int   count = 5;                    // number of stars
    float value = 3.0f;                 // filled stars (may be fractional)
    bool  allowHalf = false;            // half-star precision
    bool  readOnly = false;             // display only (no click to set)
    Color on  = Color::FromBytes(245, 200, 70);   // filled star
    Color off = Color::FromBytes(70, 70, 84);      // empty star
    UIAnchor anchor = UIAnchor::TopLeft;

    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    float CellWidth() const { return count > 0 ? size.x / (float)count : size.x; }

    /// Star index (0..count-1) under the anchored point, or -1 if outside.
    int StarAt(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        if (p.x < o.x || p.y < o.y || p.x > o.x + size.x || p.y > o.y + size.y) return -1;
        float cw = CellWidth();
        if (cw <= 0.0f) return -1;
        int i = (int)((p.x - o.x) / cw);
        return Mathf::Clamp(i, 0, count - 1);
    }

    /// Fill fraction (0, 0.5 or 1) of star `i` for the current value.
    float StarFill(int i) const {
        float f = value - (float)i;
        if (f >= 1.0f) return 1.0f;
        if (f <= 0.0f) return 0.0f;
        return allowHalf ? (f >= 0.5f ? (f >= 1.0f ? 1.0f : 0.5f) : (f > 0.0f ? 0.5f : 0.0f))
                         : (f >= 0.5f ? 1.0f : 0.0f);
    }

    void SetValue(float v) {
        float nv = Mathf::Clamp(v, 0.0f, (float)count);
        if (!allowHalf) nv = Mathf::Round(nv);
        if (nv == value) return;
        value = nv;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_change");
    }

    void Update(float) override {
        if (readOnly) return;
        if (Input::GetMouseButtonDown(0)) {
            int i = StarAt(Input::MousePosition());
            if (i < 0) return;
            float v = (float)(i + 1);
            if (allowHalf) {
                Vec2 o = ResolveAnchor(anchor, position, size);
                float cw = CellWidth();
                float local = (Input::MousePosition().x - o.x) - (float)i * cw;
                if (local < cw * 0.5f) v -= 0.5f;       // left half = half star
            }
            // Click the only filled star again to clear (common rating UX).
            if (v == value) v = 0.0f;
            SetValue(v);
        }
    }

private:
    bool m_focused = false;
};

} // namespace okay
