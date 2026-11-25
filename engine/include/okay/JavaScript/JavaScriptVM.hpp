#pragma once
#include "okay/Scripting/ScriptVM.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

namespace js { class Runtime; }

/// JavaScript game scripts (language id "javascript", `.js` files) on our own
/// engine okay::js (engine/src/JavaScript). A script exports its behaviour:
///
///     export default class Player {
///         speed = 5;                        // fields are the Inspector settings
///         Start() { console.log("hi " + this.gameObject.name); }
///         Update(dt) { this.transform.Translate(0, 0, this.speed * dt); }
///         OnTriggerEnter(other) { console.log(other.name); }
///     }
///
/// `export default { speed: 5, Update(dt) { ... } }` works the same, and a
/// script without an export uses its global functions (`function Update(dt) {}`).
/// The engine API is the same one Lua and the C#-style language get: the typed
/// binding table (Script/EngineApi) first, then the generated binding layer
/// (Scripting/ScriptBindings) for every other component and member.
class JavaScriptVM : public IScriptVM {
public:
    static constexpr const char* kLanguage = "javascript";
    /// Instructions (and library work) one callback (Start, Update, an event) may use before it is stopped.
    static constexpr std::uint64_t kDefaultBudget = 20'000'000;

    JavaScriptVM();
    ~JavaScriptVM() override;

    const char* Language() const override { return kLanguage; }
    OKAY_SCRIPT_VM_OVERRIDES   // the IScriptVM overrides every backend declares (ScriptVM.hpp)

    // dup-allow-begin: the statics every text-language backend gives the editor (LuaScriptVM declares the same set)
    /// The last runtime error ("script:12: TypeError: message"), empty if none.
    const std::string& LastError() const { return m_lastError; }
    int ErrorCount() const { return m_errorCount; }
    /// Change the per-callback budget (tests).
    void SetBudget(std::uint64_t instructions) { m_budget = instructions; }
    /// Change the heap limit in bytes (tests; default 256 MB).
    void SetMemoryLimit(std::size_t bytes);

    /// The JavaScript words, library names and bound engine names (for the editor).
    static std::vector<std::string> CompletionNames();
    /// What can follow `receiver.` ("Input", "this.transform", "Math"), for member autocomplete.
    static std::vector<std::string> CompletionMembers(const std::string& receiver);
    /// A one-line description of a completion name for hover tooltips, or "".
    static std::string CompletionDoc(const std::string& name);
    /// Warnings a syntax check can not see: an engine name that does not exist (Input.GetKeyy)
    /// and a handler that is never called (Updte).
    static std::vector<ScriptDiagnostic> Lint(const std::string& source);
    /// One Inspector setting: a field of the exported class or object (or a top-level variable)
    /// holding a number, string, boolean or Vector3.
    struct Setting { std::string name, type, defaultText; };
    /// The script's settings in source order, from running it in a throwaway engine (small budget,
    /// no output). Empty on an error.
    static std::vector<Setting> Settings(const std::string& source);
    /// Re-indent by brackets ({ [ ( open a level, } ] ) close one), 4 spaces a level; the inside of
    /// multi-line strings and template literals is kept as it is.
    static std::string Format(const std::string& source);
    // dup-allow-end

    struct Impl;   ///< internal (engine/src/JavaScript/JavaScriptVM.cpp)
private:
    std::unique_ptr<Impl> m_impl;
    ScriptHost* m_host = nullptr;
    std::string m_lastError;
    int m_errorCount = 0;
    std::uint64_t m_budget = kDefaultBudget;
    std::size_t m_memoryLimit = 256u << 20;
    bool m_quiet = false;
    bool Run(const std::string& handler, const std::vector<vs::VsValue>& args, vs::VsValue* ret, std::string* error);
    void BindObjects(js::Runtime& rt, ScriptHost* host);
};

} // namespace okay
