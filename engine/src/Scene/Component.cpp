#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

Scene* Component::GetScene() const {
    return gameObject ? gameObject->scene() : nullptr;
}

} // namespace okay
