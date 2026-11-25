#pragma once
// Language-neutral helpers for text-language script backends that sit on the
// typed binding table (Script/EngineApi) and the generated binding layer
// (Scripting/ScriptBindings): which handler names an engine event looks for, the
// names the engine itself sets on a script, how an engine type reads in an error
// message, signatures and hover text, member lists for autocomplete, the lint's
// "did you mean", the generated layer's deny rules applied to typed-table methods
// and the handle lookups of the generated layer. JavaScript uses all of these
// (JavaScript/JavaScriptVM.cpp). The Lua backend still has private copies of the
// same rules in engine/src/Lua/LuaScriptVM.cpp (Claude 3's lane): moving it onto
// this file is handed over on #422, and until then those copies carry a
// dup-allow mark with that reason here.
#include "../Script/ScriptInternal.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <set>
#include <string>
#include <vector>

namespace okay {
class GameObject;
}

namespace okay::scriptglue {

/// Engine types a language hides (named like one of its own library objects, or denied).
using HiddenFn = bool (*)(const script::ApiType& t);

// ---- handlers and settings
/// The handler names to try, in order, for an engine event name ("on_fixed_update" -> FixedUpdate,
/// fixed_update; "on_trigger_enter" -> on_trigger_enter, OnTriggerEnter; "Update" -> Update, update).
std::vector<std::string> HandlerNamesFor(const std::string& engineName);
/// Names the engine sets on a running script (other, contact_*, gameObject, transform): never settings.
bool IsEngineSetName(const std::string& name);
/// The lifecycle handlers every text language gets.
const std::vector<std::string>& LifecycleHandlers();
/// A one-line description of a lifecycle handler, or "" when `name` is not one.
std::string HandlerDoc(const std::string& name);
/// For a handler definition named `name`: "" when it is a handler (or not close to one), else the warning
/// "Updte is never called by the engine. Did you mean Update? ...".
std::string HandlerTypo(const std::string& name);
/// Where `word` first appears in `source` as a whole word (letters, digits, _ and $), or npos.
std::size_t FirstWordPos(const std::string& source, const std::string& word);
/// One Inspector setting from a script value: type "number", "string", "boolean" or "Vector3" and its default
/// text ("5", "\"ship\"", "true", "Vector3(1, 2.5, 0)"). False for any other kind of value.
bool SettingText(const vs::VsValue& v, std::string& type, std::string& text, std::string (*number)(double));
/// Write a failed handler's message to the Console the way every backend does.
void ReportHandlerError(const GameObject* self, const std::string& message);

// ---- the typed binding table
/// How an engine type reads in messages ("number", "whole number", "Vector3", "GameObject").
std::string TypeWord(const script::Type& t);
/// "Owner<sep>Name(a: number, b: string) -> boolean".
std::string Signature(const std::string& owner, const char* sep, const script::ApiMethod& m);
/// A typed-table method the generated layer's DENY_* rules leave out (file, network, platform, clipboard).
bool IsDenied(const script::ApiType& t, const script::ApiMethod& m);
/// True when the type has a method `name` scripts may call.
bool HasCallable(const script::ApiType& t, const std::vector<script::ApiMethod>& list, const std::string& name);
/// The checkLiteral message for converted arguments ("Input.GetKey: 'notakey' isn't a key name"), or "".
std::string LiteralProblem(const script::ApiType& t, const script::ApiMethod& m, const std::vector<script::Value>& args);

// ---- the generated binding layer
/// The generated layer's handle for the first component of the bound class named like the typed-table type,
/// on `g`. False when there is no such class or component.
bool HandleOnObject(GameObject& g, const std::string& typeName, vs::VsValue& out);
/// True when the component behind `handle` has a generated method `name`.
bool HandleHasMethod(const vs::VsValue& handle, const std::string& name);
/// {r, g, b[, a]} fields as a colour: `field(name, fallback)` reads one. False when no r, g or b is there.
bool ColorFromFields(double (*field)(void* ctx, const char* name, double fallback), void* ctx, vs::VsValue& out);

// ---- editor support
void AddMembers(const script::ApiType& t, bool statics, std::set<std::string>& out);
/// Every bound type name, member name and "Type.staticMember".
void AddApiNames(std::set<std::string>& out, HiddenFn hidden);
/// Hover text for an engine name ("Input.GetKey", "Translate", "Vector3"), or "". `instanceSep` joins a type and
/// an instance method in the text (":" for Lua's obj:Method, "." for JavaScript).
std::string ApiDoc(const std::string& name, const char* instanceSep, HiddenFn hidden);
/// Engine members that can follow `receiver.` (a static type, "transform", "gameObject", "other"). Returns 3 when
/// the receiver is a Vector3-valued property (the caller adds its vector members), 1 when it filled `out`, else 0.
int ApiReceiverMembers(const std::string& receiver, HiddenFn hidden, std::set<std::string>& out);
/// The lint for `Type.member` on a static engine type: "" when fine, else "Input has no member 'GetKeyy'. Did you
/// mean GetKey?". `extra` are member names the language adds (Vector3's constructor helpers, "prototype").
std::string StaticMemberProblem(const std::string& typeName, const std::string& member, HiddenFn hidden,
                                const std::vector<std::string>& extra);

} // namespace okay::scriptglue
