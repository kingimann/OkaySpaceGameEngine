#pragma once
#include "okay/Scripting/ScriptVM.hpp"
#include <memory>

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
    bool Load(const std::string& source, std::string* error = nullptr) override;
    void Bind(ScriptHost* host) override;
    void CallStart() override;
    void CallUpdate(float deltaTime) override;
    void CallEvent(const std::string& function) override;
    vs::VsValue GetGlobal(const std::string& name) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace okay
