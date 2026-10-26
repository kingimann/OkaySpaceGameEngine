#pragma once
// ---------------------------------------------------------------------------
// AnimStateMachine — an animation state machine for imported models.
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
// speed), sampled in phase — or a 2D blend tree over two parameters (strafing
// by moveX / moveZ). onStateEnter/onStateExit report state changes.
//
// Layers: a second machine on the same object with `layer` > 0 plays on top of
// the base pose, only on `maskBone` and its children (an upper-body attack while
// the legs keep walking), mixed in by `layerWeight`. Layers share the base
// machine's parameters.
//
// Per state: an event fired on enter / exit (to anim_event() and every
// component's OnAnimationEvent, like clip events) and an IK weight that Foot IK
// and Full Body IK ease to over the transition (0 = feet free while jumping).
// A transition can start its target part-way in (`offset`, normalized).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
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
        IntEquals,       ///< (int) param == value
        IntNotEquals,    ///< (int) param != value
    };
    static constexpr int kCondCount = 9;

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
        float       offset = 0.0f;      ///< normalized time the target state starts at (0..1)
        bool        muted = false;      ///< never fires (try the machine without it, keep it for later)
        /// While this transition is blending, which state's transitions may cut it
        /// short. NextState = the target's own
        /// transitions (the engine's long-standing behaviour); None = nothing but
        /// Any State until the blend ends; CurrentState = the state being left.
        enum class Interrupt { NextState = 0, None, CurrentState, CurrentThenNext, NextThenCurrent };
        Interrupt   interruption = Interrupt::NextState;
    };

    /// A blend-tree motion: a clip and where it's fully on — `threshold` on the
    /// blend parameter (1D), or the point (threshold, posY) in 2D.
    struct Motion { std::string clip; float threshold = 0.0f; float posY = 0.0f; };

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
        int         blendDims = 1;  ///< 1 = 1D tree on blendParam; 2 = 2D on (blendParam, blendParamY)
        std::string blendParamY;
        std::string speedParam;     ///< float parameter that multiplies `speed` ("" = none)
        std::string enterEvent, exitEvent;   ///< animation events fired on entering / leaving ("" = none)
        float       ikWeight = 1.0f;         ///< Foot IK / Full Body IK weight in this state (base layer)
    };

    // ---- Layer settings (layer 0 = the base machine) ----
    int         layer = 0;
    float       layerWeight = 1.0f;   ///< 0..1 how much this layer overrides the base pose
    std::string maskBone;             ///< bone whose subtree this layer drives ("" = whole model)

    std::vector<State> states;
    std::string entry;              ///< starting state ("" = first state)
    /// "Any State" transitions: tested every frame from whatever state is playing,
    /// before that state's own transitions (hit reactions, death, ...).
    std::vector<Transition> anyState;

    /// Called with the state's name when the machine enters / leaves a state.
    std::function<void(const std::string&)> onStateEnter, onStateExit;

    /// Declared parameters: purely for the
    /// editor — transitions pick from this list instead of retyping names, and
    /// Play mode shows their live values. The runtime maps stay name-keyed, so
    /// undeclared names still work from scripts. Serialized.
    struct Param { std::string name; int type = 0; };   ///< type: 0 float, 1 bool, 2 trigger, 3 int
    std::vector<Param> params;

    // ---- Parameters (runtime; set from gameplay). Layers share the base's. ----
    void SetFloat(const std::string& name, float v)  { Base()->m_floats[name] = v; }
    void SetInt(const std::string& name, int v)      { Base()->m_floats[name] = (float)v; }
    void SetBool(const std::string& name, bool b)    { Base()->m_bools[name] = b; }
    void SetTrigger(const std::string& name)         { Base()->m_triggers[name] = true; }
    float GetFloat(const std::string& name) const {
        const auto& m = Base()->m_floats;
        auto it = m.find(name); return it == m.end() ? 0.0f : it->second;
    }
    int GetInt(const std::string& name) const { return (int)std::lround(GetFloat(name)); }
    bool GetBool(const std::string& name) const {
        const auto& m = Base()->m_bools;
        auto it = m.find(name); return it != m.end() && it->second;
    }

    /// 2D blend weights (freeform cartesian, gradient band): every motion gets
    /// a weight from where `p` sits relative to all the others; they sum to 1.
    static void Weights2D(const std::vector<Motion>& ms, float px, float py, std::vector<float>& w) {
        const int n = (int)ms.size();
        w.assign(n, 0.0f);
        if (n == 0) return;
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) {
            float wi = 1.0f;
            for (int j = 0; j < n && wi > 0.0f; ++j) {
                if (i == j) continue;
                float vx = ms[j].threshold - ms[i].threshold, vy = ms[j].posY - ms[i].posY;
                float l2 = vx * vx + vy * vy;
                if (l2 < 1e-8f) continue;
                float h = 1.0f - ((px - ms[i].threshold) * vx + (py - ms[i].posY) * vy) / l2;
                wi = std::fmin(wi, h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h));
            }
            w[i] = wi; sum += wi;
        }
        if (sum > 1e-6f) { for (float& x : w) x /= sum; return; }
        int best = 0; float bd = 1e30f;   // outside every band: the nearest motion
        for (int i = 0; i < n; ++i) {
            float dx = px - ms[i].threshold, dy = py - ms[i].posY, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = i; }
        }
        w[best] = 1.0f;
    }
    /// The weights the current state's blend tree applied last frame (one per motion).
    const std::vector<float>& BlendWeights() const { return m_bWeights; }

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
    bool GoTo(const std::string& name, float blend = -1.0f, float offset = 0.0f) {
        const State* st = FindState(name);
        if (!st) return false;
        Enter(*st, blend >= 0.0f ? blend : 0.25f, offset);
        return true;
    }
    /// The IK weight currently applied (eases to the state's ikWeight).
    float IKWeight() const { return m_ikW; }

    /// The last state changes, oldest first (Play-mode debugging: "why did it
    /// go to Fall?"). `time` is seconds since the machine started.
    struct HistoryEntry { std::string from, to; float time = 0.0f; };
    const std::vector<HistoryEntry>& History() const { return m_history; }
    /// Seconds since the machine started (the clock History() times are on).
    float Clock() const { return m_clock; }
    static constexpr int kHistoryMax = 12;
    /// True while a transition's blend is running; `left` = seconds until it ends.
    bool InTransition(float* left = nullptr) const {
        if (left) *left = m_trLeft;
        return m_trActive;
    }
    /// The state the running transition is leaving ("" when not in a transition).
    const std::string& TransitionSource() const { return m_trFrom; }
    /// How many transitions were cut short by another one (Play-mode debugging).
    int Interruptions() const { return m_interruptions; }

    void Start() override { m_current.clear(); }
    void OnDestroy() override { if (ModelAnimator* ma = Anim()) ma->RemovePostPose(this); }

    void Update(float dt) override {
        ModelAnimator* ma = Anim();
        if (!ma || states.empty()) return;
        m_clock += dt;
        if (layer > 0) { LayerUpdate(dt, ma); return; }
        // Enter the entry state once the ModelAnimator is alive.
        if (m_current.empty()) {
            const State* st = FindState(entry);
            if (!st) st = &states.front();
            Enter(*st, 0.0f);
            return;
        }
        const State* cur = FindState(m_current);
        if (!cur) { m_current.clear(); return; }

        // Per-state IK weight, eased over the transition's blend time.
        {
            float want = std::fmin(std::fmax(cur->ikWeight, 0.0f), 1.0f);
            if (m_ikW != want) {
                float step = m_ikBlend > 1e-4f ? dt / m_ikBlend : 1.0f;
                m_ikW = m_ikW < want ? std::fmin(want, m_ikW + step) : std::fmax(want, m_ikW - step);
            }
            if (gameObject) {
                if (auto* f = gameObject->GetComponent<FootIK>()) f->stateWeight = m_ikW;
                if (auto* b = gameObject->GetComponent<FullBodyIK>()) b->stateWeight = m_ikW;
            }
        }

        // Advance the state clock (and drive a blend tree).
        m_stateTime += dt;
        const float spd = StateSpeed(*cur);
        if (cur->blendTree && cur->blendDims == 2) {
            Weights2D(cur->motions, GetFloat(cur->blendParam), GetFloat(cur->blendParamY), m_bWeights);
            std::vector<int> idx;
            for (const Motion& m : cur->motions) idx.push_back(ClipIndex(ma, m.clip));
            const float prevPhase = ma->BlendPhase();
            ma->speed = spd;
            ma->externalDrive = ma->BlendMany(dt, idx, m_bWeights);
            float ph = ma->BlendPhase();
            m_normTime += (ph >= prevPhase) ? ph - prevPhase : ph + 1.0f - prevPhase;
        } else if (cur->blendTree) {
            int lo, hi; float w;
            PickMotions(*cur, GetFloat(cur->blendParam), lo, hi, w);
            m_bLo = lo; m_bHi = hi; m_bW = w;
            m_bWeights.assign(cur->motions.size(), 0.0f);
            if (lo >= 0) m_bWeights[lo] += 1.0f - w;
            if (hi >= 0) m_bWeights[hi] += w;
            int A = lo >= 0 ? ClipIndex(ma, cur->motions[lo].clip) : -1;
            int B = hi >= 0 ? ClipIndex(ma, cur->motions[hi].clip) : -1;
            const float prevPhase = ma->BlendPhase();
            ma->speed = spd;
            ma->externalDrive = ma->BlendClips(dt, A, B, w);
            float ph = ma->BlendPhase();
            m_normTime += (ph >= prevPhase) ? ph - prevPhase : ph + 1.0f - prevPhase;
        } else {
            ma->speed = spd;
            float len = ma->active >= 0 ? ma->ClipLength(ma->active) : 0.0f;
            m_normTime += len > 1e-5f ? dt * std::fabs(spd) / len : 0.0f;
        }

        EvaluateTransitions(dt, ma, cur);
    }

    /// The machine that owns the parameters: the base layer on this object.
    AnimStateMachine* Base() const {
        if (layer == 0 || !gameObject) return const_cast<AnimStateMachine*>(this);
        for (const auto& c : gameObject->Components())
            if (auto* sm = dynamic_cast<AnimStateMachine*>(c.get()))
                if (sm->layer == 0) return sm;
        return const_cast<AnimStateMachine*>(this);
    }

private:
    ModelAnimator* Anim() const {
        return gameObject ? gameObject->GetComponent<ModelAnimator>() : nullptr;
    }
    float StateSpeed(const State& st) const {
        return st.speed * (st.speedParam.empty() ? 1.0f : GetFloat(st.speedParam));
    }

    // ---- Layers: their own playback, posed after the base (see LayerPose) ----
    // The state is held by NAME and looked up each use: `states` may grow (and
    // reallocate) while a layer plays, which would leave a State* dangling.
    struct LayerPlay { std::string st; float time = 0.0f, phase = 0.0f; };
    /// Advance a layer motion and list its (clip, time) samples + weights.
    void LayerMotion(LayerPlay& lp, float dt, ModelAnimator* ma,
                     std::vector<std::pair<int, float>>& ct, std::vector<float>& w, bool advance) {
        const State* sp = FindState(lp.st);   // "" (no state) finds nothing
        if (!sp) return;
        const State& st = *sp;
        const float spd = StateSpeed(st);
        if (!st.blendTree) {
            int c = ClipIndex(ma, st.clip);
            if (c < 0) return;
            float len = ma->ClipLength(c);
            if (advance) lp.time += dt * spd;
            float t = lp.time;
            if (len > 0.0f) t = st.loop ? std::fmod(std::fmod(t, len) + len, len) : std::fmin(std::fmax(t, 0.0f), len);
            ct.push_back({c, t}); w.push_back(1.0f);
            return;
        }
        std::vector<float> bw;
        if (st.blendDims == 2) Weights2D(st.motions, GetFloat(st.blendParam), GetFloat(st.blendParamY), bw);
        else {
            int lo, hi; float x;
            PickMotions(st, GetFloat(st.blendParam), lo, hi, x);
            bw.assign(st.motions.size(), 0.0f);
            if (lo >= 0) bw[lo] += 1.0f - x;
            if (hi >= 0) bw[hi] += x;
        }
        float cycle = 0.0f, sum = 0.0f;
        for (std::size_t i = 0; i < st.motions.size(); ++i) {
            int c = ClipIndex(ma, st.motions[i].clip);
            if (c < 0 || bw[i] <= 0.0f) continue;
            cycle += ma->ClipLength(c) * bw[i]; sum += bw[i];
        }
        if (sum <= 0.0f) return;
        cycle /= sum;
        if (advance) lp.phase = std::fmod(lp.phase + dt * spd / std::fmax(cycle, 1e-4f), 1.0f);
        for (std::size_t i = 0; i < st.motions.size(); ++i) {
            int c = ClipIndex(ma, st.motions[i].clip);
            if (c < 0 || bw[i] <= 0.0f) continue;
            ct.push_back({c, lp.phase * ma->ClipLength(c)}); w.push_back(bw[i] / sum);
        }
    }
    void LayerUpdate(float dt, ModelAnimator* ma) {
        if (!m_hooked) { ma->AddPostPose(this, [this](float) { LayerPose(); }); m_hooked = true; }
        if (m_current.empty()) {
            const State* st = FindState(entry);
            if (!st) st = &states.front();
            Enter(*st, 0.0f);
            return;
        }
        const State* cur = FindState(m_current);
        if (!cur) { m_current.clear(); return; }
        m_stateTime += dt;
        m_lCT.clear(); m_lW.clear();
        std::vector<std::pair<int, float>> ctCur, ctPrev; std::vector<float> wCur, wPrev;
        float prevPhase = m_lCur.phase, prevTime = m_lCur.time;
        LayerMotion(m_lCur, dt, ma, ctCur, wCur, true);
        if (cur->blendTree) m_normTime += (m_lCur.phase >= prevPhase) ? m_lCur.phase - prevPhase : m_lCur.phase + 1.0f - prevPhase;
        else if (!ctCur.empty()) {
            float len = ma->ClipLength(ctCur[0].first);
            m_normTime = len > 1e-5f ? m_lCur.time / len : 0.0f;
        }
        (void)prevTime;
        float f = 1.0f;
        if (!m_lPrev.st.empty() && m_fadeDur > 0.0f) {
            m_fadeT += dt;
            f = std::fmin(1.0f, m_fadeT / m_fadeDur);
            f = f * f * (3.0f - 2.0f * f);
            LayerMotion(m_lPrev, dt, ma, ctPrev, wPrev, true);
            if (m_fadeT >= m_fadeDur) m_lPrev.st.clear();
        }
        for (std::size_t i = 0; i < ctPrev.size(); ++i) { m_lCT.push_back(ctPrev[i]); m_lW.push_back(wPrev[i] * (1.0f - f)); }
        for (std::size_t i = 0; i < ctCur.size(); ++i)  { m_lCT.push_back(ctCur[i]);  m_lW.push_back(wCur[i] * f); }
        EvaluateTransitions(dt, ma, cur);
    }
    /// Any State first, then the current state's own transitions (first match
    /// wins). While a transition is still blending, its interruption setting
    /// decides which of the target's / source's transitions may cut it short.
    void EvaluateTransitions(float dt, ModelAnimator* ma, const State* cur) {
        if (m_trActive) {
            m_trLeft -= dt;
            if (const State* src = FindState(m_trFrom)) {   // the source keeps its own clock
                int c = src->blendTree ? -1 : ClipIndex(ma, src->clip);
                float len = c >= 0 ? ma->ClipLength(c) : 1.0f;
                m_trSrcNorm += len > 1e-5f ? dt * std::fabs(StateSpeed(*src)) / len : 0.0f;
            }
            if (m_trLeft <= 0.0f) m_trActive = false;
        }
        for (const Transition& tr : anyState) {
            if (tr.to == m_current && !tr.canInterruptSelf) continue;
            if (Try(tr, ma, cur)) return;
        }
        if (!m_trActive) {
            for (const Transition& tr : cur->transitions)
                if (Try(tr, ma, cur)) return;
            return;
        }
        using I = Transition::Interrupt;
        const I rule = m_trRule;
        const State* src = FindState(m_trFrom);
        auto tryNext = [&]() {
            for (const Transition& tr : cur->transitions) if (Try(tr, ma, cur)) return true;
            return false;
        };
        auto trySource = [&]() {
            if (!src) return false;
            // Exit times on the source's transitions use the source's own clock.
            const float keep = m_normTime; m_normTime = m_trSrcNorm;
            bool fired = false;
            for (const Transition& tr : src->transitions) {
                if (tr.to == m_current) continue;   // that's the blend already running
                if (Satisfied(tr, ma, src)) { m_normTime = keep; fired = Try(tr, ma, src, true); break; }
            }
            if (!fired) m_normTime = keep;
            return fired;
        };
        switch (rule) {
            case I::None: break;
            case I::NextState:       tryNext(); break;
            case I::CurrentState:    trySource(); break;
            case I::CurrentThenNext: if (!trySource()) tryNext(); break;
            case I::NextThenCurrent: if (!tryNext()) trySource(); break;
        }
    }
    /// Pose the layer over the base on the masked bones (runs after the base pose).
    void LayerPose() {
        ModelAnimator* ma = Anim();
        if (!ma || m_lCT.empty() || layerWeight <= 0.0f) return;
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        const std::uint64_t ver = sc ? sc->StructureVersion() : 0;
        if (ver != m_maskVer || m_maskName != maskBone) {
            m_mask.clear(); m_maskVer = ver; m_maskName = maskBone;
            if (!maskBone.empty())
                if (GameObject* mb = ma->FindNode(maskBone)) {
                    std::vector<Transform*> stack{mb->transform};
                    while (!stack.empty()) {
                        Transform* t = stack.back(); stack.pop_back();
                        m_mask.insert(t);
                        for (Transform* c : t->Children()) stack.push_back(c);
                    }
                }
        }
        if (!maskBone.empty() && m_mask.empty()) return;   // mask bone missing: drive nothing
        ma->PoseWeighted(m_lCT, m_lW, maskBone.empty() ? nullptr : &m_mask, std::fmin(layerWeight, 1.0f));
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
                return ((cur && cur->blendTree) || layer > 0) ? m_normTime >= 1.0f : ma->ClipFinished();
            case Cond::FloatGreater: return GetFloat(param) > value;
            case Cond::FloatLess:    return GetFloat(param) < value;
            case Cond::BoolTrue:     return GetBool(param);
            case Cond::BoolFalse:    return !GetBool(param);
            case Cond::Trigger: {
                const auto& tg = Base()->m_triggers;
                auto it = tg.find(param);
                return it != tg.end() && it->second;
            }
            case Cond::Always: return true;
            case Cond::IntEquals:    return GetInt(param) == (int)std::lround(value);
            case Cond::IntNotEquals: return GetInt(param) != (int)std::lround(value);
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
    bool Try(const Transition& tr, ModelAnimator* ma, const State* cur, bool presatisfied = false) {
        if (tr.muted || (!presatisfied && !Satisfied(tr, ma, cur))) return false;
        const State* to = FindState(tr.to);
        if (!to) return false;
        auto consume = [&](Cond c, const std::string& p) { if (c == Cond::Trigger) Base()->m_triggers[p] = false; };
        consume(tr.cond, tr.param);
        for (const Condition& c : tr.extra) consume(c.cond, c.param);
        if (m_trActive) ++m_interruptions;
        const std::string from = m_current;
        const float fromNorm = m_normTime;
        Enter(*to, tr.blend, tr.offset);
        m_trActive = tr.blend > 0.0f;
        m_trLeft = tr.blend;
        m_trFrom = from;
        m_trSrcNorm = fromNorm;
        m_trRule = tr.interruption;
        return true;
    }
    void Enter(const State& st, float blend, float offset = 0.0f) {
        ModelAnimator* ma = Anim();
        if (!ma) return;
        m_trActive = false;   // Try() re-arms it for a conditional transition
        if (!m_current.empty()) {
            if (onStateExit) onStateExit(m_current);
            if (const State* old = FindState(m_current)) if (!old->exitEvent.empty()) ma->FireEvent(old->exitEvent);
        }
        const bool first = m_current.empty();
        m_previous = m_current;
        m_current = st.name;
        m_history.push_back({m_previous, m_current, m_clock});
        if ((int)m_history.size() > kHistoryMax) m_history.erase(m_history.begin());
        offset = std::fmin(std::fmax(offset, 0.0f), 0.999f);
        m_stateTime = 0.0f; m_normTime = offset;
        m_ikBlend = blend;
        if (first) m_ikW = std::fmin(std::fmax(st.ikWeight, 0.0f), 1.0f);
        m_bLo = m_bHi = -1; m_bW = 0.0f;
        if (layer > 0) {   // layers crossfade from their previous state themselves
            m_lPrev = m_lCur;
            m_lCur = LayerPlay{st.name, 0.0f, offset};
            if (!st.blendTree) { int c = ClipIndex(ma, st.clip); if (c >= 0) m_lCur.time = offset * ma->ClipLength(c); }
            m_fadeT = 0.0f; m_fadeDur = !m_lPrev.st.empty() ? blend : 0.0f;
            if (blend <= 0.0f) m_lPrev.st.clear();
            if (onStateEnter) onStateEnter(st.name);
            if (!st.enterEvent.empty()) ma->FireEvent(st.enterEvent);
            return;
        }
        ma->speed = StateSpeed(st);
        if (st.blendTree && st.blendDims == 2) {
            Weights2D(st.motions, GetFloat(st.blendParam), GetFloat(st.blendParamY), m_bWeights);
            std::vector<int> idx;
            for (std::size_t i = 0; i < st.motions.size(); ++i)
                if (m_bWeights[i] > 0.0f) idx.push_back(ClipIndex(ma, st.motions[i].clip));
            ma->CaptureCrossfadeMany(idx, blend);
            ma->ResetBlendPhase(offset);
            ma->externalDrive = true;
        } else if (st.blendTree) {
            // Crossfade from the current pose into the tree, which BlendClips then poses.
            int lo, hi; float w;
            PickMotions(st, GetFloat(st.blendParam), lo, hi, w);
            ma->CaptureCrossfade(lo >= 0 ? ClipIndex(ma, st.motions[lo].clip) : -1,
                                 hi >= 0 ? ClipIndex(ma, st.motions[hi].clip) : -1, blend);
            ma->ResetBlendPhase(offset);
            ma->externalDrive = true;
        } else {
            ma->externalDrive = false;
            ma->blendTime = blend;
            ma->loop  = st.loop;
            ma->Play(st.clip);
            if (offset > 0.0f && ma->active >= 0) ma->SetTime(offset * ma->ClipLength(ma->active));
        }
        if (onStateEnter) onStateEnter(st.name);
        if (!st.enterEvent.empty()) ma->FireEvent(st.enterEvent);
    }

    std::string m_current, m_previous;
    float m_stateTime = 0.0f, m_normTime = 0.0f;
    int   m_bLo = -1, m_bHi = -1; float m_bW = 0.0f;
    std::unordered_map<std::string, float> m_floats;
    std::unordered_map<std::string, bool>  m_bools;
    std::unordered_map<std::string, bool>  m_triggers;
    std::vector<float> m_bWeights;
    float m_ikW = 1.0f, m_ikBlend = 0.25f;
    float m_clock = 0.0f;
    std::vector<HistoryEntry> m_history;
    // the transition currently blending (interruption rules)
    bool  m_trActive = false;
    float m_trLeft = 0.0f, m_trSrcNorm = 0.0f;
    std::string m_trFrom;
    Transition::Interrupt m_trRule = Transition::Interrupt::NextState;
    int   m_interruptions = 0;
    // layer playback
    LayerPlay m_lCur, m_lPrev;
    float m_fadeT = 0.0f, m_fadeDur = 0.0f;
    bool  m_hooked = false;
    std::vector<std::pair<int, float>> m_lCT;
    std::vector<float> m_lW;
    std::unordered_set<Transform*> m_mask;
    std::uint64_t m_maskVer = 0;
    std::string m_maskName;
};

} // namespace okay
