#pragma once
// The visual-script node registry: node types added while the engine runs. MakeNode asks here before the built-in list.
//  - A module's nodes (weapons): the add-node menu hides a node whose module is off. With every module on, the nodes
//    behave as they did when they were hard-coded. Written for this engine from the module plan
//    (docs/design/modular_gameplay_contracts.md, section 9, PR 9).
//  - Custom nodes (feature map 24.3): a type registered with its pins, category and help (RegisterNodeType). The editor's
//    palette, search, help pane, pin types and place rules read the same entry; custom nodes written in a script file are
//    loaded by VsCustomNodes.hpp. Registering again replaces the entry in place (hot reload), and the revision counts up so
//    readers know to look again.
#include "okay/VisualScript/VsTypes.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay::vs {

class VsNode;

/// Makes one node from its text arguments (the file's words after the type). Null with `err` set when it cannot.
using VsNodeFactory = std::function<std::unique_ptr<VsNode>(const std::vector<std::string>& args, std::string* err)>;

/// One data pin of a registered node: its label, what it carries and the value typed into it when nothing is wired
/// ("" = the type's empty value).
struct VsPinSpec {
    std::string name;
    PinType type = PinType::Any;
    std::string def;
};

/// What the editor shows of a registered node, and where it was written.
struct VsNodeInfo {
    std::string title;                  ///< shown in the node header and the palette
    std::string category = "Custom";    ///< the palette group words (the whole group is "Custom" for custom nodes)
    std::string help;                   ///< the help pane's text
    bool execIn = false;                ///< a flow node (runs when flow reaches it) rather than a pure value node
    std::vector<std::string> exits;     ///< flow outputs ("Then", or "Yes" / "No"...); empty for a pure node
    std::vector<VsPinSpec> ins, outs;   ///< data pins
    std::string source;                 ///< the script file it came from ("" = registered in code)
    int line = 0;                       ///< 1-based line of its definition in `source`
    std::string language;               ///< "okayscript", "lua", "cs"
    std::string base;                   ///< the built-in node a "custom version" was made from ("" = none)
};

struct VsNodeEntry {
    std::string type;
    std::string module;   ///< "" = always available (not owned by a switchable module)
    VsNodeFactory make;
    bool custom = false;  ///< registered with RegisterNodeType (has `info` and `sig`)
    VsNodeInfo info;
    NodeSignature sig;    ///< the pin types, as the rest of the checker reads them (SignatureFor)
    int revision = 0;     ///< VsNodeRegistryRevision() when this entry last changed
    bool removed = false; ///< taken out (UnregisterNodeType); Find* does not return it
};

/// Add or replace the factory for `type`. `module` empty means the node is always available.
void RegisterVsNode(std::string type, std::string module,
                    std::unique_ptr<VsNode> (*make)(const std::vector<std::string>& args, std::string* err));

/// Add or replace a node type with its pins, category and help (work package 1 of the custom nodes). The entry is
/// changed in place, so pointers from FindRegisteredVsNode stay valid; a removed type comes back in the same slot.
void RegisterNodeType(std::string type, VsNodeInfo info, VsNodeFactory make);
/// Take a type out (its script was deleted or no longer defines it). Graphs that use it keep a placeholder.
bool UnregisterNodeType(const std::string& type);

/// The entry for `type`, or null. The pointer stays valid for the life of the program.
const VsNodeEntry* FindRegisteredVsNode(const std::string& type);
/// Make a node of a registered type. False when `type` is not registered (out untouched); true otherwise, with `out`
/// null and `err` set when the factory refused or the type's module is off.
bool MakeRegisteredVsNode(const std::string& type, const std::vector<std::string>& args, std::string* err,
                          std::unique_ptr<VsNode>& out);

/// True when this type was registered by a module that is currently off.
bool VsNodeSuppressed(const std::string& type);

/// Registered type names. `enabledOnly` drops nodes whose module is off.
std::vector<std::string> RegisteredVsNodeTypes(bool enabledOnly);
/// The custom node types (RegisterNodeType), in the order they were first registered.
std::vector<std::string> CustomVsNodeTypes();
/// A copy of a custom type's info; false when `type` is not a registered custom node.
bool CustomVsNodeInfo(const std::string& type, VsNodeInfo& out);
/// The pin types of a custom node, or null (SignatureFor reads this).
const NodeSignature* CustomVsNodeSignature(const std::string& type);
/// Counts up every time a registered node is added, changed or removed.
int VsNodeRegistryRevision();

/// The weapon nodes (swing, fire, reload, ammo, set). Owned by the weapons module;
/// registered from the visual-script module because the node types live there.
void RegisterWeaponVsNodes();

} // namespace okay::vs
