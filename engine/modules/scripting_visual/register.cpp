// Visual-script module startup: hand the weapon nodes to the node registry.
// The node types live in this module (include/okay/VisualScript). Each one is tagged
// with the weapons module, so they disappear from MakeNode and the add-node list
// when weapons is off. With weapons on they are the same nodes as before.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/VisualScript/VsNodeRegistry.hpp"

namespace okay {
namespace modules {

void Register_scripting_visual(ModuleContext&) {
    vs::RegisterWeaponVsNodes();
}

} // namespace modules
} // namespace okay
