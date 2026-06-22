#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"

namespace okay {

class GameObject;

/// A camera supporting both 2D (orthographic) and 3D (perspective) projection.
/// The first one created registers itself as the scene's main camera (mirroring
/// Unity's "MainCamera" convention).
class Camera : public Component {
public:
    enum class Projection { Orthographic, Perspective };
    /// How the background is filled each frame (Unity's Clear Flags).
    enum class ClearFlags { Skybox, SolidColor };

    Projection projection = Projection::Perspective;   // Unity default (2D scenes set Orthographic)

    /// Half the vertical viewing volume, in world units (orthographic).
    float orthographicSize = 5.0f;
    /// Field of view in degrees (perspective). Measured along the axis set by
    /// `fovAxisHorizontal` (Unity's FOV Axis): vertical by default.
    float fieldOfView = 60.0f;
    bool  fovAxisHorizontal = false;   // false = vertical FOV (Unity default), true = horizontal
    float nearClip = 0.3f;   // Unity-like near plane
    float farClip  = 1000.0f;

    /// Normalized viewport rectangle (Unity's Camera.rect): where on the screen
    /// this camera draws — x,y = bottom-left, w,h = size, all in 0..1. Defaults to
    /// the full screen. Use it for split-screen, mini-maps, or picture-in-picture.
    float rectX = 0.0f, rectY = 0.0f, rectW = 1.0f, rectH = 1.0f;

    /// Background fill mode and the color used when SolidColor.
    ClearFlags clearFlags = ClearFlags::Skybox;
    /// Color the framebuffer is cleared to each frame.
    Color backgroundColor = Color::Black;
    /// Render/priority order: the highest-depth camera wins "main" (Unity-like).
    float depth = 0.0f;
    /// Whether this camera should become the scene's main camera on Awake.
    bool main = true;

    /// Optional object this camera does NOT render (e.g. a first-person player's
    /// own body): the mesh stays in the scene — visible to other cameras and the
    /// Scene view, and it still casts shadows — this camera just skips drawing it.
    /// Transient (set at runtime by controllers; not serialized).
    GameObject* ignoreObject = nullptr;

    /// World-to-camera matrix (inverse of the Transform's world matrix).
    Mat4 ViewMatrix() const;
    /// Projection matrix for the given viewport aspect ratio.
    Mat4 ProjectionMatrix(float aspect) const;

    void Awake() override;
};

} // namespace okay
