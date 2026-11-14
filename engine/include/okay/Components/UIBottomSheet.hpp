#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <algorithm>

namespace okay {

/// A panel that slides up from the bottom of its box. Open, Close and Toggle
/// move it. A click on the handle does the same, and a click just above the
/// panel closes it when "tap outside closes" is on. on_change() runs when it
/// opens or closes.
class UIBottomSheet final : public Behaviour {
public:
    Vec2 position{20.0f, 400.0f};
    Vec2 size{360.0f, 220.0f};
    UIAnchor anchor = UIAnchor::BottomCenter;
    bool open = false;
    /// 0 is closed, 1 is fully open. Moves toward `open` over `duration` seconds.
    float progress = 0.0f;
    float duration = 0.25f;
    bool closeOnBackdrop = true;
    bool dim = true;
    float handle = 18.0f;
    /// How far above the panel a click still counts as "outside".
    float backdrop = 480.0f;
    std::string title = "Sheet";
    bool interactable = true;
    bool focusable = true;
    Color background = Color::FromBytes(32, 36, 48);
    Color textColor = Color::White;
    Color accent = Color::FromBytes(90, 150, 230);
    Color dimColor = Color::FromBytes(0, 0, 0, 140);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 12.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    bool IsOpen() const { return open; }
    float ShownFraction() const { return progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress); }

    void Open() { SetOpen(true); }
    void Close() { SetOpen(false); }
    void Toggle() { SetOpen(!open); }

    void SetOpen(bool on) {
        if (open == on && ((on && progress >= 1.0f) || (!on && progress <= 0.0f))) return;
        const bool was = open;
        open = on;
        if (duration <= 0.0f) progress = on ? 1.0f : 0.0f;
        if (was != open) FireUIEvent(gameObject, "on_change");
    }

    void PressLocal(const Vec2& local) {
        if (!interactable) return;
        if (local.y < 0.0f) {
            if (closeOnBackdrop && (open || progress > 0.01f)) Close();
            return;
        }
        if (local.y <= handle) {
            if (local.x >= size.x - 32.0f) Close();
            else Toggle();
        }
    }

    void Update(float dt) override {
        if (!enabled) return;
        const float goal = open ? 1.0f : 0.0f;
        if (duration <= 0.0f) progress = goal;
        else if (progress < goal) progress = std::min(goal, progress + dt / duration);
        else if (progress > goal) progress = std::max(goal, progress - dt / duration);
        if (!interactable) return;
        if (!Input::GetMouseButtonDown(0)) return;
        Vec2 o, sz;
        Vec2 p = UIControlPoint(gameObject, anchor, position, size, Input::MousePosition(), o, sz);
        Vec2 local{p.x - o.x, p.y - o.y};
        const bool overX = local.x >= -0.5f && local.x <= sz.x + 0.5f;
        const bool overY = local.y >= -backdrop && local.y <= sz.y + 0.5f;
        if (overX && overY) PressLocal(local);
    }

private:
    bool m_focused = false;
};

} // namespace okay
