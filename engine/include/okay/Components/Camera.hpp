#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"

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

    /// Physical camera (Unity's Physical Camera): derive the field of view from a
    /// real lens focal length + sensor size instead of an angle. Great for matching
    /// real cameras / cinematic framing. When on, `fieldOfView` is ignored.
    bool  physicalCamera = false;
    float focalLength  = 50.0f;   // mm
    float sensorHeight = 24.0f;   // mm (full-frame vertical); FOV = 2*atan(h/2f)

    /// Culling mask (Unity's Camera.cullingMask): a 32-bit set of layers this camera
    /// renders. A mesh is drawn only if bit (1 << gameObject->layer) is set. Default
    /// = all layers. Use it for mini-maps, hidden helper geometry, layered passes.
    int cullingMask = ~0;

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
    /// The effective vertical field of view (degrees) for a viewport aspect ratio,
    /// resolving physical-camera optics and the horizontal-axis option. Useful for UI.
    float VerticalFovDegrees(float aspect) const;

    /// Project a world point to screen pixels (origin top-left, +Y down). Returns
    /// false when the point is behind the camera; on success *out is the pixel
    /// position and *outDepth (if given) is the clip-space w (view depth) for
    /// distance-based scaling. Uses the same view*projection the renderer draws
    /// meshes with, so projected UI lines up with the 3D scene. Inline so UI code
    /// (player + editor) can project world-space widgets without extra plumbing.
    bool WorldToScreen(const Vec3& world, float screenW, float screenH,
                       Vec2& out, float* outDepth = nullptr) const {
        float aspect = screenH > 0.0f ? screenW / screenH : 1.0f;
        Vec4 clip = (ProjectionMatrix(aspect) * ViewMatrix()) * Vec4{world.x, world.y, world.z, 1.0f};
        if (clip.w <= 1e-5f) return false;                 // behind / on the camera plane
        float ndcX = clip.x / clip.w, ndcY = clip.y / clip.w;
        out = Vec2{(ndcX * 0.5f + 0.5f) * screenW, (1.0f - (ndcY * 0.5f + 0.5f)) * screenH};
        if (outDepth) *outDepth = clip.w;
        return true;
    }

    void Awake() override;
};

} // namespace okay
