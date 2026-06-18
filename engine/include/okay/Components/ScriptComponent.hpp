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
    void SetLanguage(const std::string& lang) { m_language = lang; }
    /// The last source compiled (retained for editing and serialization).
    const std::string& Source() const { return m_source; }
    /// Optional path to an external script file (edit in your IDE; Reload picks
    /// up changes). Empty for inline scripts.
    const std::string& Path() const { return m_path; }
    void SetPath(const std::string& path) { m_path = path; }
    /// Re-read and recompile from the external file (no-op without a path).
    bool Reload(std::string* error = nullptr) {
        return m_path.empty() ? false : LoadFile(m_path, error);
    }
    IScriptVM* VM() const { return m_vm.get(); }
    ScriptHost& Host() { return m_host; }

    void Start() override;
    void Update(float dt) override;

    // Forward physics messages to optional script handlers: a script can define
    // on_trigger() and on_collision() to react to overlaps/contacts.
    void OnTriggerEnter2D(Collider2D* /*other*/) override {
        if (m_vm) m_vm->CallEvent("on_trigger");
    }
    void OnCollisionEnter2D(const Collision2D& /*c*/) override {
        if (m_vm) m_vm->CallEvent("on_collision");
    }

private:
    std::string m_language;
    std::string m_source;
    std::string m_path;
    std::unique_ptr<IScriptVM> m_vm;
    ScriptHost m_host;
};

} // namespace okay
