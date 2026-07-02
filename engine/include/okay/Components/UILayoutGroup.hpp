#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Auto-layout for child UI widgets (Unity's Vertical/Horizontal LayoutGroup).
/// Arranges this object's child widgets in a column or row starting at `origin`,
/// stacking them by their own sizes plus `spacing`. Combine with a Scroll View
/// to make scrollable lists/menus without hand-placing every element. Call
/// Arrange() after adding/removing children (Start() does it once).
class UILayoutGroup : public Behaviour {
public:
    enum class Direction { Vertical, Horizontal, Grid };
    Direction direction = Direction::Vertical;
    UIAnchor  anchor = UIAnchor::TopLeft;
    Vec2      origin{20.0f, 20.0f};   // where the first child is placed
    float     spacing = 8.0f;         // gap between children (row gap in Grid)
    float     padding = 0.0f;         // extra inset before the first child
    int       columns = 3;            // Grid: children per row
    float     spacingY = 8.0f;        // Grid: gap between rows (column gap uses `spacing`)

    /// Position every child UI widget in order along the layout direction.
    void Arrange();
    /// The total length used along the direction after the last Arrange().
    float ContentSize() const { return m_contentSize; }

    void Start() override { Arrange(); }

private:
    float m_contentSize = 0.0f;
};

} // namespace okay
