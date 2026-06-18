#include "okay/Components/Camera.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

void Camera::Awake() {
    if (main) {
        if (Scene* s = GetScene()) s->mainCamera = this;
    }
}

} // namespace okay
