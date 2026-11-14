#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {

/// Stacked sections. A click on a heading opens or closes that section.
/// With "several open" off, opening one closes the others. on_change() runs
/// when a section opens or closes.
class UIAccordion final : public Behaviour {
public:
    struct Section {
        std::string title;
        std::string body;
        bool open = false;
    };

    Vec2 position{20.0f, 20.0f};
    Vec2 size{320.0f, 220.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    /// One section per line: Title | Body
    std::string sectionsText = "General | Sound, picture and controls\nAdvanced | Extra options";
    /// 1 = open, 0 = closed, separated by commas. One flag per section.
    std::string openText = "1,0";
    bool allowMultiple = false;
    /// Last section that was opened or closed.
    int value = 0;
    bool interactable = true;
    bool focusable = true;
    Color background = Color::FromBytes(28, 32, 44);
    Color header = Color::FromBytes(46, 54, 72);
    Color textColor = Color::White;
    Color muted = Color::FromBytes(170, 178, 194);
    Color accent = Color::FromBytes(90, 150, 230);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;
    float headerHeight = 32.0f;
    float bodyHeight = 40.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    std::vector<Section> Sections() const {
        std::vector<Section> out;
        std::string line;
        auto push = [&]() {
            if (line.empty()) return;
            Section s;
            const auto bar = line.find('|');
            if (bar == std::string::npos) s.title = UIWidgetTrim(line);
            else {
                s.title = UIWidgetTrim(line.substr(0, bar));
                s.body = UIWidgetTrim(line.substr(bar + 1));
            }
            out.push_back(s);
            line.clear();
        };
        for (char c : sectionsText) {
            if (c == '\n' || c == '\r') push();
            else line.push_back(c);
        }
        push();
        std::vector<int> flags;
        std::string num;
        auto pushNum = [&]() {
            if (num.empty()) return;
            flags.push_back(num[0] == '1' ? 1 : 0);
            num.clear();
        };
        for (char c : openText) {
            if (c == ',' || c == ' ') pushNum();
            else num.push_back(c);
        }
        pushNum();
        for (int i = 0; i < (int)out.size(); ++i)
            out[(std::size_t)i].open = i < (int)flags.size() && flags[(std::size_t)i] != 0;
        return out;
    }

    int Count() const { return (int)Sections().size(); }
    bool IsExpanded(int index) const {
        const auto s = Sections();
        return index >= 0 && index < (int)s.size() && s[(std::size_t)index].open;
    }

    void SetExpanded(int index, bool on) {
        auto s = Sections();
        if (index < 0 || index >= (int)s.size()) return;
        bool changed = s[(std::size_t)index].open != on;
        if (on && !allowMultiple) {
            for (int i = 0; i < (int)s.size(); ++i) {
                if (i == index) continue;
                if (s[(std::size_t)i].open) { s[(std::size_t)i].open = false; changed = true; }
            }
        }
        s[(std::size_t)index].open = on;
        value = index;
        if (!changed) return;
        Store(s);
        FireUIEvent(gameObject, "on_change");
    }
    void Toggle(int index) { SetExpanded(index, !IsExpanded(index)); }

    /// Move the highlighted heading, used by the keyboard and the gamepad.
    void FocusStep(int dir) {
        const int n = Count();
        if (n <= 0) return;
        int next = value + dir;
        if (next < 0) next = 0;
        if (next >= n) next = n - 1;
        value = next;
    }

    void PressLocal(const Vec2& local) {
        if (!interactable) return;
        const int index = HeaderAt(local.y);
        if (index >= 0) Toggle(index);
    }

    /// Heading index under a y measured from the top of the widget, or -1.
    int HeaderAt(float y) const {
        const auto s = Sections();
        float top = 0.0f;
        for (int i = 0; i < (int)s.size(); ++i) {
            if (top + headerHeight > size.y) break;
            if (y >= top && y < top + headerHeight) return i;
            top += headerHeight;
            if (s[(std::size_t)i].open) top += bodyHeight;
        }
        return -1;
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        Vec2 local;
        if (UIWidgetPointerLocal(gameObject, anchor, position, size, local)) PressLocal(local);
    }

private:
    void Store(const std::vector<Section>& s) {
        sectionsText.clear();
        openText.clear();
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i) { sectionsText += '\n'; openText += ','; }
            sectionsText += s[i].title;
            sectionsText += " | ";
            sectionsText += s[i].body;
            openText += s[i].open ? '1' : '0';
        }
    }
    bool m_focused = false;
};

} // namespace okay
