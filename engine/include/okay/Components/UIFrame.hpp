#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Where a UI widget sits inside its parent's rect (its UI parent, else the
/// screen/canvas): a general replacement for the widget's own anchor preset.
///
/// Per axis, `anchorMin`/`anchorMax` are fractions of the parent rect (0 = left /
/// top, 1 = right / bottom):
///   * equal anchors pin the widget to that point: `position` is the offset of the
///     widget's `pivot` (a fraction of its own size) from the anchor point, and
///     `size` is its size;
///   * different anchors stretch it between them: `position` is the margin after
///     anchorMin and `size` the margin before anchorMax (the same meaning the
///     stretch presets give a widget's position and size).
///
/// A widget with a UIFrame resolves its rect from it everywhere (drawing, hit
/// tests, the editor). Its own position / size / anchor fields stay in step as a
/// mirror: a script or tool that writes them moves the frame (GetUIRect picks the
/// change up), and the frame's values are written back for code that reads them.
class UIFrame final : public Behaviour {
public:
    Vec2 anchorMin{0.0f, 0.0f};
    Vec2 anchorMax{0.0f, 0.0f};
    Vec2 pivot{0.0f, 0.0f};
    Vec2 position{0.0f, 0.0f};
    Vec2 size{100.0f, 100.0f};

    /// The anchors and pivot of a legacy preset.
    static void PresetValues(UIAnchor a, Vec2& amin, Vec2& amax, Vec2& pv) {
        switch (a) {
            case UIAnchor::StretchHorizontal: amin = {0.0f, 0.0f}; amax = {1.0f, 0.0f}; pv = {0.0f, 0.0f}; break;
            case UIAnchor::StretchVertical:   amin = {0.0f, 0.0f}; amax = {0.0f, 1.0f}; pv = {0.0f, 0.0f}; break;
            case UIAnchor::StretchFull:       amin = {0.0f, 0.0f}; amax = {1.0f, 1.0f}; pv = {0.0f, 0.0f}; break;
            default: {
                const int i = (int)a;
                amin = amax = pv = Vec2{(i % 3) * 0.5f, (i / 3) * 0.5f};
                break;
            }
        }
    }
    /// Set the anchors and pivot to a legacy preset (position and size unchanged).
    void ApplyPreset(UIAnchor a) {
        PresetValues(a, anchorMin, anchorMax, pivot);
        m_preset = a;
    }
    /// The legacy preset this frame matches, else the last one applied (custom
    /// anchors have no preset; code that only knows presets sees that one).
    UIAnchor Preset() const {
        for (int i = 0; i < 12; ++i) {
            Vec2 amin, amax, pv;
            PresetValues((UIAnchor)i, amin, amax, pv);
            if (amin.x == anchorMin.x && amin.y == anchorMin.y && amax.x == anchorMax.x &&
                amax.y == anchorMax.y && pv.x == pivot.x && pv.y == pivot.y)
                return (UIAnchor)i;
        }
        return m_preset;
    }
    bool StretchesX() const { return anchorMin.x != anchorMax.x; }
    bool StretchesY() const { return anchorMin.y != anchorMax.y; }

    /// Resolve against a parent rect of w x h, with `pos` / `sz` standing in for
    /// position / size (callers pass Canvas-scaled values). For a converted preset
    /// this is the very same arithmetic as ResolveAnchorRect, so it lands on
    /// exactly the same pixels.
    void Resolve(const Vec2& pos, const Vec2& sz, float w, float h, Vec2& origin, Vec2& outSize) const {
        Axis(anchorMin.x, anchorMax.x, pivot.x, pos.x, sz.x, w, origin.x, outSize.x);
        Axis(anchorMin.y, anchorMax.y, pivot.y, pos.y, sz.y, h, origin.y, outSize.y);
    }

    // Mirror bookkeeping (see GetUIRect): the widget field values last written back.
    bool     mirrored = false;
    Vec2     mirrorPos{0.0f, 0.0f}, mirrorSize{0.0f, 0.0f};
    UIAnchor mirrorAnchor = UIAnchor::TopLeft;

private:
    static void Axis(float a0, float a1, float pv, float pos, float sz, float extent, float& o, float& s) {
        if (a0 == a1) {
            s = sz;
            // Pivot on the anchor (every preset): the legacy expression, term for term.
            o = (pv == a0) ? pos + (extent - sz) * a0 : extent * a0 + pos - sz * pv;
        } else {
            o = extent * a0 + pos;
            s = extent * (a1 - a0) - pos - sz;
        }
        if (s < 0.0f) s = 0.0f;   // don't invert when margins overlap
    }
    UIAnchor m_preset = UIAnchor::TopLeft;
};

} // namespace okay
