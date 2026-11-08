#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <string>

namespace okay::testing {

/// "Root/Parent/Child": the names of an object and its ancestors, outermost first.
/// Used to point at an object in validation reports, golden files and test messages.
inline std::string ObjectPath(const GameObject* go) {
    std::string path;
    for (int guard = 0; go && guard < 256; ++guard) {
        path = path.empty() ? go->name : go->name + "/" + path;
        const Transform* p = go->transform ? go->transform->Parent() : nullptr;
        go = p ? p->gameObject : nullptr;
    }
    return path;
}

/// A component's class name without the namespace, the same on every compiler:
/// "Camera", "Rigidbody2D". Used in reports and in the scene hash.
std::string ComponentTypeName(const Component& component);

} // namespace okay::testing
