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

Mat4 Camera::ViewMatrix() const {
    if (!transform) return Mat4{};
    return transform->LocalToWorldMatrix().Inverse();
}

Mat4 Camera::ProjectionMatrix(float aspect) const {
    if (projection == Projection::Perspective) {
        float vfov = fieldOfView;
        // Horizontal FOV axis: convert the horizontal angle to the vertical angle
        // the perspective matrix expects, so the chosen axis stays fixed as the
        // aspect changes (Unity's "FOV Axis = Horizontal").
        if (fovAxisHorizontal && aspect > 1e-4f) {
            float hf = fieldOfView * Mathf::Deg2Rad * 0.5f;
            vfov = 2.0f * std::atan(std::tan(hf) / aspect) * Mathf::Rad2Deg;
        }
        return Mat4::Perspective(vfov, aspect, nearClip, farClip);
    }
    float h = orthographicSize;
    float w = orthographicSize * aspect;
    return Mat4::Ortho(-w, w, -h, h, nearClip, farClip);
}

} // namespace okay
