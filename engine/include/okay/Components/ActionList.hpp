#pragma once
#include "okay/Scene/Component.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class Collider2D;
struct Collision2D;
class Collider3D;
struct Collision3D;

/// A Game-Creator-style visual script: a **Trigger** fires the list, optional
/// **Conditions** gate it, then **Instructions** run top-to-bottom (with Wait
/// pausing between steps). Fully data-driven — no code — so it can be authored
/// from dropdowns in the editor.
class ActionList : public Behaviour {
public:
    // Appended in order — existing values must stay stable for serialization
    // (triggers serialize by integer index).
    enum class Trigger { OnStart, OnUpdate, OnKey, OnCollision, OnClick, OnKeyUp, OnMessage,
                         OnTriggerEnter, OnTriggerExit, OnMouseEnter, OnMouseExit,
                         OnMouseDown, OnMouseUp, OnMouseOver };

    /// One condition or instruction: an op name + string args (numbers parsed
    /// on use, so the data stays uniform and easy to edit/serialize).
    struct Item {
        std::string op;
        std::vector<std::string> args;
    };

    Trigger trigger = Trigger::OnStart;
    std::string triggerKey = "e";     // OnKey: which key starts the list
    bool once = false;                // fire at most once

    std::vector<Item> conditions;
    std::vector<Item> instructions;

    void Start() override;
    void Update(float dt) override;

    // Event-driven triggers latch a pending fire, run on the next Update tick.
    void OnTriggerEnter2D(Collider2D*) override        { if (trigger == Trigger::OnCollision || trigger == Trigger::OnTriggerEnter) m_pending = true; }
    void OnTriggerExit2D (Collider2D*) override        { if (trigger == Trigger::OnTriggerExit) m_pending = true; }
    void OnCollisionEnter2D(const Collision2D&) override{ if (trigger == Trigger::OnCollision) m_pending = true; }
    void OnTriggerEnter3D(Collider3D*) override        { if (trigger == Trigger::OnCollision || trigger == Trigger::OnTriggerEnter) m_pending = true; }
    void OnTriggerExit3D (Collider3D*) override        { if (trigger == Trigger::OnTriggerExit) m_pending = true; }
    void OnCollisionEnter3D(const Collision3D&) override{ if (trigger == Trigger::OnCollision) m_pending = true; }
    void OnMouseEnter() override { if (trigger == Trigger::OnMouseEnter) m_pending = true; }
    void OnMouseExit()  override { if (trigger == Trigger::OnMouseExit)  m_pending = true; }
    void OnMouseOver()  override { if (trigger == Trigger::OnMouseOver)  m_pending = true; }
    void OnMouseDown()  override { if (trigger == Trigger::OnMouseDown)  m_pending = true; }
    void OnMouseUp()    override { if (trigger == Trigger::OnMouseUp)    m_pending = true; }
    void OnMouseClick() override { if (trigger == Trigger::OnClick)      m_pending = true; }

    bool IsRunning() const { return m_running; }

    /// Deliver a named signal: fires this list if it's an OnMessage trigger
    /// listening for `msg`. Sent by the `send` instruction or send_message().
    void ReceiveMessage(const std::string& msg) {
        if (trigger == Trigger::OnMessage && triggerKey == msg) Fire();
    }

    /// Compact text form (one line per trigger / condition / instruction), for
    /// serialization and the editor. Round-trips through FromText().
    std::string ToText() const;
    void FromText(const std::string& text);

    /// Shared variables for set_var / var_eq across all action lists. Cleared
    /// between scenes via Reset().
    static std::unordered_map<std::string, float>& Vars();
    static void ResetVars();

private:
    void Fire();
    bool EvalConditions();

    bool m_running = false;
    std::size_t m_ip = 0;
    float m_wait = 0.0f;
    bool m_fired = false;
    bool m_pending = false;    // latched by event callbacks (collision/trigger/mouse)
};

} // namespace okay
