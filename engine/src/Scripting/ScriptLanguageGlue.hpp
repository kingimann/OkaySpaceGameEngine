#pragma once
// Language-neutral helpers for text-language script backends that sit on the
// typed binding table (Script/EngineApi) and the generated binding layer
// (Scripting/ScriptBindings): which handler names an engine event looks for,
// the names the engine itself sets on a script, how an engine type reads in an
// error message, a method's signature for hover text, and the generated layer's
// deny rules applied to typed-table methods. JavaScript uses these
// (JavaScript/JavaScriptVM.cpp); the Lua backend still has its own private copies
// (engine/src/Lua/LuaScriptVM.cpp, Claude 3's lane) and can move onto this file.
#include "../Script/ScriptInternal.hpp"
#include <string>
#include <vector>

namespace okay::scriptglue {

/// The handler names to try, in order, for an engine event name ("on_fixed_update" -> FixedUpdate,
/// fixed_update; "on_trigger_enter" -> on_trigger_enter, OnTriggerEnter; "Update" -> Update, update).
std::vector<std::string> HandlerNamesFor(const std::string& engineName);
/// Names the engine sets on a running script (other, contact_*, gameObject, transform): never settings.
bool IsEngineSetName(const std::string& name);
/// The lifecycle handlers every text language gets, in the order the docs list them.
const std::vector<std::string>& LifecycleHandlers();
/// A one-line description of a lifecycle handler, or "" when `name` is not one.
std::string HandlerDoc(const std::string& name);
/// How an engine type reads in messages ("number", "whole number", "Vector3", "GameObject").
std::string TypeWord(const script::Type& t);
/// "Owner<sep>Name(a: number, b: string) -> boolean".
std::string Signature(const std::string& owner, const char* sep, const script::ApiMethod& m);
/// A typed-table method the generated layer's DENY_* rules leave out (file, network, platform, clipboard).
bool IsDenied(const script::ApiType& t, const script::ApiMethod& m);

} // namespace okay::scriptglue
