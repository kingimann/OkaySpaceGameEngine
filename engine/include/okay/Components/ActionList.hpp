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

    std::string name;                 // optional label so several scripts are tellable apart
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
    // Index of the instruction about to run (or waiting) while running, else -1.
    // Used by the Flow Graph to highlight the live node during Play.
    int  CurrentInstruction() const { return m_running ? (int)m_ip : -1; }

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
    // Read a variable by name: the shared visual-script pool first, then a saved
    // (Prefs) value — so stats/prefs published under a name are readable as variables.
    static float GetVar(const std::string& key);
    // Shared float arrays (array_push/get/... ops). Cleared with Vars on Reset.
    static std::unordered_map<std::string, std::vector<float>>& Arrays();
    // Shared maps/dictionaries (string key -> float value). Cleared on Reset.
    static std::unordered_map<std::string, std::unordered_map<std::string, float>>& Maps();
    // Shared text variables (str_set/str_eq/... ops). Cleared on Reset. Displayed by
    // a UITextBind {key} the same as numbers, so names/dialogue can go on screen.
    static std::unordered_map<std::string, std::string>& StrVars();
    static void ResetVars();

    // ---- Step debugging (driven from the editor's Flow Graph) --------------
    // When paused, running lists execute only StepBudget() instructions, then hold
    // at the next one. The editor grants budget with each "Step" and shows the live
    // node. Off by default, so shipped games are unaffected.
    static bool& DebugPaused();
    static int&  StepBudget();

private:
    void Fire();
    bool EvalConditions();
    // Resolve a goto/if_goto target: a numeric line, or the index of a `label` op.
    int  ResolveTarget(const std::string& t) const;
    // Index of the matching `endOp` for a block opener at `openIp` (handles nesting).
    int  MatchingEnd(std::size_t openIp, const char* openOp, const char* endOp) const;

    bool m_running = false;
    std::size_t m_ip = 0;
    float m_wait = 0.0f;
    bool m_fired = false;
    bool m_pending = false;    // latched by event callbacks (collision/trigger/mouse)
    // Loop frames (repeat / while / for-each) and subroutine return addresses.
    struct LoopFrame {
        int kind = 0;              // 0 = repeat, 1 = while, 2 = for-each
        std::size_t headIp = 0;    // the loop's opening instruction
        std::size_t bodyStart = 0; // first instruction of the body
        std::size_t endIp = 0;     // the matching end_* instruction
        int remaining = 0;         // repeat: iterations left
        std::string arr, idxVar, valVar;  // for-each state
        std::size_t idx = 0;
    };
    std::vector<LoopFrame>   m_loops;
    std::vector<std::size_t> m_callStack;
};

} // namespace okay
