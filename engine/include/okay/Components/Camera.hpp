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
    float farClip  = 4000.0f; // generous default so large voxel worlds / open terrain stay visible to the horizon

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
    Color backgroundColor = Color::FromBytes(49, 77, 121);   // Unity-like blue-grey (was black: 2D scenes started dark)
    /// Render/priority order: the highest-depth camera wins "main" (Unity-like).
    float depth = 0.0f;
    /// Whether this camera should become the scene's main camera on Awake.
    bool main = true;

    // ---- Framing & effects --------------------------------------------------
    // Applied inside ViewMatrix / ProjectionMatrix (and RenderPosition /
    // EffectiveOrthoSize for the 2D path), so every renderer, the Game view and
    // built games use them with no extra work.
    /// Zoom multiplier: 2 = twice as close (narrower FOV / half the orthographic size).
    float zoom = 1.0f;
    /// Dutch angle: roll around the view direction, in degrees (3D).
    float dutch = 0.0f;
    /// Lens shift: slide the image without turning the camera, as a fraction of
    /// the view (0.5 = half a screen) — architecture shots, off-centre framing (3D).
    float lensShiftX = 0.0f, lensShiftY = 0.0f;
    /// Pixel perfect (2D): snap the camera to whole pixels at `pixelsPerUnit`, so
    /// pixel art doesn't shimmer as the camera moves.
    bool  pixelPerfect = false;
    float pixelsPerUnit = 32.0f;

    /// Camera shake (trauma-based): Shake() adds trauma; the camera jitters with
    /// smooth noise scaled by trauma^2 and settles at `shakeRecovery` per second.
    float shakeAmount = 0.3f;       // max position offset at full shake (world units)
    float shakeAngle = 2.0f;        // max rotation at full shake (degrees, 3D)
    float shakeFrequency = 18.0f;   // jitter speed (Hz)
    float shakeRecovery = 1.5f;     // trauma lost per second
    /// Add shake (0..1; stacks up to 1). Explosions ~0.6, hits ~0.3, footsteps ~0.1.
    void Shake(float strength = 0.5f);
    float ShakeTrauma() const { return m_trauma; }
    /// Smoothly change `zoom` to `target` over `seconds` (0 = instantly).
    void ZoomTo(float target, float seconds);

    /// Orthographic half-height after zoom (what the 2D renderers draw with).
    float EffectiveOrthoSize() const;
    /// Where the camera renders from: its position plus shake and pixel snapping.
    Vec3 RenderPosition() const;
    /// Camera-to-world matrix including dutch, shake and pixel snapping.
    Mat4 EyeMatrix() const;

    /// Optional object this camera does NOT render (e.g. a first-person player's
    /// own body): the mesh stays in the scene — visible to other cameras and the
    /// Scene view, and it still casts shadows — this camera just skips drawing it.
    /// Transient (set at runtime by controllers; not serialized).
    GameObject* ignoreObject = nullptr;

    /// Build a world-space picking ray through a screen pixel (origin top-left,
    /// +Y down) — Unity's Camera.ScreenPointToRay, for click-to-select / shooting.
    /// `outOrigin`/`outDir` (dir normalized) are set from this camera's transform and
    /// lens; works for both perspective and orthographic.
    void ScreenPointToRay(float screenX, float screenY, float screenW, float screenH,
                          Vec3& outOrigin, Vec3& outDir) const;
    /// World point at screen pixel (screenX,screenY) and `viewDepth` world units in
    /// front of the camera (Unity's Camera.ScreenToWorldPoint with z = distance).
    Vec3 ScreenToWorldPoint(float screenX, float screenY, float viewDepth,
                            float screenW, float screenH) const;

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

    /// World point to viewport coordinates (Unity's WorldToViewportPoint): x,y in
    /// 0..1 with the ORIGIN BOTTOM-LEFT (+Y up); *outDepth is the view depth. Returns
    /// false when the point is behind the camera.
    bool WorldToViewportPoint(const Vec3& world, float screenW, float screenH,
                              Vec2& out, float* outDepth = nullptr) const {
        Vec2 px; float d = 0.0f;
        if (!WorldToScreen(world, screenW, screenH, px, &d)) return false;
        out = Vec2{screenW > 0.0f ? px.x / screenW : 0.0f,
                   screenH > 0.0f ? 1.0f - px.y / screenH : 0.0f};   // flip to +Y up
        if (outDepth) *outDepth = d;
        return true;
    }

    /// True if a world point is in front of the camera AND inside the viewport rect —
    /// a quick on-screen test for culling labels, spawning, off-screen indicators.
    bool IsPointVisible(const Vec3& world, float screenW, float screenH) const {
        Vec2 vp; float d = 0.0f;
        if (!WorldToViewportPoint(world, screenW, screenH, vp, &d)) return false;
        return d > 0.0f && vp.x >= 0.0f && vp.x <= 1.0f && vp.y >= 0.0f && vp.y <= 1.0f;
    }

    /// Picking ray from a viewport point (0..1, origin bottom-left) — the viewport
    /// twin of ScreenPointToRay.
    void ViewportPointToRay(float vx, float vy, float screenW, float screenH,
                            Vec3& outOrigin, Vec3& outDir) const {
        ScreenPointToRay(vx * screenW, (1.0f - vy) * screenH, screenW, screenH, outOrigin, outDir);
    }

    void Awake() override;
    void LateUpdate(float deltaTime) override;

private:
    float m_trauma = 0.0f, m_shakeTime = 0.0f;
    Vec3  m_shakePos{0, 0, 0}, m_shakeRot{0, 0, 0};   // camera-local offset / euler degrees
    float m_zoomFrom = 1.0f, m_zoomTarget = 1.0f, m_zoomTime = 0.0f, m_zoomDur = 0.0f;
};

} // namespace okay
