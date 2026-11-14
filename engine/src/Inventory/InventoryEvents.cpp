#include "okay/Inventory/InventoryEvents.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {
namespace InventoryEvents {

void Fire(GameObject* go, const char* scriptEvent, const char* vsEntry, const std::vector<vs::VsValue>& args) {
    if (!go) return;
    if (scriptEvent)
        for (auto* sc : go->GetComponents<ScriptComponent>())
            if (sc->VM()) sc->VM()->CallEventArgs(scriptEvent, args);
    if (vsEntry) {
        const std::string name = args.empty() ? std::string() : args[0].AsString();
        const vs::VsValue value = args.size() > 1 ? args[1] : vs::VsValue{};
        for (auto* vc : go->GetComponents<VisualScriptComponent>()) vc->RaiseEvent(vsEntry, nullptr, name, value);
    }
}

} // namespace InventoryEvents
} // namespace okay
