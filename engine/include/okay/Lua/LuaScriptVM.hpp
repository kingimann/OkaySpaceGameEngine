#pragma once
#include "okay/Scripting/ScriptVM.hpp"
#include <memory>

namespace okay {

/// The engine binding for our own Lua 5.4 (engine/src/Lua). Language name "lua", files `.lua`.
/// It implements the same IScriptVM seam as every other language, so ScriptComponent runs it
/// without a second component.
///
/// A script is a Lua file whose global functions are the entry points: Awake, OnEnable, Start,
/// Update(dt), FixedUpdate, LateUpdate, OnDisable, OnDestroy, and the event handlers
/// (OnTriggerEnter(other), OnCollisionEnter(other) ...; the snake_case names such as
/// on_collision_enter work too). Global variables that are numbers, strings, booleans or vectors
/// are the script's public fields (inspector overrides and save games use them).
class LuaScriptVM : public IScriptVM {
public:
    LuaScriptVM();
    ~LuaScriptVM() override;

    static constexpr const char* kLanguage = "lua";
    const char* Language() const override { return kLanguage; }
    std::vector<std::string> BuiltinNames() const override;

    bool Load(const std::string& source, std::string* error = nullptr) override;
    bool Validate(const std::string& source, std::string* error = nullptr) override;
    std::vector<ScriptDiagnostic> ValidateAll(const std::string& source) override;
    /// Hot reload: compile `source` in a fresh interpreter, copy the public fields over, and swap.
    /// On a compile or load error the running script is left alone and false is returned.
    bool HotReload(const std::string& source, std::string* error) override;
    bool SupportsHotReload() const override { return true; }

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

    /// Estimated bytes the script uses (for tests and the memory cap).
    std::size_t MemoryUsed() const;
    /// Limits per entry call (instructions, seconds, bytes). Defaults: 5M instructions, 0.5 s, 64 MB.
    void SetLimits(std::int64_t instructions, double seconds, std::size_t memoryBytes);

    struct Impl;
private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace okay
