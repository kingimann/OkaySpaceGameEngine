#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>

namespace okay {

class GameObject;

/// Makes a world-space item (a sprite GameObject) draggable with the mouse at
/// runtime — the world equivalent of UIDraggable. Press on the sprite and move
/// to drag it around the scene (in 2D / orthographic camera space); release to
/// drop. On release, if the pointer is over another sprite that has a DropZone
/// (or any sprite when `anyTarget`), it counts as a drop:
///   * the dragged object's script gets on_drop()    — Prefs "drop_target"
///     holds the target's name.
///   * the target's script gets on_receive()         — Prefs "drop_source"
///     holds the dragged object's name.
/// While dragging, on_drag() fires each frame and on_drag_start() once. With
/// `snapToZone`, the item centers itself on the zone it lands on (instant
/// world-space inventory / slots). `returnToStart` snaps a missed drag back.
class Draggable : public Behaviour {
public:
    bool returnToStart = false;   // snap back if not dropped on a target
    bool anyTarget     = false;   // any sprite is a target (not just DropZones)
    bool snapToZone    = false;   // center the item on the zone it lands on
    /// Restrict movement to one axis (Both = free).
    enum class Axis { Both, Horizontal, Vertical };
    Axis  axis = Axis::Both;
    /// World units the pointer must move before a drag begins.
    float dragThreshold = 0.0f;
    /// Raise sortOrder while dragging so the item draws above the rest.
    bool  bringToFront = false;
    /// Snap the dragged position to a grid (world units). 0 = no snap on that
    /// axis. Great for board / tile / strategy games.
    float gridX = 0.0f;
    float gridY = 0.0f;
    /// Multiply the sprite's scale while dragging for a "lifted" look (1 = off).
    float dragScale = 1.0f;

    bool IsDragging() const { return m_dragging; }
    GameObject* LastDropTarget() const { return m_dropTarget; }

    void Update(float) override;   // implemented in Draggable.cpp

private:
    bool m_dragging = false;
    bool m_armed    = false;
    Vec3 m_start{};
    Vec2 m_press{};         // screen pixels where the press began
    Vec3 m_grab{};          // world offset from item center to cursor at grab
    int  m_savedOrder = 0;
    Vec3 m_savedScale{1, 1, 1};
    GameObject* m_dropTarget = nullptr;
};

/// A world-space sprite that can receive dropped Draggables (gets on_receive()).
/// With a non-empty `acceptTag`, only draggables whose GameObject tag matches
/// are accepted.
class DropZone : public Component {
public:
    std::string acceptTag;

    bool IsHovered() const { return m_hover; }
    /// Set the hover state; returns true if it changed (so the driver can fire
    /// on_hover_enter() / on_hover_exit() exactly on the transitions).
    bool SetHovered(bool v) { bool changed = (v != m_hover); m_hover = v; return changed; }

private:
    bool m_hover = false;
};

} // namespace okay
