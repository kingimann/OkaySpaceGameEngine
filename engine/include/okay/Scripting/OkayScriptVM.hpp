#pragma once
#include "okay/Scripting/ScriptVM.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// The built-in, dependency-free scripting backend. It implements a small
/// Lua/JavaScript-flavored language (variables, arithmetic & logic, if/while,
/// functions, and host builtins like move(), rotate(), time(), key()). Because
/// it ships in the engine, scripting works everywhere — including the prebuilt
/// Windows .exe — with no external runtime.
///
/// Define start() and/or update(dt) in your script and the component will call
/// them at the right time.
class OkayScriptVM : public IScriptVM {
public:
    OkayScriptVM();
    ~OkayScriptVM() override;

    const char* Language() const override { return "okayscript"; }
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
    std::vector<std::string> GlobalNames() const override;
    void SetGlobal(const std::string& name, const vs::VsValue& v) override;

    /// Add (or replace) a host-defined builtin callable from this VM's scripts —
    /// how embedders (e.g. editor plugins) expose their own API. Call before Load.
    using Builtin = std::function<vs::VsValue(std::vector<vs::VsValue>&)>;
    void AddBuiltin(const std::string& name, Builtin fn);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace okay
