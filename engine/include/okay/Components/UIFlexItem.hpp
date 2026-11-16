#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// How a child of a flex layout (UILayoutGroup in Flex Row / Flex Column mode) is
/// sized. Children without one keep their own size and are only positioned.
class UIFlexItem final : public Behaviour {
public:
    enum class Align { Auto, Start, Center, End, Stretch };
    float grow = 0.0f;     // share of the line's spare room this item takes (0 = none)
    float shrink = 1.0f;   // share of an overflow it gives up (0 = never shrinks)
    /// The widget's size before growing, shrinking or stretching (the main-axis
    /// part is the flex basis). 0 = take the widget's size the first time it is
    /// laid out. A widget that is 0 long then has a basis of 0, kept as -0 so it is
    /// not taken again from the size the layout wrote (it saves as "-0").
    Vec2  baseSize{0.0f, 0.0f};
    Align alignSelf = Align::Auto;   // Auto = the layout's alignment
};

} // namespace okay
