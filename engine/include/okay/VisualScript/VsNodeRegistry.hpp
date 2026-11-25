#pragma once
// Visual-script nodes a module adds. MakeNode asks here before the built-in list, and the
// add-node menu hides a node whose module is off. With every module on, the nodes behave
// as they did when they were hard-coded. Written for this engine from the module plan
// (docs/design/modular_gameplay_contracts.md, section 9, PR 9).
#include <memory>
#include <string>
#include <vector>

namespace okay::vs {

class VsNode;

struct VsNodeEntry {
    std::string type;
    std::string module;   ///< "" = always available (not owned by a switchable module)
    std::unique_ptr<VsNode> (*make)(const std::vector<std::string>& args, std::string* err) = nullptr;
};

/// Add or replace the factory for `type`. `module` empty means the node is always available.
void RegisterVsNode(std::string type, std::string module,
                    std::unique_ptr<VsNode> (*make)(const std::vector<std::string>& args, std::string* err));

const VsNodeEntry* FindRegisteredVsNode(const std::string& type);

/// True when this type was registered by a module that is currently off.
bool VsNodeSuppressed(const std::string& type);

/// Registered type names. `enabledOnly` drops nodes whose module is off.
std::vector<std::string> RegisteredVsNodeTypes(bool enabledOnly);

/// The weapon nodes (swing, fire, reload, ammo, set). Owned by the weapons module;
/// registered from the visual-script module because the node types live there.
void RegisterWeaponVsNodes();

} // namespace okay::vs
