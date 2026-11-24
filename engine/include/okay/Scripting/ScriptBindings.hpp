#pragma once
// The generic script binding layer: one table of every engine component class a script can hold, with its methods
// (generated from our own headers by scripts/gen_script_bindings.py into ScriptBindingsGenerated.cpp) and its
// properties (the PropertyRegistry, PropertyRegistry.hpp). OkayScript reaches it through EngineHooks
// (OkayScriptVM.cpp): `GetComponent("Light").intensity = 2`, `rb.AddForce(Vector3.up * 5)`.
//
// Safety: a script holds a component as a handle value (vs::VsValue::ComponentHandle): the object's instance id, the
// class key and which component of that class on the object. No pointer is ever stored in a script value. Every use
// resolves the handle again, so a handle to a destroyed object or a removed component reads as null (`if (c)` false,
// `c == null` true) and using it gives a clear error, never a crash.
//
// Design and gap audit: docs/scripting/script_bindings.md.
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Properties.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace okay {
class Component;
class GameObject;
struct PropertyDesc;
}

namespace okay::scriptbind {

/// What one argument or result of a bound method is, as a script value carries it.
enum class ArgKind : std::uint8_t { Void, Bool, Int, Float, String, Vec2, Vec3, Vec4, Quat, Color, Enum, Object, Component };
/// "a number", "text", "a vector" ... for error messages.
const char* ArgKindName(ArgKind k);

constexpr int kMaxBoundParams = 8;
using MethodFn = vs::VsValue (*)(Component& self, const vs::VsValue* args, int argc);

/// One bound method. Arguments past `required` have defaults in C++ and may be left out.
struct MethodBinding {
    const char* name;
    std::vector<ArgKind> params;
    int required;
    ArgKind ret;
    MethodFn fn;
    bool isConst = false;   ///< a const member function: it only reads the component
};

/// One bound component class.
struct ClassBinding {
    std::string name;                                   ///< the C++ class name ("Rigidbody3D")
    std::type_index type{typeid(void)};
    bool (*is)(const Component&) = nullptr;             ///< true for this class or a subclass of it
    Component* (*add)(GameObject&) = nullptr;           ///< null when the class cannot be added (abstract, no default constructor)
    std::vector<MethodBinding> methods;
};

template <class T> bool IsA(const Component& c) { return dynamic_cast<const T*>(&c) != nullptr; }
template <class T> Component* AddA(GameObject& g) {
    if constexpr (!std::is_abstract<T>::value && std::is_default_constructible<T>::value) return g.AddComponent<T>();
    else { (void)g; return nullptr; }
}
/// What the generated file calls once per class.
template <class T> ClassBinding MakeClass(const char* name, const MethodBinding* methods, int count) {
    ClassBinding c;
    c.name = name;
    c.type = std::type_index(typeid(T));
    c.is = &IsA<T>;
    c.add = &AddA<T>;
    for (int i = 0; i < count; ++i) if (methods[i].name) c.methods.push_back(methods[i]);
    return c;
}
void AddClass(ClassBinding c);
/// Defined in ScriptBindingsGenerated.cpp.
void RegisterGeneratedBindings();

// ---- The table ------------------------------------------------------------------------------------------------
/// Every bound class, in a stable order (registered on first use). The key of a class is its index + 1.
const std::vector<ClassBinding>& AllClasses();
/// By C++ name ("Rigidbody3D"), any case, by scene-record name ("rigidbody3d"), or by the script names the engine
/// has always used ("Rigidbody" = Rigidbody3D, "Collider" = Collider3D). Null when unknown.
const ClassBinding* FindClass(const std::string& name);
/// The exact class of `c` (its dynamic type), or null when it is not bound.
const ClassBinding* ClassOf(const Component& c);
const ClassBinding* ClassByKey(std::uint32_t key);
std::uint32_t KeyOf(const ClassBinding& c);

// ---- Handles --------------------------------------------------------------------------------------------------
/// The `index`-th component of class `cls` on `go` (0 = the first), or null.
Component* FindComponent(const GameObject& go, const ClassBinding& cls, int index = 0);
/// A handle to `c` as the class `cls` (its own class when null). Null value (0) when `c` is null.
vs::VsValue MakeHandle(Component* c, const ClassBinding* cls = nullptr);
bool IsHandle(const vs::VsValue& v);
/// The component a handle names, or null once its object or the component is gone (or `v` is no handle).
Component* Resolve(const vs::VsValue& v, const ClassBinding** cls = nullptr);

// ---- Use --------------------------------------------------------------------------------------------------------
enum class Result { NotFound, Ok, Error };
/// `handle.name`: a registered property of the component's class, or `gameObject` (its object), `enabled`.
/// NotFound when the class has no such member; Error (with `error`) when the handle is null.
Result GetMember(const vs::VsValue& handle, const std::string& name, vs::VsValue& out, std::string& error);
/// `handle.name = v` through the PropertyRegistry (validated, clamped, change hook fired).
Result SetMember(const vs::VsValue& handle, const std::string& name, const vs::VsValue& v, std::string& error);
/// `handle.Method(args)`: picks the overload whose argument count and types fit; a wrong type is an Error that
/// names the argument, the type wanted and the type given.
Result CallMethod(const vs::VsValue& handle, const std::string& method, const std::vector<vs::VsValue>& args,
                  vs::VsValue& out, std::string& error);
/// Call a method on a component directly (CallMethod without the handle).
Result CallOn(Component& c, const ClassBinding& cls, const std::string& method, const std::vector<vs::VsValue>& args,
              vs::VsValue& out, std::string& error);

// ---- What scripts must never reach (the generator's DENY_* rules, for other tables) --------------------------------
/// The rules scripts/gen_script_bindings.py uses to leave a method out (files, network, platform, clipboard). The
/// generator writes them into ScriptBindingsGenerated.cpp, so a language that also binds another table (Lua and the
/// typed-language EngineApi) applies the same rules and is no back door around them.
struct DenyRules {
    std::vector<std::string> classes;     ///< a whole class: NetworkManager, SteamManager, UIFilePicker, ...
    std::vector<std::string> methods;     ///< "Class.Method": NavMeshSurface.Save, ...
    std::vector<std::string> paramWords;  ///< a text parameter named with one of these: path, file, url, ...
    std::vector<std::string> nameWords;   ///< a method name with one of these words: file, clipboard, server, ...
};
/// Defined in ScriptBindingsGenerated.cpp.
const DenyRules& GeneratedDenyRules();
/// The lower-case words of a name, as the generator splits them: "filePath" -> file, path; "HTTPServer" -> http, server.
std::vector<std::string> NameWords(const std::string& name);
/// True when the generator would not bind any method of class `cls`.
bool IsDeniedClass(const std::string& cls);
/// One parameter as the rules see it: is it text, and its name.
struct DenyParam { bool isText = false; std::string name; };
/// True when the generator would not bind `cls.method` with these parameters (any one rule is enough).
bool IsDeniedMethod(const std::string& cls, const std::string& method, const std::vector<DenyParam>& params = {});

// ---- Property values <-> script values (one conversion for OkayScript and visual scripts) ---------------------
/// A property value as a script value: numbers, true/false, vectors (a Vec2 as (x, y, 0), a Color as (r, g, b)),
/// an Enum as its option name, anything else as its text.
vs::VsValue PropertyToScript(const PropertyValue& v);
/// A script value in the type `desc` wants, or false with `error` ("Light.intensity wants a number, got text").
bool ScriptToProperty(const PropertyDesc& desc, const vs::VsValue& v, PropertyValue& out, std::string& error);

// ---- Helpers the generated code uses ----------------------------------------------------------------------------
/// A script argument out of the range its parameter takes: CallOn turns it into the call's error
/// ("Light.SetMode(): argument 1 must be 0 to 3, got 9").
struct ArgError : std::runtime_error { using std::runtime_error::runtime_error; };
long long ArgInt(const vs::VsValue& v);
/// A whole number in [lo, hi] (an enum's values, the range of the C++ type), else ArgError for argument idx.
long long ArgIntIn(const vs::VsValue& v, int idx, long long lo, long long hi);
/// A count or an index: -1 .. max (so one call cannot make the engine allocate gigabytes), else ArgError.
long long ArgCount(const vs::VsValue& v, int idx, long long max);
Vec2 ArgVec2(const vs::VsValue& v);
Vec4 ArgVec4(const vs::VsValue& v);
Quat ArgQuat(const vs::VsValue& v);
Color ArgColor(const vs::VsValue& v);
GameObject* ArgObject(const vs::VsValue& v);
vs::VsValue RetVec2(const Vec2& v);
vs::VsValue RetVec4(const Vec4& v);
vs::VsValue RetQuat(const Quat& q);
vs::VsValue RetColor(const Color& c);
vs::VsValue RetObject(const GameObject* g);
vs::VsValue RetComponent(const Component* c);

} // namespace okay::scriptbind
