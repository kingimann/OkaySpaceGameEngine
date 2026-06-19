#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

class GameObject;

/// Makes its sibling UI widget draggable at runtime: press on it and move to
/// drag it around (it follows the cursor, respecting the Canvas scale); release
/// to drop. On release, if the pointer is over another UI widget that has a
/// UIDropTarget (or any widget when `anyTarget`), it counts as a drop:
///   * the dragged object's script gets on_drop()   — Prefs "ui_drop_target"
///     holds the target's name.
///   * the target's script gets on_receive()        — Prefs "ui_drop_source"
///     holds the dragged object's name.
/// While dragging, on_drag() fires each frame and on_drag_start() once. With
/// `returnToStart`, a drag that doesn't land on a target snaps back.
class UIDraggable : public Behaviour {
public:
    bool returnToStart = false;   // snap back if not dropped on a target
    bool anyTarget = false;       // any widget is a drop target (not just UIDropTarget)

    bool IsDragging() const { return m_dragging; }
    GameObject* LastDropTarget() const { return m_dropTarget; }

    void Update(float) override;   // implemented in UIDraggable.cpp

private:
    bool m_dragging = false;
    Vec2 m_start{};
    Vec2 m_prevMouse{};
    GameObject* m_dropTarget = nullptr;
};

/// A marker that a widget can receive dropped UIDraggables (gets on_receive()).
class UIDropTarget : public Component {};

} // namespace okay
