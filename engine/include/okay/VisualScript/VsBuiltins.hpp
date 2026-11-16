#pragma once
// One visual-script node per text-script builtin (`Fn.print`, `Fn.move`, ...),
// plus the generic escape hatches (property, method, component, find, message,
// text snippet). The builtin nodes call OkayScriptVM::CallBuiltin, so a graph
// can do anything a .okay script can do.
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay::vs {

struct BuiltinNodeInfo {
    std::string type;      ///< "Fn.print"
    std::string title;     ///< "print"
    std::string builtin;   ///< "print"
};

/// Every text-script builtin, as a node. Built once.
const std::vector<BuiltinNodeInfo>& BuiltinNodes();
/// True for "Fn.<builtin>".
bool IsBuiltinNode(const std::string& type);
/// The node, or nullptr when `type` is neither a builtin node nor an escape hatch.
/// `args` is the text after the type (a property path, Type.Method, a message, a snippet).
std::unique_ptr<VsNode> MakeCoverageNode(const std::string& type, const std::vector<std::string>& args);
/// The most inputs a Fn.* node has (capsule_cast3 reads 13 arguments).
constexpr int kBuiltinMaxPins = 13;
/// How many inputs the node of text-script builtin `builtin` has: one per argument its code
/// reads, and never fewer than four (every Fn.* node had four before, so saved graphs keep
/// their pins). "dist3" -> 6, "print" -> 4.
int BuiltinPinCount(const std::string& builtin);
/// Label of argument input `pin`: "First", "Second" ... "Thirteenth" ("" outside the range).
const char* BuiltinPinName(int pin);
/// Pin signature of "Fn.<builtin>": BuiltinPinCount optional inputs (Any), one result.
const NodeSignature& BuiltinSignatureFor(const std::string& type);

/// System-level coverage (FEATURE_MAP area, how a graph reaches it, the node).
std::string CoverageTableMarkdown();

} // namespace okay::vs
