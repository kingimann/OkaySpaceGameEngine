#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Render/Color.hpp"
#include <string>

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
    bool snapToSlot = false;      // center the item in the slot it's dropped on
                                  // (a one-checkbox inventory — no script needed)
    /// Restrict movement to one axis (Both = free).
    enum class Axis { Both, Horizontal, Vertical };
    Axis axis = Axis::Both;
    /// Pixels the pointer must move before a drag begins (so a click that barely
    /// moves doesn't drag). 0 = start immediately.
    float dragThreshold = 0.0f;
    /// Draw on top of its siblings while being dragged.
    bool bringToFront = false;

    bool IsDragging() const { return m_dragging; }
    GameObject* LastDropTarget() const { return m_dropTarget; }

    void Update(float) override;   // implemented in UIDraggable.cpp

private:
    bool m_dragging = false;
    bool m_armed = false;      // pressed, waiting to pass the drag threshold
    Vec2 m_start{};
    Vec2 m_press{};
    Vec2 m_prevMouse{};
    GameObject* m_dropTarget = nullptr;
};

/// A widget that can receive dropped UIDraggables (gets on_receive()). With a
/// non-empty `acceptTag`, only draggables whose GameObject tag matches are
/// accepted (e.g. a weapon slot that takes only "weapon" items). While a
/// compatible draggable hovers over it during a drag, `highlight` is painted
/// over the slot for feedback (enable with `showHighlight`).
class UIDropTarget : public Component {
public:
    std::string acceptTag;
    bool  showHighlight = true;                  // tint the slot while a valid item hovers
    Color highlight = {1.0f, 1.0f, 1.0f, 0.25f}; // overlay color

    bool IsHovered() const { return m_hover; }
    void SetHovered(bool v) { m_hover = v; }

private:
    bool m_hover = false;
};

} // namespace okay
