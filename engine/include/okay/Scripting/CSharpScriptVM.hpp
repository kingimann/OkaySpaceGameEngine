#pragma once
#include "okay/Scripting/CSharpBridge.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include <memory>

namespace okay {

/// Real C# (.cs files) through the hosted .NET runtime, behind the same IScriptVM seam as every other
/// language. Language name "csharp". A script is a class that inherits Behaviour from OkayScripting.dll.
/// Without OKAY_WITH_CSHARP this class still exists but Load() fails with "C# is not enabled in this build".
class CSharpScriptVM : public IScriptVM {
public:
    CSharpScriptVM();
    ~CSharpScriptVM() override;
    static constexpr const char* kLanguage = "csharp";
    const char* Language() const override { return kLanguage; }
    std::vector<std::string> BuiltinNames() const override;

    bool Load(const std::string& source, std::string* error = nullptr) override;
    /// Reports what is already known (a cached compile); building is done by Load / csharp::CompileScript.
    bool Validate(const std::string& source, std::string* error = nullptr) override;
    std::vector<ScriptDiagnostic> ValidateAll(const std::string& source) override;
    /// New code in a fresh (collectible) load context, public fields copied over, old one dropped.
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

    /// Frame-time watchdog: a call over `ms` logs a warning (default 10 ms). It cannot stop the script.
    void SetBudgetMs(double ms);
    /// Number of calls that went over budget so far (tests).
    int OverBudgetCount() const;

    /// "on_fixed_update" -> "FixedUpdate", "on_trigger_enter" -> "OnTriggerEnter", "Update" -> "Update".
    static std::string ManagedMethodName(const std::string& event);

    struct Impl;
private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace okay
