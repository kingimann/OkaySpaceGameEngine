#pragma once
#include "okay/Scripting/ScriptVM.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Lua scripts (language id "lua", `.lua` files) on our own interpreter
/// (okay::lua, engine/src/Lua). A script is a chunk that returns a table:
///
///     local Player = { speed = 5 }          -- fields are the Inspector settings
///     function Player:Start() end
///     function Player:Update(dt)
///         self.transform:Translate(0, 0, self.speed * dt)
///     end
///     function Player:OnTriggerEnter(other) print(other.name) end
///     return Player
///
/// Scripts that return nothing work too: their global functions are the
/// handlers (`function update(dt) ... end`, as old Lua scenes wrote them).
/// The engine API is the typed-language binding table (Script/EngineApi): the
/// same GameObject, Transform, Vector3, Input, Time, Debug, ... the C#-style
/// language and the visual-script Engine API nodes use.
class LuaScriptVM : public IScriptVM {
public:
    static constexpr const char* kLanguage = "lua";
    /// Instructions one callback (Start, Update, an event) may run before it is stopped.
    static constexpr std::uint64_t kDefaultBudget = 20'000'000;

    LuaScriptVM();
    ~LuaScriptVM() override;

    const char* Language() const override { return kLanguage; }
    std::vector<std::string> BuiltinNames() const override;

    bool Load(const std::string& source, std::string* error = nullptr) override;
    bool Validate(const std::string& source, std::string* error = nullptr) override;
    std::vector<ScriptDiagnostic> ValidateAll(const std::string& source) override;

    void Bind(ScriptHost* host) override;
    void CallStart() override;
    void CallUpdate(float deltaTime) override;
    void CallEvent(const std::string& function) override;
    void CallEventWith(const std::string& function, const vs::VsValue& arg) override;
    vs::VsValue CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& args,
                              std::string* error = nullptr) override;
    bool HasEvent(const std::string& function) const override;

    vs::VsValue GetGlobal(const std::string& name) const override;
    void SetGlobal(const std::string& name, const vs::VsValue& v) override;
    std::vector<std::string> GlobalNames() const override;

    /// The last runtime error ("script:12: message"), empty if none.
    const std::string& LastError() const { return m_lastError; }
    int ErrorCount() const { return m_errorCount; }
    /// Change the per-callback instruction budget (tests).
    void SetBudget(std::uint64_t instructions) { m_budget = instructions; }

    /// The Lua words, library functions and bound engine names (for the editor).
    static std::vector<std::string> CompletionNames();
    /// What can follow `receiver.` or `receiver:` (the text before the dot, e.g.
    /// "Input", "self.transform", "string"), for member autocomplete. Empty when unknown.
    static std::vector<std::string> CompletionMembers(const std::string& receiver);
    /// Warnings a syntax check can not see: an engine name that does not exist
    /// (Input.GetKeyy) and a handler that is never called (function M:Updte).
    static std::vector<ScriptDiagnostic> Lint(const std::string& source);
    /// One Inspector setting of a Lua script: a field of the returned table (or a
    /// global) holding a number, string, boolean or Vector3.
    struct Setting { std::string name, type, defaultText; };
    /// The script's settings in source order, found by running the chunk in a
    /// throwaway interpreter (no object attached, small budget). Empty on an error.
    static std::vector<Setting> Settings(const std::string& source);

    struct Impl;   ///< internal (engine/src/Lua/LuaScriptVM.cpp)
private:
    std::unique_ptr<Impl> m_impl;
    ScriptHost* m_host = nullptr;
    std::string m_lastError;
    int m_errorCount = 0;
    std::uint64_t m_budget = kDefaultBudget;
    bool m_quiet = false;   ///< print() goes nowhere (the Inspector's throwaway runs)
    bool Run(const std::string& handler, const std::vector<vs::VsValue>& args, vs::VsValue* ret, std::string* error);
};

} // namespace okay
