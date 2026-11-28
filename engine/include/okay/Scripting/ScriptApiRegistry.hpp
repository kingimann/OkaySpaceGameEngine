#pragma once
#include "okay/VisualScript/VsValue.hpp"
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace okay {

class GameObject;

// The one shared table of engine functions for scripts (docs/scripting/script_api_registry.md).
//
// A function written here is bound ONCE and every script language reads it:
//   - OkayScript (OkayScriptVM) calls it by its snake_case `name` (and `aliases`): save_game("1").
//   - The typed language (okayscript-next / C#, Script/EngineApi.cpp) calls it as `typedType.typedName`:
//     SaveGame.Save("1", ""). EngineApi adds the entry to that type, so the visual-script Engine API nodes
//     and the editor's C# autocomplete get it too.
// Before this table each language had its own hand-written copy and they drifted (#422 audit item 3).
//
// Each language turns its own values into the declared parameter types before `fn` runs, so `fn` always
// gets exactly params.size() arguments of the declared kinds (an Int arrives as a whole-number float, a
// Bool as a bool). A problem the script should hear about is thrown as ScriptApiError: the typed language
// stops with it as a runtime error at the call's line; OkayScript logs it once as a warning and the call
// gives the empty value of its return type (the old OkayScript functions failed quietly the same way).

/// A script function: `self` is the object the call acts on (the script's own object for a Function or a
/// StaticProperty, the receiver for a Method; null for a script with no object).
using ScriptApiFn = std::function<vs::VsValue(GameObject* self, std::vector<vs::VsValue>& args)>;

/// What a shared function reports to the script that called it (wrong object, unknown name, ...).
struct ScriptApiError : std::runtime_error { using std::runtime_error::runtime_error; };

enum class ScriptApiType : std::uint8_t { Void, Bool, Int, Float, String, Vector3 };

enum class ScriptApiKind : std::uint8_t {
    Function,        ///< typed: a static call, Input.GetKey("w"); self = the script's object
    Method,          ///< typed: a call on a component reference, rb.AddTorque(t); self = that object
    StaticProperty,  ///< typed: a read-only static value, Time.time; no parameters
};

struct ScriptApiParam {
    std::string name;
    ScriptApiType type = ScriptApiType::Float;
    bool optional = false;        ///< may be left out (then `def` is used); only trailing parameters
    vs::VsValue def;              ///< the value of a left-out optional parameter
};

struct ScriptApiEntry {
    std::string name;                     ///< the OkayScript name (snake_case)
    std::vector<std::string> aliases;     ///< more OkayScript names for the same function
    std::string typedType;                ///< the typed language's type ("SaveGame"); "" = OkayScript only
    std::string typedName;                ///< the member name there ("Save")
    ScriptApiKind kind = ScriptApiKind::Function;
    std::vector<ScriptApiParam> params;
    ScriptApiType ret = ScriptApiType::Void;
    ScriptApiFn fn;
    std::string doc;
    /// Typed language: the fewest arguments a call may give (one overload per count up to params.size());
    /// -1 = every parameter is required there. OkayScript always accepts the optional ones left out.
    int typedMinParams = -1;
    bool pure = false;                    ///< no side effects: a value node in a visual script
    /// OkayScript only: give a Bool result as 1 / 0 (what the old OkayScript save functions returned).
    bool boolAsNumber = false;
    /// Module that adds this function. Empty = always available. A switched-off module's functions
    /// are not installed into a new script VM.
    std::string module;
    /// Typed language only: a compile-time check of a literal argument ("" = fine). Same shape as
    /// script::ApiMethod::checkLiteral.
    std::function<std::string(int argIndex, const std::string& literal)> checkLiteral;
};

/// Add an OkayScript-only function (the form engine modules use).
void AddScriptApi(const std::string& name, ScriptApiFn fn);
/// Add (or replace, by `name`) a function every language sees.
void AddScriptApi(ScriptApiEntry entry);
/// Every shared function, in registration order (the engine's own come first).
const std::vector<ScriptApiEntry>& ScriptApis();
/// The entry with this OkayScript name or alias, or null.
const ScriptApiEntry* FindScriptApi(const std::string& okayName);

/// Run an entry for a dynamically typed caller (OkayScript): `raw` is what the script passed. Each
/// declared parameter takes the next argument converted to its type; a Vector3 parameter takes either a
/// vector or three numbers (add_force(0, 5, 0)); a left-out argument is the parameter's default. A
/// ScriptApiError becomes a warning (logged once per `warned` flag) and the empty result. With `error` set the problem
/// is written there instead of logged (the automation API's script.call reports it to its caller).
vs::VsValue CallScriptApiDynamic(const ScriptApiEntry& e, GameObject* self, const std::vector<vs::VsValue>& raw,
                                 bool* warned, std::string* error = nullptr);

/// How many times a language tried to define a function the shared table already has (a second copy).
/// Tests require 0: a function lives in the shared table or in a language's own map, never both.
int ScriptApiDuplicateCount();
void NoteScriptApiDuplicate(const std::string& language, const std::string& name);

namespace detail {
/// The engine's own shared functions (ScriptApiCore.cpp), appended to `out`. Called once, when the
/// table is first used.
void RegisterCoreScriptApis(std::vector<ScriptApiEntry>& out);
}

} // namespace okay
