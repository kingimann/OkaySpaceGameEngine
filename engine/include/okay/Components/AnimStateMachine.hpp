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
//
// Beyond single conditions a transition can AND extra conditions, wait for an
// exit time (normalized clip time), and come from "Any State" (checked before
// the current state's own transitions). A state can be a 1D blend tree: a float
// parameter picks a weighted mix of two neighbouring motions (idle/walk/run by
// speed), sampled in phase. onStateEnter/onStateExit report state changes.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
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
        Always,          ///< no condition (use with an exit time: "when the clip is X% done")
    };
    static constexpr int kCondCount = 7;

    /// One test on a parameter. A transition's primary condition plus its
    /// `extra` list must ALL pass.
    struct Condition {
        Cond        cond = Cond::BoolTrue;
        std::string param;
        float       value = 0.0f;
    };

    struct Transition {
        std::string to;             ///< target state name
        Cond        cond = Cond::Trigger;
        std::string param;          ///< parameter name (unused for OnClipEnd / Always)
        float       value = 0.0f;   ///< compare value for Float conditions
        float       blend = 0.25f;  ///< crossfade seconds for this switch
        std::vector<Condition> extra;   ///< more conditions, ANDed with the one above
        bool        hasExitTime = false;
        float       exitTime = 1.0f;    ///< normalized state time it may fire from (0.75 = 75% through)
        bool        canInterruptSelf = false;   ///< Any State only: may re-enter the current state
    };

    /// A 1D blend-tree motion: a clip and the parameter value where it's fully on.
    struct Motion { std::string clip; float threshold = 0.0f; };

    struct State {
        std::string name;           ///< state name (shown in editors, used by transitions)
        std::string clip;           ///< ModelAnimator clip it plays
        float       speed = 1.0f;   ///< playback speed while in this state
        bool        loop  = true;   ///< loop while in this state
        std::vector<Transition> transitions;
        float nx = 0.0f, ny = 0.0f; ///< Animator graph node position (editor; 0,0 = auto-layout)
        // 1D blend tree (when `blendTree`): `blendParam` picks between `motions`
        // (sorted by threshold) instead of playing `clip`.
        bool        blendTree = false;
        std::string blendParam;
        std::vector<Motion> motions;
    };

    std::vector<State> states;
    std::string entry;              ///< starting state ("" = first state)
    /// "Any State" transitions: tested every frame from whatever state is playing,
    /// before that state's own transitions (hit reactions, death, ...).
    std::vector<Transition> anyState;

    /// Called with the state's name when the machine enters / leaves a state.
    std::function<void(const std::string&)> onStateEnter, onStateExit;

    /// Declared parameters (Unity's Animator parameter list): purely for the
    /// editor — transitions pick from this list instead of retyping names, and
    /// Play mode shows their live values. The runtime maps stay name-keyed, so
    /// undeclared names still work from scripts. Serialized.
    struct Param { std::string name; int type = 0; };   ///< type: 0 float, 1 bool, 2 trigger
    std::vector<Param> params;

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
    /// The state played before the current one.
    const std::string& Previous() const { return m_previous; }
    /// Seconds spent in the current state.
    float StateTime() const { return m_stateTime; }
    /// Normalized time in the current state: 0..1 over one clip length (keeps
    /// counting past 1 for looping states: 2.5 = halfway through the 3rd loop).
    /// Blend-tree states report their shared phase.
    float NormalizedTime() const { return m_normTime; }
    /// The blend weights currently applied by a blend-tree state: (lower motion,
    /// upper motion, weight of the upper one). -1 indices when not blending.
    void BlendInfo(int& lo, int& hi, float& w) const { lo = m_bLo; hi = m_bHi; w = m_bW; }

    /// Which two motions (indices into st.motions, sorted by threshold) a blend
    /// parameter value falls between, and the weight of the upper one.
    static void PickMotions(const State& st, float v, int& lo, int& hi, float& w) {
        lo = hi = -1; w = 0.0f;
        const int n = (int)st.motions.size();
        if (n == 0) return;
        std::vector<int> order(n);
        for (int i = 0; i < n; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return st.motions[a].threshold < st.motions[b].threshold; });
        if (v <= st.motions[order.front()].threshold) { lo = hi = order.front(); return; }
        if (v >= st.motions[order.back()].threshold)  { lo = hi = order.back(); return; }
        for (int k = 0; k + 1 < n; ++k) {
            const float a = st.motions[order[k]].threshold, b = st.motions[order[k + 1]].threshold;
            if (v >= a && v <= b) {
                lo = order[k]; hi = order[k + 1];
                w = b - a > 1e-6f ? (v - a) / (b - a) : 0.0f;
                return;
            }
        }
    }

    /// Force a state by name (fires no conditions; uses the given blend or the
    /// target's default 0.25s). Returns false if there's no such state.
    bool GoTo(const std::string& name, float blend = -1.0f) {
        const State* st = FindState(name);
        if (!st) return false;
        Enter(*st, blend >= 0.0f ? blend : 0.25f);
        return true;
    }

    void Start() override { m_current.clear(); }

    void Update(float dt) override {
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

        // Advance the state clock (and drive a blend tree).
        m_stateTime += dt;
        if (cur->blendTree) {
            int lo, hi; float w;
            PickMotions(*cur, GetFloat(cur->blendParam), lo, hi, w);
            m_bLo = lo; m_bHi = hi; m_bW = w;
            int A = lo >= 0 ? ClipIndex(ma, cur->motions[lo].clip) : -1;
            int B = hi >= 0 ? ClipIndex(ma, cur->motions[hi].clip) : -1;
            const float prevPhase = ma->BlendPhase();
            ma->speed = cur->speed;
            ma->externalDrive = ma->BlendClips(dt, A, B, w);
            float ph = ma->BlendPhase();
            m_normTime += (ph >= prevPhase) ? ph - prevPhase : ph + 1.0f - prevPhase;
        } else {
            float len = ma->active >= 0 ? ma->ClipLength(ma->active) : 0.0f;
            m_normTime = len > 1e-5f ? m_stateTime * std::fabs(cur->speed) / len : 0.0f;
        }

        // Any State first, then the state's own transitions; first match wins.
        for (const Transition& tr : anyState) {
            if (tr.to == m_current && !tr.canInterruptSelf) continue;
            if (Try(tr, ma, cur)) return;
        }
        for (const Transition& tr : cur->transitions)
            if (Try(tr, ma, cur)) return;
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
    static int ClipIndex(ModelAnimator* ma, const std::string& name) {
        if (name.empty()) return -1;
        for (int i = 0; i < (int)ma->clips.size(); ++i) if (ma->clips[i].name == name) return i;
        return -1;
    }
    bool Test(Cond c, const std::string& param, float value, ModelAnimator* ma, const State* cur) const {
        switch (c) {
            case Cond::OnClipEnd:
                // A blend tree "ends" after one full cycle; a clip when it finishes.
                return (cur && cur->blendTree) ? m_normTime >= 1.0f : ma->ClipFinished();
            case Cond::FloatGreater: return GetFloat(param) > value;
            case Cond::FloatLess:    return GetFloat(param) < value;
            case Cond::BoolTrue:     return GetBool(param);
            case Cond::BoolFalse:    return !GetBool(param);
            case Cond::Trigger: {
                auto it = m_triggers.find(param);
                return it != m_triggers.end() && it->second;
            }
            case Cond::Always: return true;
        }
        return false;
    }
    bool Satisfied(const Transition& tr, ModelAnimator* ma, const State* cur) const {
        if (tr.hasExitTime) {
            // Looping states test the position inside the current loop; one-shots
            // the total. exitTime >= 1 on a loop means "after a whole cycle".
            const bool looping = cur && (cur->blendTree || cur->loop);
            float t = m_normTime;
            if (looping && tr.exitTime < 1.0f) t = t - std::floor(t);
            if (t < tr.exitTime) return false;
        }
        if (!Test(tr.cond, tr.param, tr.value, ma, cur)) return false;
        for (const Condition& c : tr.extra) if (!Test(c.cond, c.param, c.value, ma, cur)) return false;
        return true;
    }
    bool Try(const Transition& tr, ModelAnimator* ma, const State* cur) {
        if (!Satisfied(tr, ma, cur)) return false;
        const State* to = FindState(tr.to);
        if (!to) return false;
        auto consume = [&](Cond c, const std::string& p) { if (c == Cond::Trigger) m_triggers[p] = false; };
        consume(tr.cond, tr.param);
        for (const Condition& c : tr.extra) consume(c.cond, c.param);
        Enter(*to, tr.blend);
        return true;
    }
    void Enter(const State& st, float blend) {
        ModelAnimator* ma = Anim();
        if (!ma) return;
        if (!m_current.empty() && onStateExit) onStateExit(m_current);
        m_previous = m_current;
        m_current = st.name;
        m_stateTime = 0.0f; m_normTime = 0.0f;
        m_bLo = m_bHi = -1; m_bW = 0.0f;
        ma->speed = st.speed;
        if (st.blendTree) {
            // Crossfade from the current pose into the tree, which BlendClips then poses.
            int lo, hi; float w;
            PickMotions(st, GetFloat(st.blendParam), lo, hi, w);
            ma->CaptureCrossfade(lo >= 0 ? ClipIndex(ma, st.motions[lo].clip) : -1,
                                 hi >= 0 ? ClipIndex(ma, st.motions[hi].clip) : -1, blend);
            ma->ResetBlendPhase();
            ma->externalDrive = true;
        } else {
            ma->externalDrive = false;
            ma->blendTime = blend;
            ma->loop  = st.loop;
            ma->Play(st.clip);
        }
        if (onStateEnter) onStateEnter(st.name);
    }

    std::string m_current, m_previous;
    float m_stateTime = 0.0f, m_normTime = 0.0f;
    int   m_bLo = -1, m_bHi = -1; float m_bW = 0.0f;
    std::unordered_map<std::string, float> m_floats;
    std::unordered_map<std::string, bool>  m_bools;
    std::unordered_map<std::string, bool>  m_triggers;
};

} // namespace okay
