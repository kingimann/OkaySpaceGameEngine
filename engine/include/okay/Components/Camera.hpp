#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"

namespace okay {

/// A camera supporting both 2D (orthographic) and 3D (perspective) projection.
/// The first one created registers itself as the scene's main camera (mirroring
/// Unity's "MainCamera" convention).
class Camera : public Component {
public:
    enum class Projection { Orthographic, Perspective };

    Projection projection = Projection::Orthographic;

    /// Half the vertical viewing volume, in world units (orthographic).
    float orthographicSize = 5.0f;
    /// Vertical field of view in degrees (perspective).
    float fieldOfView = 60.0f;
    float nearClip = 0.03f;
    float farClip  = 1000.0f;

    /// Color the framebuffer is cleared to each frame.
    Color backgroundColor = Color::Black;
    /// Whether this camera should become the scene's main camera on Awake.
    bool main = true;

    /// World-to-camera matrix (inverse of the Transform's world matrix).
    Mat4 ViewMatrix() const;
    /// Projection matrix for the given viewport aspect ratio.
    Mat4 ProjectionMatrix(float aspect) const;

    void Awake() override;
};

} // namespace okay
