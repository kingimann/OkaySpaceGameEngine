#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIWorldContext.hpp"   // UIToLocal
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"
#include <cstdio>
#include <string>

namespace okay {

/// A screen-space horizontal slider the player drags to pick a value in
/// [minValue, maxValue] (volume, sensitivity, difficulty…). Position/size are
/// window pixels (origin top-left). While dragging it updates `value` and calls
/// the sibling ScriptComponent's on_change() handler each frame the value moves.
class UISlider final : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{200.0f, 24.0f};
    float value = 0.5f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    Color background = Color::FromBytes(41, 46, 59);
    Color fill       = Color::FromBytes(66, 150, 250);
    Color knob       = Color::FromBytes(245, 247, 252);
    UIAnchor anchor = UIAnchor::TopLeft;
    // Silhouette of the track + fill (Pill = clean capsule slider).
    UIShape trackShape = UIShape::Rounded;
    // Customization: rounded track, knob width (as a fraction of height), and an
    // optional value readout drawn to the right of the track.
    float cornerRadius = 3.0f;
    float knobSize = 0.6f;                           // knob width = size.y * this
    bool  roundKnob = true;                          // draw a circular handle
    bool  showValue = false;
    bool  wholeNumbers = false;                      // snap the value to integers
    bool  vertical = false;                          // drag/fill bottom->top
    Color textColor = Color::White;
    // grok/ui-options: extra controls. Defaults keep the old drag and the old draw.
    float step = 0.0f;                               // 0 = continuous; >0 snaps to this spacing
    std::string valueFormat;                        // empty = "%.2f"; "{v}" is the number
    bool  fillGradient = false;
    Color fillEnd = Color::FromBytes(120, 210, 255);
    int   knobShape = -1;                           // -1 = roundKnob decides; else a UIShape

    /// 0..1 position of the handle along the track (for rendering).
    float Fraction() const {
        float span = maxValue - minValue;
        return span > 0.0f ? Mathf::Clamp01((value - minValue) / span) : 0.0f;
    }
    /// Clamp, then snap to `step` and whole numbers. `step` 0 leaves the value continuous.
    float Snap(float v) const {
        float nv = Mathf::Clamp(v, minValue, maxValue);
        if (step > 0.0f) {
            nv = minValue + Mathf::Round((nv - minValue) / step) * step;
            nv = Mathf::Clamp(nv, minValue, maxValue);
        }
        if (wholeNumbers) nv = Mathf::Round(nv);
        return nv;
    }
    /// The readout next to the track. An empty format is the historical "%.2f".
    std::string FormatValue() const {
        char num[32];
        const char* spec = (wholeNumbers || step >= 1.0f) ? "%.0f" : "%.2f";
        std::snprintf(num, sizeof(num), spec, value);
        if (valueFormat.empty()) {
            std::snprintf(num, sizeof(num), "%.2f", value);
            return num;
        }
        std::string out;
        out.reserve(valueFormat.size() + 8);
        for (std::size_t i = 0; i < valueFormat.size(); ++i) {
            if (valueFormat[i] == '{' && i + 2 < valueFormat.size() &&
                valueFormat[i + 1] == 'v' && valueFormat[i + 2] == '}') {
                out += num;
                i += 2;
                continue;
            }
            out += valueFormat[i];
        }
        return out;
    }
    void SetStep(float s) { step = s < 0.0f ? 0.0f : s; }
    void SetFormat(const std::string& f) { valueFormat = f; }
    void SetValue(float v) {
        float nv = Snap(v);
        if (nv == value) return;
        value = nv;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_change");
    }
    bool IsDragging() const { return m_dragging; }

    // Keyboard/gamepad menu focus (driven by NavigateUI).
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    /// A screen point mapped into this widget's own frame (where it is really drawn:
    /// inside its UI parent, at the Canvas scale). See UIToLocal.
    Vec2 ToLocal(const Vec2& p) const { return UIToLocal(gameObject, ResolveAnchor(anchor, position, size), size, p); }
    bool Contains(const Vec2& sp) const {
        Vec2 o = ResolveAnchor(anchor, position, size), p = ToLocal(sp);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    bool interactable = true;   // when false: greyed out, ignores drag

    void Update(float) override {
        if (!interactable) { m_dragging = false; return; }
        Vec2 m = Input::MousePosition();
        if (Input::GetMouseButtonDown(0) && Contains(m)) m_dragging = true;
        if (!Input::GetMouseButton(0)) m_dragging = false;
        if (m_dragging && size.x > 0.0f && size.y > 0.0f) {
            Vec2 o = ResolveAnchor(anchor, position, size), lm = ToLocal(m);
            // Horizontal: left->right. Vertical: bottom->top (natural for volume).
            float frac = vertical ? Mathf::Clamp01((o.y + size.y - lm.y) / size.y)
                                  : Mathf::Clamp01((lm.x - o.x) / size.x);
            float nv = Snap(minValue + frac * (maxValue - minValue));
            if (nv != value) {
                value = nv;
                if (gameObject)
                    if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                        if (sc->VM()) sc->VM()->CallEvent("on_change");
            }
        }
    }

private:
    bool m_dragging = false;
    bool m_focused = false;
};

} // namespace okay
