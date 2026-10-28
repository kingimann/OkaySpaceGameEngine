#pragma once
#include "okay/Scripting/ScriptVM.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okay {

class Scene;

/// The new OkayScript: C#-style classes compiled and checked before they run
/// (engine/src/Script). A script is a class extending Behaviour; its fields are
/// the component's settings and the engine calls its Awake / OnEnable / Start /
/// Update / LateUpdate / FixedUpdate / OnDisable / OnDestroy by name.
///
/// Language id "okayscript-next" while it grows to replace the older VM.
class NextScriptVM : public IScriptVM {
public:
    static constexpr const char* kLanguage = "okayscript-next";

    NextScriptVM();
    ~NextScriptVM() override;

    const char* Language() const override { return kLanguage; }
    /// Every engine type and member scripts can use (for autocomplete).
    std::vector<std::string> BuiltinNames() const override;

    bool Load(const std::string& source, std::string* error = nullptr) override;
    bool Validate(const std::string& source, std::string* error = nullptr) override;
    std::vector<ScriptDiagnostic> ValidateAll(const std::string& source) override;

    void Bind(ScriptHost* host) override;
    void CallStart() override;
    void CallUpdate(float deltaTime) override;
    void CallEvent(const std::string& function) override;

    vs::VsValue GetGlobal(const std::string& name) const override;
    std::vector<std::string> GlobalNames() const override;
    void SetGlobal(const std::string& name, const vs::VsValue& v) override;

    /// Drop the `static` field values of scripts in `scene`: they last one play
    /// session (called when the scene is cleared or destroyed).
    static void ForgetSceneStatics(const Scene* scene);

    /// The last runtime error ("line N: message"), empty if none.
    const std::string& LastError() const { return m_lastError; }
    /// How many runtime errors happened (each one is also logged).
    int ErrorCount() const { return m_errorCount; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    ScriptHost* m_host = nullptr;
    std::string m_lastError;
    int m_errorCount = 0;
    bool RunMethod(const std::string& name);
};

} // namespace okay
