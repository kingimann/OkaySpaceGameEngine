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

    /// The value to draw: the live hover preview while the pointer is over the
    /// stars, otherwise the committed value. Lets the renderer light up stars as
    /// the player sweeps across them (and back down) before they click.
    float DisplayValue() const { return (!readOnly && m_hovering) ? m_hoverValue : value; }

    /// Fill fraction (0, 0.5 or 1) of star `i` for the displayed value.
    float StarFill(int i) const {
        float f = DisplayValue() - (float)i;
        if (f >= 1.0f) return 1.0f;
        if (f <= 0.0f) return 0.0f;
        return allowHalf ? (f >= 0.5f ? 0.5f : 0.0f) : (f >= 0.5f ? 1.0f : 0.0f);
    }

    /// The rating the pointer `p` maps to (1..count, or half-steps when allowHalf):
    /// the star it's over, minus a half if it's on that star's left half.
    float ValueAt(const Vec2& p) const {
        int i = StarAt(p);
        if (i < 0) return value;
        float v = (float)(i + 1);
        if (allowHalf) {
            Vec2 o = ResolveAnchor(anchor, position, size);
            float cw = CellWidth();
            float local = (p.x - o.x) - (float)i * cw;
            if (local < cw * 0.5f) v -= 0.5f;            // left half = half star
        }
        return v;
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
        if (readOnly) { m_hovering = false; return; }
        Vec2 m = Input::MousePosition();
        m_hovering = StarAt(m) >= 0;                      // pointer is over the stars
        if (!m_hovering) return;
        m_hoverValue = ValueAt(m);                         // live preview as you sweep
        if (Input::GetMouseButtonDown(0)) {
            float v = m_hoverValue;
            if (v == value) v = 0.0f;                      // click the set value again to clear
            SetValue(v);
        }
    }

private:
    bool  m_focused = false;
    bool  m_hovering = false;
    float m_hoverValue = 0.0f;
};

} // namespace okay
