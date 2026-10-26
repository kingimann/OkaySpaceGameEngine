#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Auto-layout for child UI widgets.
/// Arranges this object's child widgets in a column, a row or a grid starting at
/// `origin`, stacking them by their own sizes plus `spacing`. Combine with a
/// Scroll View to make scrollable lists/menus without hand-placing every element.
///
/// The Flex Row / Flex Column modes lay the children out inside a box of
/// `boxSize`: lines along the main axis (wrapping when `wrap` is on), spare room
/// handed out by `justify` and by each child's UIFlexItem grow/shrink, and the
/// children placed across the line by `align`. Flex layouts re-arrange every frame
/// (UpdateUILayouts), so they follow changes live; the other modes arrange when
/// asked (Start() does it once, and after adding/removing children).
class UILayoutGroup final : public Behaviour {
public:
    enum class Direction { Vertical, Horizontal, Grid, FlexRow, FlexColumn };
    enum class Justify { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
    enum class Align { Start, Center, End, Stretch };
    Direction direction = Direction::Vertical;
    UIAnchor  anchor = UIAnchor::TopLeft;
    Vec2      origin{20.0f, 20.0f};   // where the first child is placed
    float     spacing = 8.0f;         // gap between children (row gap in Grid)
    float     padding = 0.0f;         // extra inset before the first child
    int       columns = 3;            // Grid: children per row
    float     spacingY = 8.0f;        // Grid / Flex: gap between rows or lines (`spacing` is the main gap)
    // Flex modes only:
    Vec2      boxSize{400.0f, 300.0f}; // the flex container's size
    Justify   justify = Justify::Start; // spare room along a line
    Align     align = Align::Start;    // placement across a line
    bool      wrap = false;            // start a new line when the box is full

    bool IsFlex() const { return direction == Direction::FlexRow || direction == Direction::FlexColumn; }

    /// Position every child UI widget in order along the layout direction.
    void Arrange();
    /// The total length used along the direction after the last Arrange().
    float ContentSize() const { return m_contentSize; }

    void Start() override { Arrange(); }

private:
    float m_contentSize = 0.0f;
};

} // namespace okay
