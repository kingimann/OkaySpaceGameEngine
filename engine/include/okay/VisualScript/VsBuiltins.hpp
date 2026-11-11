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
/// Shared pin signature of every Fn.* node: four optional arguments, one result.
const NodeSignature& BuiltinSignature();

/// System-level coverage (FEATURE_MAP area, how a graph reaches it, the node).
std::string CoverageTableMarkdown();

} // namespace okay::vs
