#pragma once
#include "okay/VisualScript/VsValue.hpp"
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class Transform;
class GameObject;

/// The engine-side context a script can see and manipulate. Every script VM
/// (built-in, Lua, C#) binds against this same surface, so a game can swap
/// languages without changing the engine integration.
struct ScriptHost {
    Transform*  transform  = nullptr;
    GameObject* gameObject = nullptr;
    float       deltaTime  = 0.0f;
    /// A shared blackboard scripts can read/write (also visible to gameplay code).
    std::unordered_map<std::string, vs::VsValue> globals;
};

/// One syntax problem found by ValidateAll — a 1-based source line and a
/// human-readable message. The editor lists these in its Problems panel.
struct ScriptDiagnostic {
    int         line = 0;
    std::string message;
};

/// Abstract scripting backend. Load source once, then drive Start/Update.
class IScriptVM {
public:
    virtual ~IScriptVM() = default;

    /// The language this backend implements ("okayscript", "lua", "csharp").
    virtual const char* Language() const = 0;

    /// The names of every built-in function this backend provides — for editor
    /// autocomplete. Default empty; OkayScript returns its full builtin set.
    virtual std::vector<std::string> BuiltinNames() const { return {}; }

    /// Compile/parse the given source. Returns false and fills `error` on failure.
    virtual bool Load(const std::string& source, std::string* error = nullptr) = 0;

    /// Syntax-check the source WITHOUT executing it (no side effects) — for live
    /// editor diagnostics. Returns false and fills `error` (e.g. "line 3: ...") on a
    /// parse error. Default falls back to Load for backends that can't parse-only.
    virtual bool Validate(const std::string& source, std::string* error = nullptr) {
        return Load(source, error);
    }

    /// Parse-check the source and report EVERY syntax error, not just the first —
    /// via statement-level error recovery — so the editor's Problems panel can list
    /// them all at once. Returns an empty vector when the source is clean. The
    /// default reports at most the single error Validate finds, so backends without
    /// recovery still work.
    virtual std::vector<ScriptDiagnostic> ValidateAll(const std::string& source) {
        std::string e;
        if (Validate(source, &e)) return {};
        int line = 0;
        if (e.rfind("line ", 0) == 0) line = std::atoi(e.c_str() + 5);
        return { ScriptDiagnostic{ line, e } };
    }

    /// Bind the host context the script operates on (called before Start/Update).
    virtual void Bind(ScriptHost* host) = 0;

    /// Invoke the script's start()/update(dt) entry points if present.
    virtual void CallStart() = 0;
    virtual void CallUpdate(float deltaTime) = 0;

    /// Invoke a named zero-argument event handler if the script defines it
    /// (e.g. "on_trigger", "on_collision"). Default: no-op, so backends that
    /// don't implement events still compile.
    virtual void CallEvent(const std::string& /*function*/) {}

    /// Read back a global the script defined (for tests and gameplay glue).
    virtual vs::VsValue GetGlobal(const std::string& name) const = 0;

    /// Set a global variable — used to apply a component's serialized public-field
    /// overrides over the script's defaults (Unity-style inspector values).
    virtual void SetGlobal(const std::string& /*name*/, const vs::VsValue& /*v*/) {}
};

/// Names of the script backends compiled into this build (always includes
/// "okayscript"; "lua"/"csharp" appear when their CMake options are enabled).
std::vector<std::string> AvailableScriptLanguages();

/// Create a VM for the given language, or nullptr if unsupported in this build.
std::unique_ptr<IScriptVM> CreateScriptVM(const std::string& language);

} // namespace okay
