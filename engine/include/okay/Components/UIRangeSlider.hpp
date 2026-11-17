#pragma once
// UI Range Slider: one track with TWO handles that pick a low and a high value
// (price filter, level range, volume window). Written from the plain idea of a
// two-thumb slider; it shares only our own helpers (UIWidgetUtil, UIShape).
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mathf.hpp"
#include <cstdio>
#include <string>

namespace okay {

/// A track with a low handle and a high handle. The low handle never goes above
/// the high handle (minGap keeps them apart). The player drags the handle that is
/// nearest the press; keyboard and gamepad move the picked handle (Enter swaps
/// handles). on_change() runs on the object's script when low or high moves.
class UIRangeSlider final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{260.0f, 28.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    float minValue = 0.0f;
    float maxValue = 100.0f;
    float low = 20.0f;
    float high = 80.0f;
    /// 0 = smooth. Above 0 each handle snaps to multiples of this from minValue.
    float step = 0.0f;
    bool wholeNumbers = false;
    /// Smallest distance the two handles may have.
    float minGap = 0.0f;
    bool showValue = true;
    /// "{lo}" and "{hi}" are replaced by the two numbers. Empty = "{lo} - {hi}".
    std::string valueFormat;
    bool interactable = true;
    bool focusable = true;
    Color background = Color::FromBytes(41, 46, 59);
    Color fill = Color::FromBytes(66, 150, 250);
    Color knob = Color::FromBytes(245, 247, 252);
    Color textColor = Color::White;
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 4.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }
    /// 0 = the low handle is picked, 1 = the high handle.
    int ActiveHandle() const { return m_active; }
    bool IsDragging() const { return m_dragging; }

    float Span() const { return maxValue - minValue; }
    float Fraction(float v) const { return Span() > 0.0f ? Mathf::Clamp01((v - minValue) / Span()) : 0.0f; }
    float LowFraction() const { return Fraction(low); }
    float HighFraction() const { return Fraction(high); }

    /// Clamp a value to the range, then snap it to the step and to whole numbers.
    float Snap(float v) const {
        float nv = Mathf::Clamp(v, minValue, maxValue);
        if (step > 0.0f) nv = Mathf::Clamp(minValue + Mathf::Round((nv - minValue) / step) * step, minValue, maxValue);
        if (wholeNumbers) nv = Mathf::Round(nv);
        return nv;
    }
    float Gap() const { return minGap < 0.0f ? 0.0f : minGap; }

    /// Move the low handle. It stops at (high - gap). True when it moved.
    bool SetLow(float v) {
        float nv = Snap(v);
        const float top = high - Gap();
        if (nv > top) nv = Snap(top) > top ? top : Snap(top);
        if (nv < minValue) nv = minValue;
        if (nv == low) return false;
        low = nv;
        FireUIEvent(gameObject, "on_change");
        return true;
    }
    /// Move the high handle. It stops at (low + gap). True when it moved.
    bool SetHigh(float v) {
        float nv = Snap(v);
        const float bottom = low + Gap();
        if (nv < bottom) nv = Snap(bottom) < bottom ? bottom : Snap(bottom);
        if (nv > maxValue) nv = maxValue;
        if (nv == high) return false;
        high = nv;
        FireUIEvent(gameObject, "on_change");
        return true;
    }
    /// Set both at once (low first, then high, so a swap of order still works).
    void SetRange(float lo, float hi) {
        if (lo > hi) { const float t = lo; lo = hi; hi = t; }
        const float oldLow = low, oldHigh = high;
        low = minValue; high = maxValue;               // free both, then place them
        low = Snap(lo);
        high = Snap(hi);
        if (high < low + Gap()) high = Mathf::Clamp(low + Gap(), minValue, maxValue);
        if (low != oldLow || high != oldHigh) FireUIEvent(gameObject, "on_change");
    }

    /// Which handle is nearer to a point on the track (0..1). A tie goes to the
    /// handle the point has passed, so two stacked handles can still be pulled apart.
    int NearestHandle(float frac) const {
        const float dl = frac - LowFraction(), dh = HighFraction() - frac;
        const float al = dl < 0 ? -dl : dl, ah = dh < 0 ? -dh : dh;
        if (al < ah) return 0;
        if (ah < al) return 1;
        return frac < LowFraction() ? 0 : 1;
    }
    /// A press at `frac` (0..1 along the track): pick the nearest handle and move it there.
    void PressAtFraction(float frac) {
        if (!interactable) return;
        frac = Mathf::Clamp01(frac);
        m_active = NearestHandle(frac);
        m_dragging = true;
        DragToFraction(frac);
    }
    /// Move the picked handle to `frac` (0..1 along the track).
    void DragToFraction(float frac) {
        if (!interactable) return;
        const float v = minValue + Mathf::Clamp01(frac) * Span();
        if (m_active == 0) SetLow(v); else SetHigh(v);
    }
    void Release() { m_dragging = false; }

    /// How far one key press moves a handle.
    float KeyStep() const {
        if (step > 0.0f) return step;
        if (wholeNumbers) return 1.0f;
        return Span() / 20.0f;
    }
    void Nudge(int dir) {
        if (!interactable) return;
        const float d = (float)dir * KeyStep();
        if (m_active == 0) SetLow(low + d); else SetHigh(high + d);
    }
    void SwapHandle() { m_active = 1 - m_active; }

    /// The text beside the track.
    std::string FormatValue() const {
        const bool whole = wholeNumbers || step >= 1.0f;
        char lo[32], hi[32];
        std::snprintf(lo, sizeof(lo), whole ? "%.0f" : "%.2f", low);
        std::snprintf(hi, sizeof(hi), whole ? "%.0f" : "%.2f", high);
        const std::string f = valueFormat.empty() ? std::string("{lo} - {hi}") : valueFormat;
        std::string out;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (f.compare(i, 4, "{lo}") == 0) { out += lo; i += 3; }
            else if (f.compare(i, 4, "{hi}") == 0) { out += hi; i += 3; }
            else out += f[i];
        }
        return out;
    }

    void Update(float) override {
        if (!enabled || !interactable) { m_dragging = false; return; }
        Vec2 trackO, trackSz;
        UIControlRect(anchor, position, size, trackO, trackSz);
        if (trackSz.x <= 0.0f || trackSz.y <= 0.0f) return;
        const Vec2 m = Input::MousePosition();
        const Vec2 lm = UIToLocal(gameObject, trackO, trackSz, m);
        const float frac = Mathf::Clamp01((lm.x - trackO.x) / trackSz.x);
        if (Input::GetMouseButtonDown(0)) {
            Vec2 o, sz;
            const Vec2 p = UIControlPoint(gameObject, anchor, position, size, m, o, sz);
            if (p.x >= o.x && p.y >= o.y && p.x <= o.x + sz.x && p.y <= o.y + sz.y) PressAtFraction(frac);
        } else if (m_dragging && Input::GetMouseButton(0)) {
            DragToFraction(frac);
        }
        if (!Input::GetMouseButton(0)) m_dragging = false;
    }

private:
    int m_active = 0;
    bool m_dragging = false;
    bool m_focused = false;
};

} // namespace okay
