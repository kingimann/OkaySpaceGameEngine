#pragma once
#include <string>
#include <vector>

namespace okay::csharp {

// Code help for C# scripts in the Script Editor: member lists after `x.` and signature
// help inside `f(`. Everything comes from the same binding table the C# checker and
// the visual-script nodes use (engine/src/Script/EngineApi.cpp) plus the script's own
// classes, so what the editor offers is exactly what compiles.

/// One thing that can follow `receiver.`.
struct Member {
    std::string name;
    bool method = false;          ///< a method (the editor adds "(")
    std::string type;             ///< a property's / field's type, or a method's result ("void")
    std::string doc;              ///< one line, from the binding table ("" for the script's own members)
};

/// The type of the expression before a '.', worked out from the script (fields,
/// parameters, locals, `var x = new T(...)`, GetComponent<T>()) and the binding table
/// (transform.position is a Vector3). "Mathf" / "Health" as a type name gives
/// "static:Mathf" / "static:Health". "" when it isn't known.
std::string ReceiverType(const std::string& source, const std::string& receiver);

/// Everything to offer after `receiver.` in `source`, sorted by name; empty when the
/// receiver's type isn't known.
std::vector<Member> MembersAfter(const std::string& source, const std::string& receiver);

/// One way to call a method: "void Translate(float x, float y, float z)".
struct Signature {
    std::string text;
    std::vector<std::string> params;   ///< each parameter as written in text ("float x")
    std::string doc;
};
/// Each way to call `method` on `receiver` ("" = the script's own method, or a
/// constructor after `new`). Empty when nothing is known by that name.
std::vector<Signature> SignaturesFor(const std::string& source, const std::string& receiver, const std::string& method);

/// True when `name` is an engine component C# scripts can get (GetComponent<name>()).
bool IsEngineComponent(const std::string& name);

} // namespace okay::csharp
