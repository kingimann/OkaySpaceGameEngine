#pragma once
#include "okay/Scene/Component.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class Collider2D;
struct Collision2D;

/// A Game-Creator-style visual script: a **Trigger** fires the list, optional
/// **Conditions** gate it, then **Instructions** run top-to-bottom (with Wait
/// pausing between steps). Fully data-driven — no code — so it can be authored
/// from dropdowns in the editor.
class ActionList : public Behaviour {
public:
    enum class Trigger { OnStart, OnUpdate, OnKey, OnCollision, OnClick };

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
    void OnTriggerEnter2D(Collider2D* other) override;
    void OnCollisionEnter2D(const Collision2D& c) override;

    bool IsRunning() const { return m_running; }

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
    bool m_collided = false;   // latched by collision callbacks for OnCollision
};

} // namespace okay
