#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Components/UITouchScroll.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

class GameObject;

/// A scrolling list that only builds the rows you can see, so a list of 10,000
/// entries costs the same as one of 10. Its first child is the row template
/// (any UI widgets: a button, a panel with text and an icon...). The template is
/// kept hidden; the list clones it into a small pool of rows and moves them as it
/// scrolls, rebinding each to the entry it now shows.
///
/// Binding a row to entry `i`:
///   * with `items` filled in, the row's first text (a screen-space Text, else a
///     button label) shows items[i];
///   * `onBind` (C++) is called with the row and its index;
///   * the list's script gets on_list_bind(), with Prefs "ui_list_index" holding
///     the index and "ui_list_row" the row object's name.
/// Clicking a row that carries a button selects it: `selected` changes and the
/// list's script gets on_list_select() with Prefs "ui_list_selected".
/// Pool rows are runtime-only (never saved). Rows are laid out top to bottom,
/// `itemHeight` tall with `spacing` between them; the wheel or a finger drag scrolls it.
class UIListView final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{300.0f, 240.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    Color background = Color::FromBytes(30, 33, 41, 230);
    Color barColor   = Color::FromBytes(255, 255, 255, 46);
    float itemHeight = 32.0f;
    float spacing = 2.0f;
    int   itemCount = 0;                // entries in the list (items.size() when items is used)
    std::vector<std::string> items;     // optional text per entry
    int   selected = -1;
    float scroll = 0.0f;
    float wheelSpeed = 30.0f;
    std::function<void(GameObject* row, int index)> onBind;

    float Stride() const { return itemHeight + spacing; }
    int   Count() const { return items.empty() ? itemCount : (int)items.size(); }
    float ContentHeight() const;
    float ScrollMax() const;
    void  SetScroll(float v);
    void  ScrollTo(int index);          // bring entry `index` into view
    /// Rebind every visible row now (after changing items / itemCount by hand).
    void  Refresh() { m_dirty = true; }
    /// Throw the pooled rows away and clone the template again (after editing it).
    void  Rebuild() { m_rebuild = true; }

    /// Build / move / rebind the pooled rows. Runs every frame from
    /// UpdateUILayouts (editor preview and game alike).
    void Layout();
    void Update(float dt) override;

    const std::vector<GameObject*>& Rows() const { return m_rows; }
    GameObject* Template() const;

private:
    UITouchScroll m_touch;   // finger drag-scrolling
    void Bind(GameObject* row, int index);
    std::vector<GameObject*> m_rows;
    std::vector<int> m_rowIndex;        // entry each pooled row shows (-1 = none)
    const GameObject* m_template = nullptr;
    bool m_dirty = true;
    bool m_rebuild = false;
};

} // namespace okay
