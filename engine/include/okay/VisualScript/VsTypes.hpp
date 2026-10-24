#pragma once
// Pin types for visual-script nodes and the graph checker that runs before
// Play and before a build. One table here describes every node type's data
// pins; the editor colours pins from it and refuses wires it says can't work.
#include <string>
#include <vector>

namespace okay { class Scene; }

namespace okay::vs {

/// What a data pin carries. Stored as one letter per pin in the node table.
enum class PinType : char {
    Any = 'A',     ///< anything (variables, events, Select, Equals...)
    Number = 'N',
    Bool = 'B',
    Text = 'T',
    Vec3 = 'V',
    Object = 'O',  ///< an object reference, or a name typed into the pin
    List = 'L',
};

/// A node type's pins: whether it has an exec input, then one PinType letter per
/// data input / output (a "Target" input is already appended where the node has one).
struct NodeSignature {
    bool execIn = false;
    std::string ins, outs;
};
/// nullptr for an unknown type.
const NodeSignature* SignatureFor(const std::string& type);
/// Every node type in the table (the editor's palette, by type name).
std::vector<std::string> AllNodeTypes();
/// The type a Const node's typed value has ("3" number, "true" bool, else text).
PinType ConstType(const std::string& arg);
/// The output type of `type`'s pin `pin` given the node's text field (Const).
PinType OutputType(const std::string& type, int pin, const std::string& arg = "");
PinType InputType(const std::string& type, int pin);

/// Can a value of type `from` feed an input of type `to` and mean something?
/// Number <-> Bool, anything -> Text, Object <-> Text (by name), Object -> Bool
/// (still exists?) and Any either way are fine; the rest is refused.
bool PinCompatible(PinType from, PinType to);
const char* PinTypeName(PinType t);
/// What to use instead when `from` can't feed `to` ("" when there's no tip).
std::string ConversionHint(PinType from, PinType to);

/// A visual-script function's signature, written in its Function node's text
/// field: `Damage(amount: Number, who: Object) -> left: Number, dead: Bool`
/// (no parameters: `Reset()`; returns are optional; a type may be left out
/// and is then Any). Types: Number, Bool, Text, Vector3, Object, List, Any.
struct FunctionSig {
    std::string name;
    struct Pin { std::string name; PinType type = PinType::Any; };
    std::vector<Pin> params, returns;
};
/// False (with `error` saying what's wrong) when `text` isn't a valid signature.
bool ParseFunctionSignature(const std::string& text, FunctionSig& out, std::string* error = nullptr);
/// The function name a signature text starts with (whatever is before '('), trimmed.
std::string FunctionNameOf(const std::string& signatureText);

/// One problem found in a graph. `node` is the node's id in the file/editor
/// (-1 for the whole graph); `severity` 0 = error (it won't work), 1 = warning.
struct VsIssue {
    int severity = 0;
    int node = -1;
    std::string message;
};
/// Check a graph (OkayVS text): wires between pins that don't fit, nodes no
/// event ever reaches, events that do nothing, variables read but never set,
/// unknown key names, prefab files that don't exist (relative to `baseDir`
/// when given). Parse errors come back as an error too.
std::vector<VsIssue> ValidateGraphText(const std::string& text, const std::string& baseDir = "");

/// Every visual script in a scene, checked. `object` names the GameObject, `graph`
/// its .okayvs file ("" when embedded), `nodeType` the node an issue is about.
struct SceneGraphIssue {
    std::string object, graph, nodeType;
    VsIssue issue;
    /// "Visual Script on 'Player' (enemy.okayvs), node 'Add' (#2): <message>"
    std::string Describe() const;
};
std::vector<SceneGraphIssue> ValidateSceneGraphs(const okay::Scene& scene, const std::string& baseDir = "");

} // namespace okay::vs
