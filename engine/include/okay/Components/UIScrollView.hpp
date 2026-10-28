#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIWorldContext.hpp"   // UIToLocal
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/UITouchScroll.hpp"
#include <cmath>

namespace okay {

/// A scrollable UI region. Its child UI widgets are
/// offset by the scroll position and clipped to the viewport rect, with a
/// scrollbar on the right (vertical) and/or along the bottom (horizontal). Put
/// widgets as children of the scroll view's GameObject; the content extent is
/// measured from them (or set by hand), and the mouse wheel, a drag of a
/// scrollbar or a finger dragging the content moves through it. A horizontal-only view scrolls with the wheel;
/// one that scrolls both ways takes Shift+wheel for the horizontal axis.
class UIScrollView final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};      // viewport top-left (pixels, from anchor)
    Vec2 size{300.0f, 200.0f};        // viewport size
    UIAnchor anchor = UIAnchor::TopLeft;
    Color background = Color::FromBytes(30, 33, 41, 230);
    Color barColor   = Color::FromBytes(255, 255, 255, 46);
    /// Which ways the content scrolls.
    enum class Axis { Vertical, Horizontal, Both };
    Axis  axis = Axis::Vertical;
    float contentHeight = 400.0f;     // total height of the scrollable content
    float contentWidth = 0.0f;        // total width (horizontal scrolling)
    float scroll = 0.0f;              // current vertical scroll offset (pixels)
    float scrollX = 0.0f;             // current horizontal scroll offset (pixels)
    /// When true (default), the content extent is recomputed each frame from the
    /// child widgets, so scrolling "just works" without hand-setting it. Turn off
    /// to drive contentHeight / contentWidth yourself.
    bool  autoContent = true;
    float barWidth = 8.0f;            // scrollbar hit/draw width (pixels)

    bool  ScrollsY() const { return axis != Axis::Horizontal; }
    bool  ScrollsX() const { return axis != Axis::Vertical; }
    float ScrollMax() const { return ScrollsY() ? Mathf::Max(0.0f, contentHeight - size.y) : 0.0f; }
    float ScrollMaxX() const { return ScrollsX() ? Mathf::Max(0.0f, contentWidth - size.x) : 0.0f; }
    void  ScrollBy(float delta) { scroll = Mathf::Clamp(scroll + delta, 0.0f, ScrollMax()); }
    void  SetScroll(float v)    { scroll = Mathf::Clamp(v, 0.0f, ScrollMax()); }
    void  ScrollByX(float delta) { scrollX = Mathf::Clamp(scrollX + delta, 0.0f, ScrollMaxX()); }
    void  SetScrollX(float v)    { scrollX = Mathf::Clamp(v, 0.0f, ScrollMaxX()); }
    /// 0..1 fraction scrolled (for a scrollbar thumb).
    float Fraction() const { float m = ScrollMax(); return m > 0.0f ? scroll / m : 0.0f; }
    float FractionX() const { float m = ScrollMaxX(); return m > 0.0f ? scrollX / m : 0.0f; }
    /// The content offset its children move by (x and y, pixels).
    Vec2  Offset() const { return Vec2{ScrollsX() ? scrollX : 0.0f, ScrollsY() ? scroll : 0.0f}; }
    /// Pixels scrolled per wheel notch.
    float wheelSpeed = 30.0f;
    // grok/ui-controls — 0 / false / 0 keep the immediate wheel step and the old bar.
    /// Wheel glide. 0 = each notch scrolls immediately. Higher stops the glide sooner (1/seconds).
    float inertia = 0.0f;
    /// Let a drag or a glide pass the ends, then spring back.
    bool elastic = false;
    /// Scrollbar: 0 = the current bar, 1 = thin, 2 = hidden until the view is scrolling.
    int barStyle = 0;
    bool IsDraggingBar() const { return m_dragBar || m_dragBarX; }
    void SetInertia(float v) { inertia = v < 0.0f ? 0.0f : v; if (inertia <= 0.0f) m_velY = m_velX = 0.0f; }
    void SetElastic(bool on) { elastic = on; }
    void SetBarStyle(int s) { barStyle = s < 0 ? 0 : (s > 2 ? 2 : s); }
    float Inertia() const { return inertia; }
    bool Elastic() const { return elastic; }
    int BarStyle() const { return barStyle; }
    /// Autohide bars stay hidden until a scroll; other styles are always drawn.
    bool BarVisible() const { return barStyle != 2 || m_barHold > 0.0f || m_dragBar || m_dragBarX; }

    /// A screen point mapped into this widget's own frame (where it is really drawn:
    /// inside its UI parent, at the Canvas scale). See UIToLocal.
    Vec2 ToLocal(const Vec2& p) const { return UIToLocal(gameObject, ResolveAnchor(anchor, position, size), size, p); }
    bool LocalContains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y && p.x <= o.x + size.x && p.y <= o.y + size.y;
    }
    bool Contains(const Vec2& sp) const { return LocalContains(ToLocal(sp)); }

    // Wheel-scroll when the pointer is over the viewport, and let the player grab a
    // scrollbar (right edge / bottom edge) and drag it (built game and play mode).
    void Update(float dt) override {
        Vec2 m = ToLocal(Input::MousePosition());   // the viewport's own frame
        Vec2 o = ResolveAnchor(anchor, position, size);
        auto onBars = [&](const Vec2& p) {
            return (ScrollsY() && p.x >= o.x + size.x - barWidth - 4.0f && p.x <= o.x + size.x) ||
                   (ScrollsX() && p.y >= o.y + size.y - barWidth - 4.0f && p.y <= o.y + size.y);
        };
        float wheel = Input::MouseWheel();
        if (wheel != 0.0f && LocalContains(m)) {
            const bool horiz = axis == Axis::Horizontal || (axis == Axis::Both && Input::GetKey(Input::KeyShift));
            if (inertia <= 0.0f && !elastic) {
                if (horiz) ScrollByX(-wheel * wheelSpeed);
                else       ScrollBy(-wheel * wheelSpeed);
            } else if (inertia <= 0.0f) {
                if (horiz) Nudge(scrollX, -wheel * wheelSpeed, ScrollMaxX(), size.x);
                else       Nudge(scroll, -wheel * wheelSpeed, ScrollMax(), size.y);
            } else if (horiz) {
                m_velX += -wheel * wheelSpeed * 18.0f;
            } else {
                m_velY += -wheel * wheelSpeed * 18.0f;
            }
            m_barHold = 0.75f;
        }

        // Scrollbar drag: grab anywhere in a bar's strip, then track the pointer
        // (clamped); clicking the track also jumps the thumb to that spot.
        bool onBar = ScrollsY() && m.x >= o.x + size.x - barWidth - 4.0f && m.x <= o.x + size.x &&
                     m.y >= o.y && m.y <= o.y + size.y;
        bool onBarX = ScrollsX() && m.y >= o.y + size.y - barWidth - 4.0f && m.y <= o.y + size.y &&
                      m.x >= o.x && m.x <= o.x + size.x;
        if (Input::GetMouseButtonDown(0)) {
            if (onBar && ScrollMax() > 0.0f) m_dragBar = true;
            else if (onBarX && ScrollMaxX() > 0.0f) m_dragBarX = true;
        }
        if (!Input::GetMouseButton(0)) m_dragBar = m_dragBarX = false;
        if (m_dragBar && size.y > 0.0f) {
            float thumb = size.y * (contentHeight > 1.0f ? size.y / contentHeight : 1.0f);
            float t = Mathf::Clamp01((m.y - o.y - thumb * 0.5f) / Mathf::Max(1.0f, size.y - thumb));
            SetScroll(t * ScrollMax());
        }
        if (m_dragBarX && size.x > 0.0f) {
            float thumb = size.x * (contentWidth > 1.0f ? size.x / contentWidth : 1.0f);
            float t = Mathf::Clamp01((m.x - o.x - thumb * 0.5f) / Mathf::Max(1.0f, size.x - thumb));
            SetScrollX(t * ScrollMaxX());
        }

        // A finger dragging the content (not a scrollbar) scrolls it, and a flick glides.
        m_touch.scrollX = ScrollsX(); m_touch.scrollY = ScrollsY();
        const Vec2 d = m_touch.Step(dt,
            [&](const Vec2& sp) { Vec2 p = ToLocal(sp); return LocalContains(p) && !onBars(p); },
            [&](const Vec2& sp) { return ToLocal(sp); });
        if (d.x != 0.0f || d.y != 0.0f) {
            const Vec2 before = Offset();
            if (ScrollsY()) { if (elastic) Nudge(scroll, d.y, ScrollMax(), size.y); else ScrollBy(d.y); }
            if (ScrollsX()) { if (elastic) Nudge(scrollX, d.x, ScrollMaxX(), size.x); else ScrollByX(d.x); }
            if (!m_touch.tracking && Offset().x == before.x && Offset().y == before.y) m_touch.Stop();   // hit the end
            m_barHold = 0.75f;
        }
        // grok/ui-controls: wheel glide, then a spring when the content is past an end.
        if (inertia > 0.0f && !m_dragBar && !m_dragBarX && !m_touch.tracking) {
            auto coast = [&](float& vel, float& pos, auto step) {
                if (std::fabs(vel) <= 0.5f) { vel = 0.0f; return; }
                const float before = pos;
                step(vel * dt);
                vel *= std::exp(-inertia * dt);
                if (!elastic && pos == before) vel = 0.0f;
                m_barHold = 0.75f;
            };
            if (ScrollsY()) coast(m_velY, scroll, [&](float dlt) {
                if (elastic) Nudge(scroll, dlt, ScrollMax(), size.y); else ScrollBy(dlt);
            });
            if (ScrollsX()) coast(m_velX, scrollX, [&](float dlt) {
                if (elastic) Nudge(scrollX, dlt, ScrollMaxX(), size.x); else ScrollByX(dlt);
            });
        }
        if (elastic && !m_dragBar && !m_dragBarX && !m_touch.tracking) {
            Spring(scroll, ScrollMax(), dt);
            Spring(scrollX, ScrollMaxX(), dt);
        }
        if (m_dragBar || m_dragBarX) m_barHold = 0.75f;
        else if (m_barHold > 0.0f) m_barHold -= dt;
    }

    bool IsTouchDragging() const { return m_touch.tracking && m_touch.dragging; }

private:
    static void Nudge(float& v, float delta, float maxv, float view) {
        const float lim = std::min(72.0f, std::max(24.0f, view * 0.35f));
        if (v < 0.0f || v > maxv) v += delta * 0.28f;
        else {
            float next = v + delta;
            if (next < 0.0f) next = next * 0.35f;
            else if (next > maxv) next = maxv + (next - maxv) * 0.35f;
            v = next;
        }
        if (v < -lim) v = -lim;
        if (v > maxv + lim) v = maxv + lim;
    }
    static void Spring(float& v, float maxv, float dt) {
        if (v >= 0.0f && v <= maxv) return;
        const float target = v < 0.0f ? 0.0f : maxv;
        v += (target - v) * (1.0f - std::exp(-14.0f * dt));
        if (std::fabs(v - target) < 0.5f) v = target;
    }
    bool m_dragBar = false;
    bool m_dragBarX = false;
    float m_velY = 0.0f, m_velX = 0.0f;
    float m_barHold = 0.0f;
    UITouchScroll m_touch;
};

} // namespace okay
