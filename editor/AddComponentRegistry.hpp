#pragma once
// The bridge between the Add Component browser and the engine's component registry (okay::PropertyRegistry): every registered
// component type becomes a row without a per-type edit, and a hover preview says what a component adds. No ImGui in here, so
// the Dear ImGui popup (AddComponentMenu.cpp) and the OkayUI popup (OkayUIHostObjects.cpp) share it. Written for the owner's
// request of 2026-11-20; no code from another project.
#include "AddComponentModel.hpp"

#include <memory>
#include <string>
#include <vector>

namespace okay { class GameObject; struct ComponentTypeInfo; }

namespace oked {
namespace addcomp {

struct RegItem {
    std::shared_ptr<const okay::ComponentTypeInfo> info;
    std::string label;      // "Box Collider 3D" (the type name with spaces)
    std::string nkey;       // Canon(KeyOf(type name))
    bool present = false;   // the object already has one
};
/// Every registered component type that can be added (not the object itself, not Transform), with whether `go` has one.
std::vector<RegItem> RegistryItems(const okay::GameObject& go);
/// Adds the type to the object (the registry's `add`); the components it requires come with it. False when it cannot.
bool AddRegistered(okay::GameObject& go, const okay::ComponentTypeInfo& info);
/// The text of the hover preview: what it is, what else it adds, a few settings you can change.
std::string PreviewText(const Entry& e);
/// Entries for a whole object straight from the registry (the OkayUI popup, tests): same model rows the other window uses.
/// `infos` (optional) gets the registry entry of each returned row, same order, so the host can add the one that was picked.
std::vector<Entry> EntriesFromRegistry(const okay::GameObject& go, std::vector<std::shared_ptr<const okay::ComponentTypeInfo>>* infos = nullptr);

}  // namespace addcomp
}  // namespace oked
