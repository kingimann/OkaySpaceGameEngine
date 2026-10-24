#pragma once
// Visual-script nodes generated from the engine binding table the C#-style
// language uses (engine/src/Script/EngineApi.cpp). Every property and method
// there becomes a node here, calling the very same binding, so something added
// to the table once shows up in scripts and in graphs alike.
//
// Node types are spelled the way the code reads:
//   Transform.position       get a property      (value node)
//   Transform.position=      set a property      (step in the flow)
//   Transform.Translate/3    call a method with 3 parameters
//                            (a step, or a value node when it has no side effects)
// Inputs are the value being set / the parameters, in order. Members of an object
// type (GameObject, Transform, Rigidbody) then take a last Target input (empty =
// the graph's own object), like every other node acting on an object; members of
// Vector3 take the vector last; static members (Input, Time, Mathf...) take neither.
#include "okay/VisualScript/VsTypes.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay::vs {

class VsNode;

struct VsApiPin {
    std::string name;
    PinType type = PinType::Any;
};

struct VsApiEntry {
    std::string type;       ///< node type in .okayvs files ("Transform.Translate/3")
    std::string apiType;    ///< "Transform"
    std::string member;     ///< "Translate"
    std::string title;      ///< node header: "Transform.Translate(x, y, z)"
    std::string doc;        ///< what it does (from the binding table)
    std::string code;       ///< how the same thing reads in a script: "transform.Translate(x, y, z)"
    bool exec = false;      ///< a step in the flow (exec in / out) rather than a value node
    bool hasTarget = false; ///< the last input is a Target object (empty = this object)
    std::vector<VsApiPin> ins, outs;
    NodeSignature sig;      ///< the same pins as the pin-type table describes them
};

/// Every generated node, grouped by API type in table order.
const std::vector<VsApiEntry>& VsApiEntries();
/// The entry for node type `type`, or nullptr when it isn't a generated node.
const VsApiEntry* VsApiFind(const std::string& type);
/// A runtime node for `type`, or nullptr when it isn't a generated node.
std::unique_ptr<VsNode> MakeApiNode(const std::string& type);
/// The binding's own check of a typed-in value for input `pin` (key names,
/// axis names): an error message, or "" when it's fine or not checked.
std::string VsApiCheckLiteral(const std::string& type, int pin, const std::string& literal);
/// The reference page for the whole binding table (docs/engine_api.md is this text):
/// per type, each member as script code, as a node, its pins and what it does.
std::string VsApiReference();

} // namespace okay::vs
