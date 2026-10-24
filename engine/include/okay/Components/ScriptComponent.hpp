#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include <memory>
#include <string>
#include <unordered_map>

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
    /// Replace the retained source WITHOUT compiling/executing it — used by the editor
    /// when saving edits so the scene serializes the new text even before the next Run.
    void SetSource(const std::string& source) { m_source = source; }
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

    /// Per-instance overrides for the script's public variables, shown and edited
    /// in the Inspector (Unity-style serialized fields). Stored as text and
    /// applied over the script's defaults after each compile. The editor reads
    /// the script source to know which names exist; this map holds the values
    /// the user changed.
    std::unordered_map<std::string, std::string> fields;
    /// Apply the `fields` overrides to the live VM (parsing text -> bool/number/
    /// string). Called automatically after a successful compile.
    void ApplyFieldOverrides();

    void Start() override;
    void Update(float dt) override;

    // Forward engine messages to optional script handlers. A script just defines a
    // function with the matching name to react. Physics overlaps/contacts and
    // pointer/mouse events all flow through here; 2D and 3D map to the same names,
    // and the legacy on_trigger()/on_collision() still fire on enter (see the VM's
    // alias table) so older scripts keep working.
    /// Clip events: `anim_event` holds the name, then the handler named like the
    /// event runs (Unity-style), then on_anim_event().
    void OnAnimationEvent(const std::string& name) override {
        if (!m_vm) return;
        m_vm->SetGlobal("anim_event", vs::VsValue(name));
        if (!name.empty()) m_vm->CallEvent(name);
        m_vm->CallEvent("on_anim_event");
    }
    // The other object reaches the script as `other` (its name), and as the
    // argument of a handler that declares one: OnTriggerEnter(Collider other).
    void OnTriggerEnter2D(Collider2D* c) override         { FireOther("on_trigger_enter", c); }
    void OnTriggerStay2D (Collider2D* c) override         { FireOther("on_trigger_stay", c); }
    void OnTriggerExit2D (Collider2D* c) override         { FireOther("on_trigger_exit", c); }
    void OnCollisionEnter2D(const Collision2D& c) override { FireOther("on_collision_enter", c); }
    void OnCollisionStay2D (const Collision2D& c) override { FireOther("on_collision_stay", c); }
    void OnCollisionExit2D (const Collision2D& c) override { FireOther("on_collision_exit", c); }

    void OnTriggerEnter3D(Collider3D* c) override          { FireOther("on_trigger_enter", c); }
    void OnTriggerStay3D (Collider3D* c) override          { FireOther("on_trigger_stay", c); }
    void OnTriggerExit3D (Collider3D* c) override          { FireOther("on_trigger_exit", c); }
    void OnCollisionEnter3D(const Collision3D& c) override { FireOther("on_collision_enter", c); }
    void OnCollisionStay3D (const Collision3D& c) override { FireOther("on_collision_stay", c); }
    void OnCollisionExit3D (const Collision3D& c) override { FireOther("on_collision_exit", c); }

    void OnMouseEnter() override { Fire("on_mouse_enter"); }
    void OnMouseExit()  override { Fire("on_mouse_exit"); }
    void OnMouseOver()  override { Fire("on_mouse_over"); }
    void OnMouseDown()  override { Fire("on_mouse_down"); }
    void OnMouseUp()    override { Fire("on_mouse_up"); }
    void OnMouseClick() override { Fire("on_click"); }

private:
    void Fire(const char* event) { if (m_vm) m_vm->CallEvent(event); }
    void FireOther(const char* event, Collider2D* c);
    void FireOther(const char* event, Collider3D* c);
    void FireOther(const char* event, const Collision2D& c);
    void FireOther(const char* event, const Collision3D& c);
    void FireWithObject(const char* event, GameObject* other);
    void SetContact(const Vec3& point, const Vec3& normal, float impactSpeed, GameObject* other);

    std::string m_language;
    std::string m_source;
    std::string m_path;
    std::unique_ptr<IScriptVM> m_vm;
    ScriptHost m_host;
};

} // namespace okay
