#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Utf8.hpp"
#include <vector>

namespace okay {

/// A search field over a list of items. Typing filters the list to items that
/// contain the query. An empty query shows every item. The sibling script's
/// on_change() runs when the query or the chosen row changes, and on_submit()
/// runs when Enter confirms the query.
class UISearchBar final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{320.0f, 180.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    std::string query;
    /// One item per line.
    std::string itemsText = "Apple\nApricot\nBanana";
    std::string placeholder = "Search";
    bool caseSensitive = false;
    int maxLength = 64;
    /// Index into Items() of the chosen row, or -1.
    int value = -1;
    bool editing = false;
    bool interactable = true;
    bool focusable = true;
    Color background = Color::FromBytes(28, 32, 44);
    Color field = Color::FromBytes(18, 20, 28);
    Color textColor = Color::White;
    Color muted = Color::FromBytes(150, 158, 176);
    Color accent = Color::FromBytes(90, 150, 230);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;
    float fieldHeight = 36.0f;
    float rowHeight = 28.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; if (!f) editing = false; }

    std::vector<std::string> Items() const { return SplitLines(itemsText); }

    bool ItemMatches(const std::string& item) const {
        const std::string needle = UIWidgetTrim(query);
        if (needle.empty()) return true;
        const std::string hay = caseSensitive ? item : UIWidgetLower(item);
        const std::string n = caseSensitive ? needle : UIWidgetLower(needle);
        return hay.find(n) != std::string::npos;
    }

    /// Items that match the current query, in list order.
    std::vector<std::string> Matches() const {
        std::vector<std::string> out;
        for (const std::string& item : Items())
            if (ItemMatches(item)) out.push_back(item);
        return out;
    }
    /// Indexes into Items() for each match.
    std::vector<int> MatchIndexes() const {
        std::vector<int> out;
        const auto items = Items();
        for (int i = 0; i < (int)items.size(); ++i)
            if (ItemMatches(items[(std::size_t)i])) out.push_back(i);
        return out;
    }
    int MatchCount() const { return (int)MatchIndexes().size(); }

    std::string SelectedText() const {
        const auto items = Items();
        if (value < 0 || value >= (int)items.size()) return {};
        return items[(std::size_t)value];
    }

    void SetQuery(std::string q) {
        if ((int)utf8::Length(q) > maxLength) {
            // Drop whole characters off the end so a limit never splits one.
            const char* begin = q.data();
            const char* p = begin;
            const char* end = begin + q.size();
            int n = 0;
            const char* cut = end;
            while (p < end && n < maxLength) { utf8::Next(p, end); ++n; }
            if (n >= maxLength) cut = p;
            q.resize((std::size_t)(cut - begin));
        }
        if (q == query) return;
        query = std::move(q);
        if (value >= 0) {
            const auto items = Items();
            if (value >= (int)items.size() || !ItemMatches(items[(std::size_t)value])) value = -1;
        }
        FireUIEvent(gameObject, "on_change");
    }
    void Clear() {
        const bool had = !query.empty() || value != -1;
        query.clear();
        value = -1;
        if (had) FireUIEvent(gameObject, "on_change");
    }

    /// Choose a row by its index in Items(). Fires on_change when it changes.
    void Select(int index) {
        const auto items = Items();
        if (index < 0 || index >= (int)items.size()) return;
        if (!ItemMatches(items[(std::size_t)index])) return;
        if (value == index) return;
        value = index;
        FireUIEvent(gameObject, "on_change");
    }

    /// A point in pixels from the widget's top-left.
    void PressLocal(const Vec2& local) {
        if (!interactable) return;
        const float fh = FieldH();
        if (local.y <= fh) {
            const bool onClear = !query.empty() && local.x >= size.x - 32.0f;
            if (onClear) Clear();
            else { editing = true; m_focused = true; }
            return;
        }
        const int row = (int)((local.y - fh - 4.0f) / rowHeight);
        const auto idx = MatchIndexes();
        if (row >= 0 && row < (int)idx.size()) Select(idx[(std::size_t)row]);
    }

    float FieldH() const { return fieldHeight < size.y ? fieldHeight : size.y; }

    void Update(float) override {
        if (!enabled || !interactable) return;
        Vec2 local;
        if (UIWidgetPointerLocal(gameObject, anchor, position, size, local)) PressLocal(local);
        if (!editing) return;
        const std::string before = query;
        bool changed = TypeText(Input::TypedText());
        if (Input::GetKeyDown((char)8) || Input::GetKeyDown((char)127))
            if (Backspace()) changed = true;
        if (changed) {
            std::string now = std::move(query);
            query = before;
            SetQuery(std::move(now));
        }
        if (Input::GetKeyDown('\r') || Input::GetKeyDown('\n')) {
            editing = false;
            FireUIEvent(gameObject, "on_submit");
        } else if (Input::GetKeyDown((char)27)) editing = false;
    }

private:
    static std::vector<std::string> SplitLines(const std::string& text) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : text) {
            if (c == '\n' || c == '\r') {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            } else cur.push_back(c);
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }
    bool TypeText(const std::string& typed) {
        bool changed = false;
        const char* p = typed.data();
        const char* end = p + typed.size();
        std::size_t length = utf8::Length(query);
        while (p < end) {
            const char* start = p;
            const std::uint32_t cp = utf8::Next(p, end);
            if ((int)length >= maxLength) break;
            if (cp < 32 || cp == 127 || cp == utf8::kReplacement) continue;
            query.append(start, (std::size_t)(p - start));
            ++length;
            changed = true;
        }
        return changed;
    }
    bool Backspace() {
        if (query.empty()) return false;
        std::size_t i = query.size() - 1;
        while (i > 0 && ((unsigned char)query[i] & 0xC0) == 0x80) --i;
        query.erase(i);
        return true;
    }
    bool m_focused = false;
};

} // namespace okay
