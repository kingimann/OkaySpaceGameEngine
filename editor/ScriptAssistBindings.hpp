#pragma once
// Script assist from the engine's binding table (part of Script assist, editor/ScriptAssist.hpp): what the Script Editor's
// autocomplete, argument hints and hover docs know about engine components comes from the one table a script itself
// uses at run time: okay::scriptbind (the classes and methods generated from our headers by scripts/gen_script_bindings.py)
// and the PropertyRegistry (their properties). No hand-written member list for components: a new component, method or
// property shows up in the editor as soon as it is bound.
//
// No Dear ImGui here, so tests/test_script_assist_bindings.cpp checks it headless.
#include <map>
#include <string>
#include <vector>

namespace oked {

/// One member of a bound class: a property ("intensity", detail "float intensity: How bright the light is") or a method
/// ("AddForce", detail "void AddForce(Vector3, int = ...)", one line per overload).
struct BoundMember {
    std::string name, detail;
    bool method = false;
};

/// The members a script can reach on a bound component class: gameObject, enabled, the class's properties, its methods
/// (one entry per name), sorted by name. Null when the name is no bound class (any case, or a script alias such as
/// "Rigidbody" for Rigidbody3D).
const std::vector<BoundMember>* BoundMembersOf(const std::string& className);

/// The bound class a receiver expression names, "" when none: GetComponent("X") / GetComponent<X>() / getComponent("X")
/// (also at the end of a chain: gameObject.GetComponent("X")), or a variable that `src` sets from one of those or declares
/// with a class's name (`Light lamp`, `var rb = GetComponent("Rigidbody")`). Returns the class's own name.
std::string BoundClassOf(const std::string& src, const std::string& receiver);

/// What the autocomplete lists after `receiver.`: property names, method names with a '(' after them. Empty when the
/// receiver is no bound class.
std::vector<std::string> BoundMemberWords(const std::string& src, const std::string& receiver);

/// The argument hint for a method name over every bound class: one line per class and overload,
/// "Rigidbody3D.AddForce(Vector3, [int])". Null when no bound class has such a method.
const std::string* BoundMethodSignature(const std::string& method);

/// Hover docs: each bound class name ("Light (component): ...") and each member name ("Light: float intensity ...",
/// one line per class that has it, at most 4).
const std::map<std::string, std::string>& BoundDocs();

} // namespace oked
