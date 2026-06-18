#include "okay/Components/Camera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

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
    if (projection == Projection::Perspective)
        return Mat4::Perspective(fieldOfView, aspect, nearClip, farClip);
    float h = orthographicSize;
    float w = orthographicSize * aspect;
    return Mat4::Ortho(-w, w, -h, h, nearClip, farClip);
}

} // namespace okay
