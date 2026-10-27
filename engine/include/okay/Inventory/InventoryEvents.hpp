#pragma once
#include "okay/VisualScript/VsValue.hpp"
#include <string>
#include <vector>

namespace okay {

class GameObject;

/// Inventory gameplay events, delivered to everything on one object that listens:
///  - every ScriptComponent gets `scriptEvent` (on_item_added, ...) with `args`
///    (OkayScript also matches the C#-style alias, e.g. OnItemAdded);
///  - every VisualScriptComponent runs its `vsEntry` event nodes (On Item Added, ...)
///    with Event Name = args[0] and Event Value = args[1] (when given).
/// `vsEntry` may be null for events without a visual-script node.
namespace InventoryEvents {
void Fire(GameObject* go, const char* scriptEvent, const char* vsEntry, const std::vector<vs::VsValue>& args);
} // namespace InventoryEvents

} // namespace okay
