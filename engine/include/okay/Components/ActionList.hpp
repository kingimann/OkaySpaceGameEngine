#pragma once
#include "okay/Scene/Component.hpp"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay {

class Scene;
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
                         OnMouseDown, OnMouseUp, OnMouseOver,
                         // Appended (keep order stable for serialization):
                         OnCollisionExit,   // physics contact ended
                         OnCollisionStay,   // every frame contact persists
                         OnInterval,        // fire repeatedly every triggerKey seconds (a timer)
                         OnLateUpdate,      // every frame, after all Update()s
                         OnTriggerStay };   // every frame something stays inside a trigger

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

    std::vector<Item> conditions;     // handler 0's gate
    std::vector<Item> instructions;   // handler 0's actions

    /// A whole extra event handler in the SAME script — its own trigger, gate and
    /// actions, like adding another function to one script (OnStart + OnUpdate + OnKey,
    /// each doing different things). The primary trigger/conditions/instructions above
    /// are handler 0; these are the rest. One handler runs at a time.
    struct Handler {
        Trigger trigger = Trigger::OnUpdate;
        std::string triggerKey = "e";
        bool once = false;
        std::vector<Item> conditions;
        std::vector<Item> instructions;
        bool m_fired = false;
        float m_timer = 0.0f;   // OnInterval accumulator
    };
    std::vector<Handler> extraHandlers;

    /// True if this list responds to trigger `t` (as the primary or any extra handler).
    bool HasTrigger(Trigger t) const {
        if (trigger == t) return true;
        for (const Handler& h : extraHandlers) if (h.trigger == t) return true;
        return false;
    }

    /// A variable declared up-front (in the editor's Variables panel) with a starting
    /// value, seeded into the shared pool when the scene starts. type 0 = Number
    /// (into Vars), 1 = Text (into StrVars). Lets designers create variables without
    /// first writing a Set Variable action.
    struct VarDecl {
        std::string name;
        int         type = 0;         // 0 = number, 1 = text
        std::string value;            // stringified initial value
    };
    std::vector<VarDecl> variables;
    /// Put one declared variable's starting value into the shared pools.
    static void SeedVariable(const VarDecl& v);

    /// Saved Flow-Graph node positions so a hand-arranged layout survives save/load
    /// (the editor writes these when you drag a block). Key = "<hidx>:<role>:<idx>"
    /// (hidx -1 = the primary handler; role = trig/cond/ins). Ignored at runtime.
    std::unordered_map<std::string, std::pair<float, float>> nodeLayout;

    void Start() override;
    void Update(float dt) override;

    // Event-driven triggers latch the event type; the next Update fires the handler(s)
    // that listen for it.
    void OnTriggerEnter2D(Collider2D*) override        { Latch(Trigger::OnTriggerEnter); }
    void OnTriggerExit2D (Collider2D*) override        { Latch(Trigger::OnTriggerExit); }
    // Stay fires every frame, so only latch it when a handler wants it (the single
    // latch slot would otherwise hide other events from lists that don't).
    void OnTriggerStay2D (Collider2D*) override        { if (HasTrigger(Trigger::OnTriggerStay)) Latch(Trigger::OnTriggerStay); }
    void OnTriggerStay3D (Collider3D*) override        { if (HasTrigger(Trigger::OnTriggerStay)) Latch(Trigger::OnTriggerStay); }
    void OnCollisionEnter2D(const Collision2D&) override{ Latch(Trigger::OnCollision); }
    void OnCollisionExit2D (const Collision2D&) override{ Latch(Trigger::OnCollisionExit); }
    void OnCollisionStay2D (const Collision2D&) override{ Latch(Trigger::OnCollisionStay); }
    void OnTriggerEnter3D(Collider3D*) override        { Latch(Trigger::OnTriggerEnter); }
    void OnTriggerExit3D (Collider3D*) override        { Latch(Trigger::OnTriggerExit); }
    void OnCollisionEnter3D(const Collision3D&) override{ Latch(Trigger::OnCollision); }
    void OnCollisionExit3D (const Collision3D&) override{ Latch(Trigger::OnCollisionExit); }
    void OnCollisionStay3D (const Collision3D&) override{ Latch(Trigger::OnCollisionStay); }
    void LateUpdate(float dt) override;   // drives OnLateUpdate handlers
    void OnMouseEnter() override { Latch(Trigger::OnMouseEnter); }
    void OnMouseExit()  override { Latch(Trigger::OnMouseExit); }
    void OnMouseOver()  override { Latch(Trigger::OnMouseOver); }
    void OnMouseDown()  override { Latch(Trigger::OnMouseDown); }
    void OnMouseUp()    override { Latch(Trigger::OnMouseUp); }
    void OnMouseClick() override { Latch(Trigger::OnClick); }

    bool IsRunning() const { return m_running; }
    // Index of the instruction about to run (or waiting) while running, else -1.
    // Used by the Flow Graph to highlight the live node during Play.
    int  CurrentInstruction() const { return m_running ? (int)m_ip : -1; }

    /// Deliver a named signal: fires this list if it's an OnMessage trigger
    /// listening for `msg`. Sent by the `send` instruction or send_message().
    void ReceiveMessage(const std::string& msg) {
        if (trigger == Trigger::OnMessage && triggerKey == msg) { FireHandler(conditions, instructions, m_fired, once); return; }
        for (Handler& h : extraHandlers)
            if (h.trigger == Trigger::OnMessage && h.triggerKey == msg) { FireHandler(h.conditions, h.instructions, h.m_fired, h.once); return; }
    }

    /// Compact text form (one line per trigger / condition / instruction), for
    /// serialization and the editor. Round-trips through FromText().
    std::string ToText() const;
    void FromText(const std::string& text);

    /// Number variables for set_var / var_eq. Every action list in the scene that
    /// is running shares one pool; a second live scene has its own. With no scene
    /// running, this is the process-wide pool (tests and tools that have no Scene).
    /// The first time a scene runs it copies whichever pool is current, so a value
    /// set before Start is visible, then later writes stay in that scene.
    /// ResetVars() clears every pool. Scene::Clear and the scene destructor drop
    /// that scene's pools.
    static std::unordered_map<std::string, float>& Vars();
    // Read a variable by name: the current scene's pool first, then a saved
    // (Prefs) value — so stats/prefs published under a name are readable as variables.
    static float GetVar(const std::string& key);
    // Float arrays (array_push/get/... ops). Same scene / process split as Vars.
    static std::unordered_map<std::string, std::vector<float>>& Arrays();
    // Maps/dictionaries (string key -> float value). Same scene / process split as Vars.
    static std::unordered_map<std::string, std::unordered_map<std::string, float>>& Maps();
    // Text variables (str_set/str_eq/... ops). Same scene / process split as Vars.
    // Displayed by a UITextBind {key} the same as numbers, so names/dialogue can go on screen.
    static std::unordered_map<std::string, std::string>& StrVars();
    static void ResetVars();
    /// Make `scene` the pool Vars/Arrays/Maps/StrVars read and write. Null uses the
    /// process-wide pools. Scene::Start/Update/Render call this.
    static void UseScene(const Scene* scene);
    /// Drop `scene`'s pools (also the cooldown memory for its conditions).
    static void ForgetScene(const Scene* scene);

    // ---- Step debugging (driven from the editor's Flow Graph) --------------
    // When paused, running lists execute only StepBudget() instructions, then hold
    // at the next one. The editor grants budget with each "Step" and shows the live
    // node. Off by default, so shipped games are unaffected.
    static bool& DebugPaused();
    static int&  StepBudget();
    /// Debug breakpoints as (address of an instruction vector, instruction index).
    /// Set by the editor's Flow Graph; runtime-only (never serialized). When the
    /// interpreter is about to run a marked instruction it flips DebugPaused().
    static std::vector<std::pair<const void*, int>>& Breakpoints();

    // ---- Visual-script bridge ------------------------------------------------
    // The node graph's Action / Condition / Action Block nodes run the same step
    // library through a private ActionList that is never added to a scene.
    /// Start running `instructions` now, whatever the trigger (false if already running).
    bool BeginRun() { m_fired = false; return FireHandler(conditions, instructions, m_fired, false); }
    /// True when every condition in `conds` passes for this list's object.
    bool TestConditions(const std::vector<Item>& conds) { return EvalConditions(conds); }
    /// One step as a single token-safe line: "op arg arg" with each arg %-escaped
    /// (no spaces, quotes or '#'), so it can sit in an .okayvs node line.
    static std::string EncodeItem(const Item& it);
    static Item DecodeItem(const std::string& line);
    /// Several steps joined by " ; ".
    static std::string EncodeItems(const std::vector<Item>& items);
    static std::vector<Item> DecodeItems(const std::string& text);

private:
    bool m_startDeferred = false;   // Start() arrived while scripts were off
    void Fire();   // fire handler 0 (used by Start for On Start)
    // Start running one handler's instructions if idle and its gate passes.
    bool FireHandler(std::vector<Item>& conds, std::vector<Item>& ins, bool& fired, bool once);
    void Latch(Trigger t) { m_pending = true; m_pendingType = t; }
    bool EvalConditions(const std::vector<Item>& conds);
    // The instruction list currently running (or handler 0's when idle).
    const std::vector<Item>& RunList() const { return m_run ? *m_run : instructions; }
    // Resolve a goto/if_goto target: a numeric line, or the index of a `label` op.
    int  ResolveTarget(const std::string& t) const;
    // Index of the matching `endOp` for a block opener at `openIp` (handles nesting).
    int  MatchingEnd(std::size_t openIp, const char* openOp, const char* endOp) const;

    bool m_running = false;
    std::size_t m_ip = 0;
    float m_wait = 0.0f;
    bool m_fired = false;
    float m_timer = 0.0f;   // primary handler's OnInterval accumulator
    std::vector<Item>* m_run = nullptr;   // active instruction list while running
    bool* m_runFired = nullptr;           // fired-flag of the running handler (set on completion)
    bool m_pending = false;    // latched by event callbacks (collision/trigger/mouse)
    Trigger m_pendingType = Trigger::OnCollision;   // which event latched
    // Loop frames (repeat / while / for-each) and subroutine return addresses.
    struct LoopFrame {
        int kind = 0;              // 0 = repeat, 1 = while, 2 = for-each array, 3 = for-each tagged
        std::size_t headIp = 0;    // the loop's opening instruction
        std::size_t bodyStart = 0; // first instruction of the body
        std::size_t endIp = 0;     // the matching end_* instruction
        int remaining = 0;         // repeat: iterations left
        std::string arr, idxVar, valVar;  // for-each state
        std::size_t idx = 0;
        std::vector<std::string> names;   // for-each-tagged: the object names to visit
    };
    std::vector<LoopFrame>   m_loops;
    std::vector<std::size_t> m_callStack;
};

} // namespace okay
