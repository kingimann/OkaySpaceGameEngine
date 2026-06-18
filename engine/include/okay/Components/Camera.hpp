#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"

namespace okay {

/// An orthographic 2D camera. The first one created registers itself as the
/// scene's main camera (mirroring Unity's "MainCamera" convention).
class Camera : public Component {
public:
    /// Half the vertical viewing volume, in world units.
    float orthographicSize = 5.0f;
    /// Color the framebuffer is cleared to each frame.
    Color backgroundColor = Color::Black;
    /// Whether this camera should become the scene's main camera on Awake.
    bool main = true;

    void Awake() override;
};

} // namespace okay
