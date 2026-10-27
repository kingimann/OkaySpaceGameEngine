#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIWorldContext.hpp"   // UIToLocal
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/UIInputField.hpp"
#include <cctype>
#include <string>
#include <vector>

namespace okay {

/// A screen-space dropdown / option selector: a header box
/// showing the current choice; clicking it opens a list of options below, and
/// picking one sets `value` (the selected index) and calls the sibling
/// ScriptComponent's on_change() handler. Position/size are window pixels
/// (origin top-left); each open option is `size.y` tall.
class UIDropdown final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{180.0f, 32.0f};
    std::vector<std::string> options{"Option A", "Option B", "Option C"};
    int  value = 0;                                 // selected option index
    bool open  = false;                             // list expanded
    Color color       = Color::FromBytes(41, 46, 59);
    Color hoverColor  = Color::FromBytes(55, 62, 80);   // highlighted option
    Color listColor   = Color::FromBytes(30, 33, 41);   // open list background
    Color textColor   = Color::White;
    Color borderColor = Color::FromBytes(255, 255, 255, 22);
    UIAnchor anchor = UIAnchor::TopLeft;
    // Silhouette + corner radius for the header (and list) — clean rounded selects.
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;
    /// Shown in the header (dimmed) when nothing is chosen (value < 0).
    std::string placeholder = "Select...";
    // grok/ui-controls — off by default, so the open list stays the old row-per-option path.
    /// A search row at the top of the open list. Typing filters `options` (not saved).
    bool searchable = false;
    /// How many matching options the open list shows. 0 = every match.
    int maxVisible = 0;
    /// Short glyph drawn in front of each option ("" = none). Not a texture.
    std::vector<std::string> optionIcons;

    void SetSearchable(bool on) { searchable = on; if (!on) m_filter.clear(); }
    void SetMaxVisible(int n) { maxVisible = n < 0 ? 0 : n; }
    void SetOptionIcon(int i, const std::string& glyph) {
        if (i < 0) return;
        if ((int)optionIcons.size() <= i) optionIcons.resize((std::size_t)i + 1);
        optionIcons[(std::size_t)i] = glyph;
    }
    bool Searchable() const { return searchable; }
    int MaxVisible() const { return maxVisible; }
    const std::string& Filter() const { return m_filter; }
    const std::string& IconOf(int i) const {
        static const std::string none;
        return (i >= 0 && i < (int)optionIcons.size()) ? optionIcons[(std::size_t)i] : none;
    }
    /// True when the open list is not the plain one-row-per-option list.
    bool ListExtras() const {
        if (searchable || maxVisible > 0) return true;
        for (const std::string& ic : optionIcons) if (!ic.empty()) return true;
        return false;
    }

    /// The currently selected option text (empty if none).
    const std::string& Selected() const {
        static const std::string empty;
        if (value >= 0 && value < (int)options.size()) return options[value];
        return empty;
    }

    /// Header text: the selection, or the placeholder when none is chosen.
    const std::string& HeaderText() const {
        return (value >= 0 && value < (int)options.size()) ? options[value] : placeholder;
    }
    bool HasSelection() const { return value >= 0 && value < (int)options.size(); }

    /// Set the selection by index, firing on_change if it actually changed.
    void SetValue(int v) {
        if (v < 0 || v >= (int)options.size() || v == value) return;
        value = v;
        Fire();
    }

    bool IsHovered() const { return m_hover; }
    int  HoveredOption() const { return m_hoverOption; }   // -1 = none

    // Keyboard/gamepad menu focus (driven by NavigateUI).
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }
    /// Move the selection by `dir` (-1/+1), clamped, firing on_change.
    void Cycle(int dir) {
        int v = value + dir;
        if (v < 0) v = 0;
        if (v > (int)options.size() - 1) v = (int)options.size() - 1;
        SetValue(v);
    }

    /// A screen point mapped into this widget's own frame (where it is really drawn:
    /// inside its UI parent, at the Canvas scale). See UIToLocal.
    Vec2 ToLocal(const Vec2& p) const { return UIToLocal(gameObject, ResolveAnchor(anchor, position, size), size, p); }
    /// Header rectangle origin (resolved against the anchor).
    Vec2 Origin() const { return ResolveAnchor(anchor, position, size); }

    bool HeaderContains(const Vec2& p) const {
        Vec2 o = Origin();
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    /// Search row of an open searchable list (not an option index).
    static constexpr int kSearchRow = -2;

    /// Which open option (0..N-1) the point is over, or -1 (also -1 when closed).
    /// A searchable list returns kSearchRow for the filter row.
    int OptionAt(const Vec2& p) const {
        if (!open) return -1;
        Vec2 o = Origin();
        if (p.x < o.x || p.x > o.x + size.x) return -1;
        float top = o.y + size.y;
        if (!ListExtras()) {
            for (int i = 0; i < (int)options.size(); ++i) {
                float y = top + i * size.y;
                if (p.y >= y && p.y <= y + size.y) return i;
            }
            return -1;
        }
        if (searchable) {
            if (p.y >= top && p.y <= top + size.y) return kSearchRow;
            top += size.y;
        }
        int slot = 0;
        const int cap = maxVisible > 0 ? maxVisible : 1000000;
        for (int i = 0; i < (int)options.size(); ++i) {
            if (!PassesFilter(i)) continue;
            if (slot >= cap) break;
            float y = top + slot * size.y;
            ++slot;
            if (p.y >= y && p.y <= y + size.y) return i;
        }
        return -1;
    }
    /// On-screen option row (0 = first match, after the search row), or -1.
    int ShownSlot(int optionIndex) const {
        if (optionIndex < 0 || optionIndex >= (int)options.size() || !PassesFilter(optionIndex)) return -1;
        int slot = 0;
        const int cap = maxVisible > 0 ? maxVisible : 1000000;
        for (int i = 0; i < (int)options.size(); ++i) {
            if (!PassesFilter(i)) continue;
            if (slot >= cap) return -1;
            if (i == optionIndex) return slot;
            ++slot;
        }
        return -1;
    }

    bool interactable = true;   // when false: greyed out, ignores clicks

    void Update(float) override {
        if (!interactable) { m_hover = false; m_hoverOption = -1; open = false; return; }
        Vec2 m = ToLocal(Input::MousePosition());   // header / option tests work in the widget's frame
        m_hover = HeaderContains(m);
        m_hoverOption = OptionAt(m);
        if (Input::GetMouseButtonDown(0)) {
            if (m_hover) {
                open = !open;                       // toggle the list
                if (!open) m_filter.clear();
            } else if (open && m_hoverOption == kSearchRow) {
                // The filter row stays open so the player can type.
            } else if (open && m_hoverOption >= 0) {
                SetValue(m_hoverOption);            // pick an option
                open = false;
                m_filter.clear();
            } else if (open) {
                open = false;                       // click outside closes
                m_filter.clear();
            }
        }
        // grok/ui-controls: typing filters a searchable list, unless a text field has focus.
        if (open && searchable && !FieldFocused()) {
            for (char c : Input::TypedText()) {
                if ((unsigned char)c >= 32 && m_filter.size() < 32) m_filter.push_back(c);
            }
            if (Input::GetKeyDown((char)8) || Input::GetKeyDown((char)127)) {
                if (!m_filter.empty()) m_filter.pop_back();
            }
        }
    }

private:
    bool PassesFilter(int i) const {
        if (i < 0 || i >= (int)options.size()) return false;
        if (m_filter.empty()) return true;
        auto lower = [](unsigned char c) { return (char)std::tolower(c); };
        const std::string& hay = options[(std::size_t)i];
        std::size_t n = m_filter.size();
        if (n > hay.size()) return false;
        for (std::size_t a = 0; a + n <= hay.size(); ++a) {
            bool ok = true;
            for (std::size_t b = 0; b < n; ++b)
                if (lower((unsigned char)hay[a + b]) != lower((unsigned char)m_filter[b])) { ok = false; break; }
            if (ok) return true;
        }
        return false;
    }
    bool FieldFocused() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return false;
        for (const auto& up : s->Objects())
            if (up->active)
                if (auto* in = up->GetComponent<UIInputField>())
                    if (in->focused) return true;
        return false;
    }
    void Fire() {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_change");
    }
    bool m_hover = false;
    bool m_focused = false;
    int  m_hoverOption = -1;
    std::string m_filter;   // runtime only; cleared when the list closes
};

} // namespace okay
