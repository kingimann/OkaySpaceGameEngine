#pragma once
#include "okay/Scene/Component.hpp"

namespace okay {

/// Marker that turns the UI widget on this object into a standalone in-world (3D)
/// element: it renders at the object's Transform position (billboarded toward the
/// camera by default) instead of on the screen. Each widget is its own GameObject
/// you place and move in the 3D world — no Canvas required. The widget's anchor and
/// pixel offset still apply (relative to its own size, around its 3D point), so
/// nothing about the widget is removed or converted.
class WorldSpaceUI : public Component {
public:
    float pixelsPerUnit = 100.0f;   // design px per world unit (bigger = smaller in-world)
    bool  billboard = true;         // always face the camera (else use the object's orientation)
    /// Constant screen size: when true the widget keeps a fixed on-screen size no
    /// matter how near/far the camera is (it still moves in 3D, but doesn't shrink
    /// or grow with distance). `constantScale` multiplies that fixed size — 1.0
    /// draws the widget at its authored design pixels. When false, the widget
    /// scales with distance like a real object (pixelsPerUnit decides how big).
    bool  constantSize = false;
    float constantScale = 1.0f;
};

} // namespace okay
