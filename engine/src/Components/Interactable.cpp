#include "okay/Components/Interactable.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Inventory/InventoryEvents.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {

bool Interactable::Use(GameObject* actor) {
    if (!Available()) return false;
    ++useCount;
    const std::vector<vs::VsValue> args{vs::VsValue(actor ? actor->name : std::string())};
    // script event only: the visual-script OnInteract entry already polls the key by itself
    InventoryEvents::Fire(gameObject, "on_interact", nullptr, args);
    return true;
}

void InteractionSensor::Update(float dt) {
    m_target = nullptr; m_prompt.clear();
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc || !transform) { m_progress = 0.0f; m_held = 0.0f; return; }
    float d = 0.0f;
    auto it = interact::Nearest(*sc, gameObject, &d);
    if (!it || (maxRange > 0.0f && d > maxRange)) { m_progress = 0.0f; m_held = 0.0f; return; }
    InteractContext ctx; ctx.actor = gameObject;
    m_target = it->Object();
    m_prompt = it->Prompt(ctx);
    if (std::string(it->Model()) != "Interactable") { m_progress = 0.0f; m_held = 0.0f; return; }   // the others read the key themselves
    const float hold = it->HoldSeconds();
    if (hold <= 0.0f) {
        m_progress = 0.0f; m_held = 0.0f;
        if (!ItemPickup::InteractIsBlocked() && InteractPressed()) { ItemPickup::BlockInteract(); it->Interact(ctx); }
        return;
    }
    if (!InteractHeld() || ItemPickup::InteractIsBlocked()) { m_held = 0.0f; m_progress = 0.0f; return; }
    m_held += dt;
    m_progress = m_held / hold;
    if (m_held >= hold) { m_held = 0.0f; m_progress = 0.0f; ItemPickup::BlockInteract(); it->Interact(ctx); }
}

void InteractionSensor::LateUpdate(float) { ItemPickup::ClearInteractBlock(); }

} // namespace okay
