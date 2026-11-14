#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {

/// A path of crumbs, such as Home / Library / Books. Choosing a crumb keeps
/// that folder and drops the ones after it. on_change() runs when the path changes.
class UIBreadcrumbs final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{420.0f, 36.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    /// Folders separated by /.
    std::string pathText = "Home/Library/Books";
    /// Which crumb is highlighted. Choosing it shortens the path to here.
    int value = 2;
    bool interactable = true;
    bool focusable = true;
    Color background = Color::FromBytes(28, 32, 44);
    Color textColor = Color::FromBytes(210, 216, 228);
    Color accent = Color::FromBytes(90, 150, 230);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    std::vector<std::string> Segments() const {
        std::vector<std::string> out;
        std::string cur;
        auto push = [&]() {
            const std::string t = UIWidgetTrim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        };
        for (char c : pathText) {
            if (c == '/' || c == '\\') push();
            else cur.push_back(c);
        }
        push();
        return out;
    }

    std::string Path() const {
        const auto s = Segments();
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i) out += '/';
            out += s[i];
        }
        return out;
    }

    void SetPath(const std::string& path) {
        const std::string next = Join(SplitRaw(path));
        if (next == Path()) { pathText = next; return; }
        pathText = next;
        const int n = (int)Segments().size();
        if (n <= 0) value = 0;
        else if (value >= n) value = n - 1;
        else if (value < 0) value = 0;
        FireUIEvent(gameObject, "on_change");
    }

    /// Keep crumbs 0..index and drop the rest.
    void Select(int index) {
        auto s = Segments();
        if (s.empty()) return;
        if (index < 0) index = 0;
        if (index >= (int)s.size()) index = (int)s.size() - 1;
        s.resize((std::size_t)index + 1);
        const std::string next = Join(s);
        const bool changed = next != Path() || value != index;
        pathText = next;
        value = index;
        if (changed) FireUIEvent(gameObject, "on_change");
    }

    /// Move the highlight without shortening the path.
    void Move(int dir) {
        const int n = (int)Segments().size();
        if (n <= 0) return;
        int next = value + dir;
        if (next < 0) next = 0;
        if (next >= n) next = n - 1;
        value = next;
    }

    /// Left edge of each crumb, and its width, in widget pixels.
    struct CrumbBox { float x, w; };
    std::vector<CrumbBox> Layout() const {
        std::vector<CrumbBox> boxes;
        const auto s = Segments();
        float x = 8.0f;
        const float cw = UIWidgetCharW();
        for (std::size_t i = 0; i < s.size(); ++i) {
            const float w = 8.0f + cw * (float)s[i].size();
            boxes.push_back({x, w});
            x += w;
            if (i + 1 < s.size()) x += cw * 3.0f;   // " / "
        }
        return boxes;
    }

    void PressLocal(const Vec2& local) {
        if (!interactable) return;
        const auto boxes = Layout();
        for (int i = 0; i < (int)boxes.size(); ++i)
            if (local.x >= boxes[(std::size_t)i].x && local.x <= boxes[(std::size_t)i].x + boxes[(std::size_t)i].w) {
                Select(i);
                return;
            }
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        Vec2 local;
        if (UIWidgetPointerLocal(gameObject, anchor, position, size, local)) PressLocal(local);
    }

private:
    static std::vector<std::string> SplitRaw(const std::string& path) {
        UIBreadcrumbs tmp;
        tmp.pathText = path;
        return tmp.Segments();
    }
    static std::string Join(const std::vector<std::string>& s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i) out += '/';
            out += s[i];
        }
        return out;
    }
    bool m_focused = false;
};

} // namespace okay
