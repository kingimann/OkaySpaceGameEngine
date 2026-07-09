#pragma once
// ---------------------------------------------------------------------------
// AnimStateMachine — a Unity-style animation state machine for imported models.
// States name a ModelAnimator clip (with speed/loop overrides); transitions move
// between them when their condition passes: a clip finishing, a float parameter
// compared to a value, a bool parameter, or a one-frame trigger. Each transition
// carries its own crossfade time.
//
// Gameplay sets parameters (SetFloat/SetBool/SetTrigger — or the script builtins
// anim_set_float / anim_set_bool / anim_trigger) and the machine drives the
// sibling ModelAnimator. States/transitions serialize; parameters are runtime.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class AnimStateMachine : public Behaviour {
public:
    /// How a transition decides to fire.
    enum class Cond {
        OnClipEnd = 0,   ///< the state's clip finished (non-looping / one-shot style)
        FloatGreater,    ///< param > value
        FloatLess,       ///< param < value
        BoolTrue,        ///< param is true
        BoolFalse,       ///< param is false (or unset)
        Trigger,         ///< param was triggered since the last check (consumed)
    };

    struct Transition {
        std::string to;             ///< target state name
        Cond        cond = Cond::Trigger;
        std::string param;          ///< parameter name (unused for OnClipEnd)
        float       value = 0.0f;   ///< compare value for Float conditions
        float       blend = 0.25f;  ///< crossfade seconds for this switch
    };

    struct State {
        std::string name;           ///< state name (shown in editors, used by transitions)
        std::string clip;           ///< ModelAnimator clip it plays
        float       speed = 1.0f;   ///< playback speed while in this state
        bool        loop  = true;   ///< loop while in this state
        std::vector<Transition> transitions;
        float nx = 0.0f, ny = 0.0f; ///< Animator graph node position (editor; 0,0 = auto-layout)
    };

    std::vector<State> states;
    std::string entry;              ///< starting state ("" = first state)

    // ---- Parameters (runtime; set from gameplay) ----
    void SetFloat(const std::string& name, float v)  { m_floats[name] = v; }
    void SetBool(const std::string& name, bool b)    { m_bools[name] = b; }
    void SetTrigger(const std::string& name)         { m_triggers[name] = true; }
    float GetFloat(const std::string& name) const {
        auto it = m_floats.find(name); return it == m_floats.end() ? 0.0f : it->second;
    }
    bool GetBool(const std::string& name) const {
        auto it = m_bools.find(name); return it != m_bools.end() && it->second;
    }

    /// Name of the state currently playing ("" before the machine starts).
    const std::string& Current() const { return m_current; }

    /// Force a state by name (fires no conditions; uses the given blend or the
    /// target's default 0.25s). Returns false if there's no such state.
    bool GoTo(const std::string& name, float blend = -1.0f) {
        const State* st = FindState(name);
        if (!st) return false;
        Enter(*st, blend >= 0.0f ? blend : 0.25f);
        return true;
    }

    void Start() override { m_current.clear(); }

    void Update(float) override {
        ModelAnimator* ma = Anim();
        if (!ma || states.empty()) return;
        // Enter the entry state once the ModelAnimator is alive.
        if (m_current.empty()) {
            const State* st = FindState(entry);
            if (!st) st = &states.front();
            Enter(*st, 0.0f);
            return;
        }
        const State* cur = FindState(m_current);
        if (!cur) { m_current.clear(); return; }
        for (const Transition& tr : cur->transitions) {
            if (!Satisfied(tr, ma)) continue;
            if (const State* to = FindState(tr.to)) {
                if (tr.cond == Cond::Trigger) m_triggers[tr.param] = false;   // consume
                Enter(*to, tr.blend);
            }
            break;   // one transition per frame, first match wins
        }
    }

private:
    ModelAnimator* Anim() const {
        return gameObject ? gameObject->GetComponent<ModelAnimator>() : nullptr;
    }
    const State* FindState(const std::string& name) const {
        if (name.empty()) return nullptr;
        for (const State& s : states) if (s.name == name) return &s;
        return nullptr;
    }
    bool Satisfied(const Transition& tr, ModelAnimator* ma) const {
        switch (tr.cond) {
            case Cond::OnClipEnd:    return ma->ClipFinished();
            case Cond::FloatGreater: return GetFloat(tr.param) > tr.value;
            case Cond::FloatLess:    return GetFloat(tr.param) < tr.value;
            case Cond::BoolTrue:     return GetBool(tr.param);
            case Cond::BoolFalse:    return !GetBool(tr.param);
            case Cond::Trigger: {
                auto it = m_triggers.find(tr.param);
                return it != m_triggers.end() && it->second;
            }
        }
        return false;
    }
    void Enter(const State& st, float blend) {
        ModelAnimator* ma = Anim();
        if (!ma) return;
        m_current = st.name;
        ma->blendTime = blend;
        ma->speed = st.speed;
        ma->loop  = st.loop;
        ma->Play(st.clip);
    }

    std::string m_current;
    std::unordered_map<std::string, float> m_floats;
    std::unordered_map<std::string, bool>  m_bools;
    std::unordered_map<std::string, bool>  m_triggers;
};

} // namespace okay
