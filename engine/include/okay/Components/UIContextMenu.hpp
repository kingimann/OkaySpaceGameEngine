#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

class GameObject;

/// A right-click menu. Right-clicking the widget on the same object (or, with
/// `anywhere`, any spot no other context menu claims) opens `items` at the cursor;
/// clicking one picks it and closes the menu, as do a click outside it and Escape.
/// Picking fires `onSelect` (C++) and the object's script on_context_select(), with
/// Prefs "ui_context_index" and "ui_context_item" holding the choice. An item
/// written "-" is a separator. The menu draws on top of all UI, in screen pixels.
class UIContextMenu final : public Behaviour {
public:
    std::vector<std::string> items{"Copy", "Paste", "-", "Delete"};
    bool  anywhere = false;
    float itemHeight = 26.0f;
    float width = 180.0f;
    float textSize = 2.0f;          // text pixel size
    Color background = Color::FromBytes(36, 40, 52, 245);
    Color hoverColor = Color::FromBytes(70, 110, 200, 255);
    Color textColor  = Color::FromBytes(235, 238, 245, 255);
    Color borderColor = Color::FromBytes(255, 255, 255, 40);
    std::function<void(int index, const std::string& item)> onSelect;

    bool IsOpen() const { return m_open; }
    /// Open at screen point `at` (kept inside the screen of w x h).
    void OpenAt(const Vec2& at, float screenW, float screenH);
    void Close() { m_open = false; m_hover = -1; }
    /// Menu rect in screen pixels while open.
    Vec2 Origin() const { return m_at; }
    Vec2 Size() const { return Vec2{width, itemHeight * (float)items.size()}; }
    /// The item under screen point `p` (-1 = none, or a separator).
    int ItemAt(const Vec2& p) const;
    int Hover() const { return m_hover; }

    void Update(float dt) override;

    /// The open menu containing screen point `p`, if any (it takes the pointer).
    static UIContextMenu* OpenMenuAt(GameObject* anyInScene, const Vec2& p);

private:
    void Pick(int index);
    bool m_open = false;
    Vec2 m_at{0.0f, 0.0f};
    int  m_hover = -1;
    int  m_openedFrame = 0;   // the right-click that opened it doesn't also close it
};

} // namespace okay
