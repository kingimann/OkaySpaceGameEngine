#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"

namespace okay {

Scene* Component::GetScene() const {
    return gameObject ? gameObject->scene() : nullptr;
}

void Component::DeliverDisableBeforeDestroy() {
    if (!m_live) return;
    m_live = false;
    // A script already runs its own on_disable from OnDestroy; don't send it twice.
    if (dynamic_cast<ScriptComponent*>(this)) return;
    OnDisable();
}

} // namespace okay
