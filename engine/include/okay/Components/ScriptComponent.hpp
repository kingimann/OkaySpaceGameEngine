#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include <memory>
#include <string>

namespace okay {

/// Runs a text script (OkayScript by default, or Lua / C# when those backends
/// are enabled) on a GameObject. The script's start() runs once and update(dt)
/// runs every frame — the same authoring model as Unity's MonoBehaviour, but in
/// a scripting language.
class ScriptComponent : public Behaviour {
public:
    /// Choose the scripting language up front (default: "okayscript").
    explicit ScriptComponent(std::string language = "okayscript")
        : m_language(std::move(language)) {}

    /// Compile a script from source. Returns false on error.
    bool LoadSource(const std::string& source, std::string* error = nullptr);
    /// Compile a script from a file.
    bool LoadFile(const std::string& path, std::string* error = nullptr);

    const std::string& Language() const { return m_language; }
    IScriptVM* VM() const { return m_vm.get(); }
    ScriptHost& Host() { return m_host; }

    void Start() override;
    void Update(float dt) override;

private:
    std::string m_language;
    std::unique_ptr<IScriptVM> m_vm;
    ScriptHost m_host;
};

} // namespace okay
