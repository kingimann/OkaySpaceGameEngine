#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>
#include <algorithm>

namespace okay {

/// Previous, page numbers, and next. The page stays inside 1 .. page count.
/// on_change() runs when the page changes.
class UIPagination final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{360.0f, 36.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    int page = 1;
    int pageCount = 1;
    int pageSize = 10;
    int totalItems = 0;
    bool interactable = true;
    bool focusable = true;
    Color background = Color::FromBytes(28, 32, 44);
    Color textColor = Color::White;
    Color accent = Color::FromBytes(90, 150, 230);
    Color muted = Color::FromBytes(150, 158, 176);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    int Page() const { return page; }
    int PageCount() const { return pageCount < 1 ? 1 : pageCount; }

    void SetPage(int p) {
        const int n = PageCount();
        if (p < 1) p = 1;
        if (p > n) p = n;
        if (p == page) return;
        page = p;
        FireUIEvent(gameObject, "on_change");
    }
    void Next() { SetPage(page + 1); }
    void Previous() { SetPage(page - 1); }

    /// page count becomes enough pages for `items` rows of `perPage`.
    void SetTotal(int items, int perPage) {
        pageSize = perPage < 1 ? 1 : perPage;
        totalItems = items < 0 ? 0 : items;
        const int pages = totalItems == 0 ? 1 : (totalItems + pageSize - 1) / pageSize;
        pageCount = pages < 1 ? 1 : pages;
        const int was = page;
        if (page < 1) page = 1;
        if (page > pageCount) page = pageCount;
        if (page != was) FireUIEvent(gameObject, "on_change");
    }

    /// Page numbers to draw. 0 is an ellipsis, which does not select a page.
    std::vector<int> VisiblePages() const {
        const int n = PageCount();
        int p = page;
        if (p < 1) p = 1;
        if (p > n) p = n;
        std::vector<int> v;
        if (n <= 7) {
            for (int i = 1; i <= n; ++i) v.push_back(i);
            return v;
        }
        v.push_back(1);
        int start = p - 1, end = p + 1;
        if (start < 2) start = 2;
        if (end > n - 1) end = n - 1;
        if (start > 2) v.push_back(0);
        for (int i = start; i <= end; ++i) v.push_back(i);
        if (end < n - 1) v.push_back(0);
        v.push_back(n);
        return v;
    }

    struct Slot { float x, w; int page; };   // page 0 = ellipsis, -1 = prev, -2 = next
    std::vector<Slot> Slots() const {
        std::vector<Slot> s;
        const float edge = 64.0f;
        s.push_back({0.0f, edge, -1});
        s.push_back({size.x - edge, edge, -2});
        const auto pages = VisiblePages();
        const float inner = std::max(0.0f, size.x - edge * 2.0f);
        const float w = pages.empty() ? inner : inner / (float)pages.size();
        for (int i = 0; i < (int)pages.size(); ++i)
            s.push_back({edge + w * (float)i, w, pages[(std::size_t)i]});
        return s;
    }

    void PressLocal(const Vec2& local) {
        if (!interactable) return;
        for (const Slot& s : Slots()) {
            if (local.x < s.x || local.x > s.x + s.w) continue;
            if (s.page == -1) Previous();
            else if (s.page == -2) Next();
            else if (s.page > 0) SetPage(s.page);
            return;
        }
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        Vec2 local;
        if (UIWidgetPointerLocal(gameObject, anchor, position, size, local)) PressLocal(local);
    }

private:
    bool m_focused = false;
};

} // namespace okay
