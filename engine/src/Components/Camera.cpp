#include "okay/Components/Camera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/EnsureCamera.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include <cmath>

namespace okay {

void Camera::Awake() {
    if (!main) return;
    Scene* s = GetScene();
    if (!s) return;
    // A main camera taking over must outrank the current one: a lower-depth camera
    // waking up later doesn't steal the view (Scene::ResolveMainCamera agrees).
    Camera* cur = s->mainCamera;
    const bool takeOver = !cur || cur == this || !CameraUsable(cur) || !cur->main ||
                          (CameraUsable(this) && depth >= cur->depth);
    if (takeOver) s->mainCamera = this;
}

void Camera::ScreenPointToRay(float screenX, float screenY, float screenW, float screenH,
                              Vec3& outOrigin, Vec3& outDir) const {
    // Unproject through the same view * projection the renderers use, so picking
    // stays exact with zoom, dutch, lens shift and shake. Pixels are mapped
    // through the viewport rect; a full-screen rect is the whole window.
    float rx, ry, rw, rh;
    ScreenRect(screenW, screenH, rx, ry, rw, rh);
    float aspect = rh > 0.0f ? rw / rh : 1.0f;
    const float lx = rw > 0.0f ? (screenX - rx) / rw : 0.5f;
    const float ly = rh > 0.0f ? (screenY - ry) / rh : 0.5f;
    float ndcX = 2.0f * lx - 1.0f;
    float ndcY = 1.0f - 2.0f * ly;   // +Y up
    const Mat4 inv = (ProjectionMatrix(aspect) * ViewMatrix()).Inverse();
    auto unproject = [&](float z) {
        Vec4 p = inv * Vec4{ndcX, ndcY, z, 1.0f};
        const float w = std::fabs(p.w) > 1e-8f ? p.w : 1e-8f;
        return Vec3{p.x / w, p.y / w, p.z / w};
    };
    const Vec3 nearPt = unproject(-1.0f), farPt = unproject(1.0f);
    if (projection == Projection::Perspective) {
        outOrigin = RenderPosition();
        outDir = (farPt - outOrigin).Normalized();
    } else {
        // Orthographic: parallel rays starting on the camera's own plane (the
        // ortho volume reaches farClip behind the camera, see ProjectionMatrix).
        outDir = (farPt - nearPt).Normalized();
        outOrigin = nearPt + outDir * farClip;
    }
}

Vec3 Camera::ScreenToWorldPoint(float screenX, float screenY, float viewDepth,
                                float screenW, float screenH) const {
    Vec3 o, d;
    ScreenPointToRay(screenX, screenY, screenW, screenH, o, d);
    if (projection == Projection::Perspective) {
        const Mat4 eye = EyeMatrix();
        Vec3 fwd = Vec3{-eye.at(2, 0), -eye.at(2, 1), -eye.at(2, 2)}.Normalized();   // cameras look down -Z
        float along = Vec3::Dot(d, fwd);                      // ray foreshortening
        float t = (std::fabs(along) > 1e-5f) ? viewDepth / along : viewDepth;
        return o + d * t;                                     // hits the plane viewDepth in front
    }
    return o + d * viewDepth;
}

Mat4 Camera::EyeMatrix() const {
    if (!transform) return Mat4{};
    Mat4 m = transform->LocalToWorldMatrix();
    const Vec3 shakePos = m_shakePos + m_cutPos;
    const Vec3 shakeRot = m_shakeRot + m_cutRot;
    const bool moved = std::fabs(shakePos.x) + std::fabs(shakePos.y) + std::fabs(shakePos.z)
                     + std::fabs(shakeRot.x) + std::fabs(shakeRot.y) + std::fabs(shakeRot.z) > 1e-5f;
    const bool rolled = std::fabs(dutch) > 1e-4f || moved;
    if (rolled)
        m = m * Mat4::Translate(shakePos) * Mat4::Rotate(Quat::Euler(shakeRot.x, shakeRot.y, shakeRot.z + dutch));
    if (pixelPerfect && projection == Projection::Orthographic && pixelsPerUnit > 0.0f) {
        m.at(3, 0) = std::round(m.at(3, 0) * pixelsPerUnit) / pixelsPerUnit;
        m.at(3, 1) = std::round(m.at(3, 1) * pixelsPerUnit) / pixelsPerUnit;
    }
    return m;
}

Vec3 Camera::RenderPosition() const {
    if (!transform) return Vec3::Zero;
    const Mat4 m = EyeMatrix();
    return Vec3{m.at(3, 0), m.at(3, 1), m.at(3, 2)};
}

float Camera::EffectiveOrthoSize() const {
    const float z = zoom > 0.01f ? zoom : 0.01f;
    return (orthographicSize > 1e-3f ? orthographicSize : 5.0f) / z;
}

Mat4 Camera::ViewMatrix() const {
    if (!transform) return Mat4{};
    return EyeMatrix().Inverse();
}

void Camera::Shake(float strength) {
    if (Accessibility::MotionScale() <= 0.0f) return;   // the player turned off screen shake
    m_trauma = Mathf::Clamp(m_trauma + strength, 0.0f, 1.0f);
}

void Camera::SetCutsceneShake(const Vec3& pos, const Vec3& rotDeg) { m_cutPos = pos; m_cutRot = rotDeg; }
void Camera::ClearCutsceneShake() { m_cutPos = {0, 0, 0}; m_cutRot = {0, 0, 0}; }

void Camera::ZoomTo(float target, float seconds) {
    target = target > 0.01f ? target : 0.01f;
    if (seconds <= 0.0f) { zoom = target; m_zoomDur = 0.0f; return; }
    m_zoomFrom = zoom; m_zoomTarget = target; m_zoomTime = 0.0f; m_zoomDur = seconds;
}

void Camera::LateUpdate(float dt) {
    if (m_zoomDur > 0.0f) {   // smooth zoom (ease in-out)
        m_zoomTime += dt;
        float t = Mathf::Clamp(m_zoomTime / m_zoomDur, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        zoom = m_zoomFrom + (m_zoomTarget - m_zoomFrom) * t;
        if (m_zoomTime >= m_zoomDur) { zoom = m_zoomTarget; m_zoomDur = 0.0f; }
    }
    if (Accessibility::MotionScale() <= 0.0f) m_trauma = 0.0f;   // reduced motion switched on mid-shake: stop at once
    if (m_trauma <= 0.0f) { m_shakePos = {0, 0, 0}; m_shakeRot = {0, 0, 0}; return; }
    m_shakeTime += dt;
    // Smooth pseudo-noise: two incommensurate sines per channel.
    auto n = [&](float seed) {
        const float t = m_shakeTime * shakeFrequency;
        return std::sin(t * 1.00f + seed) * 0.6f + std::sin(t * 1.73f + seed * 2.31f) * 0.4f;
    };
    const float k = m_trauma * m_trauma;
    m_shakePos = {n(1.3f) * shakeAmount * k, n(7.9f) * shakeAmount * k, 0.0f};
    m_shakeRot = {n(3.7f) * shakeAngle * k, n(5.1f) * shakeAngle * k, n(9.4f) * shakeAngle * k * 0.5f};
    m_trauma = Mathf::Max(0.0f, m_trauma - shakeRecovery * dt);
}

float Camera::VerticalFovDegrees(float aspect) const {
    float vfov = fieldOfView;
    if (physicalCamera && focalLength > 1e-3f) {
        // Vertical FOV from real lens geometry: 2*atan( sensorHeight / (2*focal) ).
        vfov = 2.0f * std::atan(sensorHeight / (2.0f * focalLength)) * Mathf::Rad2Deg;
    } else if (fovAxisHorizontal && aspect > 1e-4f) {
        // Horizontal FOV axis: convert the horizontal angle to the vertical angle
        // the perspective matrix expects, so the chosen axis stays fixed as the
        // aspect changes ("FOV Axis = Horizontal").
        float hf = fieldOfView * Mathf::Deg2Rad * 0.5f;
        vfov = 2.0f * std::atan(std::tan(hf) / aspect) * Mathf::Rad2Deg;
    }
    if (std::fabs(zoom - 1.0f) > 1e-4f && zoom > 0.01f)   // zoom narrows the view like a longer lens
        vfov = 2.0f * std::atan(std::tan(vfov * Mathf::Deg2Rad * 0.5f) / zoom) * Mathf::Rad2Deg;
    // A typed 0, a negative, 180 or more, or NaN made the projection infinite, mirrored or
    // NaN (a blank frame). Keep it a real, non-degenerate lens.
    if (!std::isfinite(vfov)) vfov = 60.0f;
    return vfov < 1.0f ? 1.0f : (vfov > 179.0f ? 179.0f : vfov);
}

Mat4 Camera::ProjectionMatrix(float aspect) const {
    // A minimised window (0 wide or high), a NaN or a negative size must not turn
    // the matrix into inf / NaN: reuse the last good aspect instead.
    if (!(aspect > 1e-4f) || !std::isfinite(aspect)) aspect = viewportAspect > 1e-4f ? viewportAspect : 1.0f;
    if (aspect > 0.05f) viewportAspect = aspect;
    // Clip planes typed into the inspector: near must be positive and far beyond it.
    const float nearP = nearClip > 1e-3f ? nearClip : 1e-3f;
    const float farP  = farClip > nearP + 0.01f ? farClip : nearP + 0.01f;
    if (projection == Projection::Perspective) {
        Mat4 p = Mat4::Perspective(VerticalFovDegrees(aspect), aspect, nearP, farP);
        p.at(2, 0) = lensShiftX * 2.0f;   // off-axis lens shift
        p.at(2, 1) = lensShiftY * 2.0f;
        return p;
    }
    float h = EffectiveOrthoSize();
    float w = h * aspect;
    // An orthographic (2D) camera sees its whole rectangle whatever the depth:
    // a camera added to an object at z = 0 used to clip away every sprite at
    // z = 0 behind its 0.3 near plane. Depth sorting is unaffected.
    Mat4 p = Mat4::Ortho(-w, w, -h, h, -farP, farP);
    p.at(3, 0) -= lensShiftX * 2.0f;
    p.at(3, 1) -= lensShiftY * 2.0f;
    return p;
}

} // namespace okay
