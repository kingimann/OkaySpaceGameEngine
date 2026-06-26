#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include <string>
#include <vector>

namespace okay {

/// A segmented control / tab bar: a row of equal segments where exactly one is
/// selected (settings tabs, difficulty pickers, view switches). Clicking a segment
/// sets `value` (the selected index) and calls the sibling ScriptComponent's
/// on_change() handler. Position/size are window pixels (origin top-left).
class UITabs : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{300.0f, 40.0f};
    std::vector<std::string> tabs{"One", "Two", "Three"};
    /// Optional content page per tab: the NAME of a GameObject to show when that tab
    /// is selected (all the others are hidden). Index-aligned with `tabs`; an empty
    /// entry (or a missing name) means "no page for this tab". This makes tabs work
    /// with zero scripting — assign a panel to each tab and switching just works.
    std::vector<std::string> pages;
    int  value = 0;                                  // selected segment index
    Color background        = Color::FromBytes(34, 38, 48);   // track
    Color selected          = Color::FromBytes(60, 120, 200); // active segment
    Color textColor         = Color::FromBytes(200, 205, 215);
    Color selectedTextColor = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    // Outer silhouette (Pill = classic segmented control); cornerRadius for Rounded.
    UIShape shape = UIShape::Pill;
    float cornerRadius = 8.0f;
    bool  interactable = true;

    // Keyboard/gamepad menu focus (driven by NavigateUI).
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    int Count() const { return (int)tabs.size(); }

    /// Which segment a local x (pixels from the bar's left edge) falls in, or -1.
    int SegmentAt(float localX) const {
        int n = Count();
        if (n <= 0 || size.x <= 0.0f || localX < 0.0f || localX >= size.x) return -1;
        int i = (int)(localX / (size.x / n));
        return i < 0 ? 0 : (i >= n ? n - 1 : i);
    }

    /// The pixel rect (relative to the bar's top-left) of segment `i`.
    void SegmentRect(int i, float& ox, float& oy, float& w, float& h) const {
        int n = Count() > 0 ? Count() : 1;
        float seg = size.x / n;
        ox = seg * i; oy = 0.0f; w = seg; h = size.y;
    }

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y && p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    /// Show the selected tab's content page and hide every other tab's page (by
    /// GameObject name). Safe to call every frame; no-op for empty entries. This is
    /// what makes tab switching work without any script.
    void ApplyPages() {
        if (!gameObject) return;
        Scene* s = gameObject->scene();
        if (!s) return;
        for (int i = 0; i < (int)pages.size(); ++i) {
            if (pages[i].empty()) continue;
            if (GameObject* g = s->Find(pages[i])) g->active = (i == value);
        }
    }

    /// Select a segment and fire on_change (clamped to a valid index).
    void Select(int index) {
        int n = Count();
        if (n <= 0) return;
        index = index < 0 ? 0 : (index >= n ? n - 1 : index);
        if (index == value) return;
        value = index;
        ApplyPages();
        m_appliedValue = value;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_change");
    }

    bool IsHovered() const { return m_hover; }

    void Update(float) override {
        // Keep the visible page in sync with the selection (also applies the initial
        // state on the first frame, and after the value is set externally / by script).
        if (m_appliedValue != value) { ApplyPages(); m_appliedValue = value; }
        if (!interactable) { m_hover = false; return; }
        Vec2 m = Input::MousePosition();
        m_hover = Contains(m);
        if (m_hover && Input::GetMouseButtonDown(0)) {
            Vec2 o = ResolveAnchor(anchor, position, size);
            Select(SegmentAt(m.x - o.x));
        }
    }

private:
    bool m_hover = false;
    bool m_focused = false;
    int  m_appliedValue = -1;   // last value ApplyPages() reflected (-1 = not yet)
};

} // namespace okay
