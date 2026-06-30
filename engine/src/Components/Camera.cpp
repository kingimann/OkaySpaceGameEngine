#include "okay/Components/Camera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

void Camera::Awake() {
    if (main) {
        if (Scene* s = GetScene()) s->mainCamera = this;
    }
}

void Camera::ScreenPointToRay(float screenX, float screenY, float screenW, float screenH,
                              Vec3& outOrigin, Vec3& outDir) const {
    float aspect = screenH > 0.0f ? screenW / screenH : 1.0f;
    float ndcX = screenW > 0.0f ? (2.0f * screenX / screenW - 1.0f) : 0.0f;
    float ndcY = screenH > 0.0f ? (1.0f - 2.0f * screenY / screenH) : 0.0f;   // +Y up
    Quat rot = transform ? transform->Rotation() : Quat::Identity;
    outOrigin = transform ? transform->Position() : Vec3::Zero;
    Vec3 right = rot * Vec3::Right, up = rot * Vec3::Up;
    Vec3 fwd = rot * Vec3{0.0f, 0.0f, -1.0f};                 // cameras look down -Z
    if (projection == Projection::Perspective) {
        float tanV = std::tan(VerticalFovDegrees(aspect) * Mathf::Deg2Rad * 0.5f);
        float tanH = tanV * aspect;
        outDir = (fwd + right * (ndcX * tanH) + up * (ndcY * tanV)).Normalized();
    } else {
        // Orthographic: parallel rays; the origin slides across the view rectangle.
        outOrigin = outOrigin + right * (ndcX * orthographicSize * aspect)
                              + up * (ndcY * orthographicSize);
        outDir = fwd.Normalized();
    }
}

Vec3 Camera::ScreenToWorldPoint(float screenX, float screenY, float viewDepth,
                                float screenW, float screenH) const {
    Vec3 o, d;
    ScreenPointToRay(screenX, screenY, screenW, screenH, o, d);
    if (projection == Projection::Perspective) {
        Quat rot = transform ? transform->Rotation() : Quat::Identity;
        Vec3 fwd = rot * Vec3{0.0f, 0.0f, -1.0f};
        float along = Vec3::Dot(d, fwd);                      // ray foreshortening
        float t = (std::fabs(along) > 1e-5f) ? viewDepth / along : viewDepth;
        return o + d * t;                                     // hits the plane viewDepth in front
    }
    return o + d * viewDepth;
}

Mat4 Camera::ViewMatrix() const {
    if (!transform) return Mat4{};
    return transform->LocalToWorldMatrix().Inverse();
}

float Camera::VerticalFovDegrees(float aspect) const {
    float vfov = fieldOfView;
    if (physicalCamera && focalLength > 1e-3f) {
        // Vertical FOV from real lens geometry: 2*atan( sensorHeight / (2*focal) ).
        vfov = 2.0f * std::atan(sensorHeight / (2.0f * focalLength)) * Mathf::Rad2Deg;
    } else if (fovAxisHorizontal && aspect > 1e-4f) {
        // Horizontal FOV axis: convert the horizontal angle to the vertical angle
        // the perspective matrix expects, so the chosen axis stays fixed as the
        // aspect changes (Unity's "FOV Axis = Horizontal").
        float hf = fieldOfView * Mathf::Deg2Rad * 0.5f;
        vfov = 2.0f * std::atan(std::tan(hf) / aspect) * Mathf::Rad2Deg;
    }
    return vfov;
}

Mat4 Camera::ProjectionMatrix(float aspect) const {
    if (projection == Projection::Perspective) {
        return Mat4::Perspective(VerticalFovDegrees(aspect), aspect, nearClip, farClip);
    }
    float h = orthographicSize;
    float w = orthographicSize * aspect;
    return Mat4::Ortho(-w, w, -h, h, nearClip, farClip);
}

} // namespace okay
