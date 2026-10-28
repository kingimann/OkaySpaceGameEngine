#pragma once
// ---------------------------------------------------------------------------
// AnimStateMachine — the Animator: an animation state machine.
// It plays through a backend (AnimBackend): the object's ModelAnimator on an
// imported model, or — on a built-in Character body — a CharacterAnimBackend it
// owns, which plays the Character's clips.
// States name a clip (with speed/loop overrides); transitions move
// between them when their condition passes: a clip finishing, a float parameter
// compared to a value, a bool parameter, or a one-frame trigger. Each transition
// carries its own crossfade time.
//
// Gameplay sets parameters (SetFloat/SetBool/SetTrigger — or the script builtins
// anim_set_float / anim_set_bool / anim_trigger) and the machine drives the
// backend. States/transitions serialize; parameters are runtime.
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
#include "okay/Components/CharacterAnimBackend.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/Stance.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
        std::string clip;           ///< clip it plays
        /// Played when `clip` is not in this backend's library: a same-named
        /// imported take, or a clip imported into the slot. Empty = no stand-in.
        /// A name that exists only on a child model's clip library is played
        /// there (the retarget yields) instead of keeping the previous clip.
        std::string fallbackClip;
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
        /// Full Body IK weight on its own, below 0 = follow ikWeight. Lets a
        /// state turn foot planting off while the hands keep working (prone,
        /// crawling: the body lies down but still holds what it holds).
        float       fullBodyIkWeight = -1.0f;
        /// Blend trees: the motions' positions are how fast each one travels, and
        /// the tree plays as fast as the body moves (speedParam's value, else the
        /// blend position's distance from 0) over the blended motions' speed, so
        /// the feet keep pace with the ground between and beyond the motions.
        bool        matchSpeed = false;
        /// False locks the state: Any State (jump, fall, a later action) cannot
        /// cut it short. Death uses this. Its own transitions still run.
        bool        interruptible = true;
    };

    // ---- Layer settings (layer 0 = the base machine) ----
    int         layer = 0;
    float       layerWeight = 1.0f;   ///< 0..1 how strongly this layer mixes in
    std::string maskBone;             ///< bone whose subtree this layer drives ("" = whole model)
    /// More mask roots, each with the bones under it. Together with `maskBone`
    /// this is the avatar mask (upper body plus the head, arms only, ...).
    std::vector<std::string> maskBones;
    /// Add the layer onto the base pose instead of replacing the masked bones.
    /// Character clips are offsets from rest, so this is a recoil or aim layer.
    /// Imported clips add their change from the clip's first frame.
    bool        additive = false;

    std::vector<State> states;
    std::string entry;              ///< starting state ("" = first state)
    /// "Any State" transitions: tested every frame from whatever state is playing,
    /// before that state's own transitions (hit reactions, death, ...).
    std::vector<Transition> anyState;
    /// Runtime: while set, no transition fires (a held gesture keeps its state).
    bool hold = false;
    /// How long this body's stance changes take; the movement controllers ease the
    /// body, its collider and the camera over them. Serialized.
    StanceTimes stanceTimes;
    /// Built-in action revision already merged into this graph (see HumanoidActions).
    /// 0 = a graph from before actions. Serialized as `animsmactions`.
    int actionsRev = 0;
    /// How long idle / walk / run and ground / air crossfades take. Serialized
    /// when it differs from the defaults. Editing it retimes those pairs.
    GaitCrossfades crossfades;

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
    /// Clip name `st` plays on `backend`: `clip` when that name exists there,
    /// otherwise `fallbackClip` when that one does, otherwise `clip`.
    static std::string ClipToPlay(AnimBackend* backend, const State& st) {
        if (backend && (st.clip.empty() || backend->FindClip(st.clip) < 0) &&
            !st.fallbackClip.empty() && backend->FindClip(st.fallbackClip) >= 0)
            return st.fallbackClip;
        return st.clip;
    }
    /// True while a missing character clip is standing in on a child model's
    /// own clip library (Humanoid Retarget yields to it).
    bool PlayingImported() const { return m_importHold != nullptr; }
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

    void Start() override {
        m_current.clear();
        if (layer == 0) if (AnimBackend* ma = Anim()) ma->SetStateMachine(this);   // driveByMovement waits while we're enabled
    }
    void OnDestroy() override {
        ReleaseImported();
        if (AnimBackend* ma = Anim()) ma->RemovePostPose(this);
        if (m_charBackend) m_charBackend->Detach();
    }

    void Update(float dt) override {
        AnimBackend* ma = Anim();
        if (!ma || states.empty()) return;
        if (layer == 0) ma->SetStateMachine(this);
        m_clock += dt;
        if (layer > 0) { LayerUpdate(dt, ma); return; }
        // Enter the entry state once the backend is alive.
        if (m_current.empty()) {
            const State* st = FindState(entry);
            if (!st) st = &states.front();
            Enter(*st, 0.0f);
            ma->BeginFrame(*this);
            ma->Advance(0.0f);
            return;
        }
        ma->BeginFrame(*this);
        const State* cur = FindState(m_current);
        if (!cur) { m_current.clear(); return; }

        // Per-state IK weights (feet, and the full body's own), eased over the
        // transition's blend time.
        {
            const float want = std::fmin(std::fmax(cur->ikWeight, 0.0f), 1.0f);
            const float wantFb = cur->fullBodyIkWeight < 0.0f ? want
                               : std::fmin(std::fmax(cur->fullBodyIkWeight, 0.0f), 1.0f);
            const float step = m_ikBlend > 1e-4f ? dt / m_ikBlend : 1.0f;
            if (m_ikW != want) m_ikW = m_ikW < want ? std::fmin(want, m_ikW + step) : std::fmax(want, m_ikW - step);
            if (m_fbW != wantFb) m_fbW = m_fbW < wantFb ? std::fmin(wantFb, m_fbW + step) : std::fmax(wantFb, m_fbW - step);
            // Push them to the IK components when they change (plus a slow refresh,
            // so IK added later picks it up) instead of two GetComponent scans a frame.
            if (gameObject && (m_ikW != m_ikWritten || m_fbW != m_fbWritten || (m_ikRefresh -= dt) <= 0.0f)) {
                if (auto* f = gameObject->GetComponent<FootIK>()) f->stateWeight = m_ikW;
                if (auto* b = gameObject->GetComponent<FullBodyIK>()) b->stateWeight = m_fbW;
                m_ikWritten = m_ikW; m_fbWritten = m_fbW; m_ikRefresh = 0.5f;
            }
        }

        // Advance the state clock (and drive a blend tree).
        m_stateTime += dt;
        const float spd = StateSpeed(*cur);
        if (cur->blendTree && m_importHold) ReleaseImported();
        if (cur->blendTree && cur->blendDims == 2) {
            Weights2D(cur->motions, GetFloat(cur->blendParam), GetFloat(cur->blendParamY), m_bWeights);
            std::vector<int>& idx = m_motionClips;   // reused: no per-frame allocation
            idx.clear();
            for (const Motion& m : cur->motions) idx.push_back(MotionClip(ma, *cur, m.clip));
            const float prevPhase = ma->BlendPhase();
            ma->SetSpeed(spd * MatchRate(*cur, m_bWeights));
            ma->SetExternalDrive(ma->BlendMany(dt, idx, m_bWeights));
            float ph = ma->BlendPhase();
            m_normTime += (ph >= prevPhase) ? ph - prevPhase : ph + 1.0f - prevPhase;
        } else if (cur->blendTree) {
            int lo, hi; float w;
            PickMotions(*cur, GetFloat(cur->blendParam), lo, hi, w);
            m_bLo = lo; m_bHi = hi; m_bW = w;
            m_bWeights.assign(cur->motions.size(), 0.0f);
            if (lo >= 0) m_bWeights[lo] += 1.0f - w;
            if (hi >= 0) m_bWeights[hi] += w;
            int A = lo >= 0 ? MotionClip(ma, *cur, cur->motions[lo].clip) : -1;
            int B = hi >= 0 ? MotionClip(ma, *cur, cur->motions[hi].clip) : -1;
            const float prevPhase = ma->BlendPhase();
            ma->SetSpeed(spd * MatchRate(*cur, m_bWeights));
            ma->SetExternalDrive(ma->BlendClips(dt, A, B, w));
            float ph = ma->BlendPhase();
            m_normTime += (ph >= prevPhase) ? ph - prevPhase : ph + 1.0f - prevPhase;
        } else if (m_missingClip) {
            // The state's clip isn't there. Keep the pose already on screen and
            // don't let that old clip keep running; OnClipEnd is true so the
            // graph can leave this state.
            ma->SetSpeed(0.0f);
            m_normTime = 1.0f;
        } else if (m_importHold && m_importHold->gameObject && !m_importHold->gameObject->IsDestroyed()
                   && m_importHold->ActiveClip() >= 0) {
            m_importHold->speed = spd;
            const float len = m_importHold->ClipLength(m_importHold->ActiveClip());
            m_normTime = len > 1e-5f ? m_importHold->Time() / len : m_normTime;
        } else {
            if (m_importHold) ReleaseImported();
            ma->SetSpeed(spd);
            float len = ma->ActiveClip() >= 0 ? ma->ClipLength(ma->ActiveClip()) : 0.0f;
            m_normTime += len > 1e-5f ? dt * std::fabs(spd) / len : 0.0f;
        }

        EvaluateTransitions(dt, ma, cur);
        ma->Advance(dt);
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
    /// The backend: the ModelAnimator on this object, else (on a Character body)
    /// our own CharacterAnimBackend. Layers play through the base machine's.
    AnimBackend* Anim() const {
        if (layer > 0) { AnimStateMachine* b = Base(); if (b != this) return b->Anim(); }
        if (!gameObject) return nullptr;
        if (AnimBackend* ma = gameObject->GetComponent<ModelAnimator>()) return ma;
        if (!gameObject->GetComponent<Character>()) return nullptr;
        if (!m_charBackend) m_charBackend = std::make_unique<CharacterAnimBackend>(gameObject);
        return m_charBackend.get();
    }
    float StateSpeed(const State& st) const {
        if (st.blendTree && st.matchSpeed) return st.speed;   // MatchRate reads speedParam
        return st.speed * (st.speedParam.empty() ? 1.0f : GetFloat(st.speedParam));
    }
    /// A matchSpeed tree's playback rate for blend weights `w` (1 otherwise).
    float MatchRate(const State& st, const std::vector<float>& w) const {
        if (!st.matchSpeed) return 1.0f;
        const bool two = st.blendDims == 2;
        float want;
        if (!st.speedParam.empty()) want = std::fabs(GetFloat(st.speedParam));
        else {
            const float x = GetFloat(st.blendParam), y = two ? GetFloat(st.blendParamY) : 0.0f;
            want = std::sqrt(x * x + y * y);
        }
        float have = 0.0f;
        for (std::size_t i = 0; i < st.motions.size() && i < w.size(); ++i) {
            const float y = two ? st.motions[i].posY : 0.0f;
            have += w[i] * std::sqrt(st.motions[i].threshold * st.motions[i].threshold + y * y);
        }
        return have > 1e-4f ? want / have : 1.0f;
    }

    /// Clip index for a slot. `name` wins; `fallbackClip` stands in when that
    /// name is missing from this backend (same library only).
    int MotionClip(AnimBackend* ma, const State& st, const std::string& name) const {
        if (!ma) return -1;
        if (!name.empty()) {
            const int c = ma->FindClip(name);
            if (c >= 0) return c;
        }
        if (!st.fallbackClip.empty() && st.fallbackClip != name) return ma->FindClip(st.fallbackClip);
        return -1;
    }
    /// A child (not this object) whose Model Animator has `name`.
    ModelAnimator* ImportedWith(const std::string& name) const {
        if (!gameObject || name.empty()) return nullptr;
        Scene* sc = gameObject->scene();
        if (!sc) return nullptr;
        for (const auto& up : sc->Objects()) {
            GameObject* g = up.get();
            if (!g || g == gameObject || !g->IsSelfOrDescendantOf(gameObject)) continue;
            if (auto* m = g->GetComponent<ModelAnimator>())
                if (m->FindClip(name) >= 0) return m;
        }
        return nullptr;
    }
    void ReleaseImported() {
        if (!m_importHold) return;
        ModelAnimator* m = m_importHold;
        m_importHold = nullptr;
        if (m->gameObject && !m->gameObject->IsDestroyed()) m->ReleaseHold();
    }
    /// Play `clip`, or `fallbackClip`, on a child model's library. The retarget
    /// yields while that hold lasts. False when neither name is imported.
    bool HoldImported(const State& st, float blend) {
        ReleaseImported();
        ModelAnimator* m = ImportedWith(st.clip);
        std::string name = st.clip;
        if (!m && !st.fallbackClip.empty()) { m = ImportedWith(st.fallbackClip); name = st.fallbackClip; }
        if (!m) return false;
        const float keepBlend = m->blendTime;
        m->loop = st.loop;
        m->speed = StateSpeed(st);
        if (blend >= 0.0f) m->blendTime = blend;
        const bool ok = m->PlayHeld(name);
        m->blendTime = keepBlend;
        if (!ok) return false;
        m_importHold = m;
        return true;
    }

    // ---- Layers: their own playback, posed after the base (see LayerPose) ----
    // The state is held by NAME and looked up each use: `states` may grow (and
    // reallocate) while a layer plays, which would leave a State* dangling.
    struct LayerPlay { std::string st; float time = 0.0f, phase = 0.0f; };
    /// Advance a layer motion and list its (clip, time) samples + weights.
    void LayerMotion(LayerPlay& lp, float dt, AnimBackend* ma,
                     std::vector<std::pair<int, float>>& ct, std::vector<float>& w, bool advance) {
        const State* sp = FindState(lp.st);   // "" (no state) finds nothing
        if (!sp) return;
        const State& st = *sp;
        const float spd = StateSpeed(st);
        if (!st.blendTree) {
            int c = MotionClip(ma, st, st.clip);
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
            int c = MotionClip(ma, st, st.motions[i].clip);
            if (c < 0 || bw[i] <= 0.0f) continue;
            cycle += ma->ClipLength(c) * bw[i]; sum += bw[i];
        }
        if (sum <= 0.0f) return;
        cycle /= sum;
        const float rate = spd * MatchRate(st, bw);
        if (advance) lp.phase = std::fmod(lp.phase + dt * rate / std::fmax(cycle, 1e-4f), 1.0f);
        for (std::size_t i = 0; i < st.motions.size(); ++i) {
            int c = MotionClip(ma, st, st.motions[i].clip);
            if (c < 0 || bw[i] <= 0.0f) continue;
            ct.push_back({c, lp.phase * ma->ClipLength(c)}); w.push_back(bw[i] / sum);
        }
    }
    void LayerUpdate(float dt, AnimBackend* ma) {
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
        std::vector<std::pair<int, float>> ctCur; std::vector<float> wCur;
        // An empty clip name is a layer that plays nothing. A name that does not
        // resolve is a missing clip: hold, and let OnClipEnd move on.
        const bool clipMissing = !cur->blendTree && !cur->clip.empty() && MotionClip(ma, *cur, cur->clip) < 0;
        float prevPhase = m_lCur.phase;
        LayerMotion(m_lCur, dt, ma, ctCur, wCur, !clipMissing);
        if (clipMissing) {
            NoteMissingClip(*cur);
            m_normTime = 1.0f;
        } else if (cur->blendTree) {
            m_normTime += (m_lCur.phase >= prevPhase) ? m_lCur.phase - prevPhase : m_lCur.phase + 1.0f - prevPhase;
        } else if (!ctCur.empty()) {
            float len = ma->ClipLength(ctCur[0].first);
            m_normTime = len > 1e-5f ? m_lCur.time / len : 0.0f;
        }
        float f = 1.0f;
        // Fade from the frozen pose captured on enter. A missing clip holds that
        // pose instead of advancing whatever was playing before.
        if (!m_lSnap.empty() && m_fadeDur > 0.0f && !clipMissing) {
            m_fadeT += dt;
            f = std::fmin(1.0f, m_fadeT / m_fadeDur);
            f = f * f * (3.0f - 2.0f * f);
            if (m_fadeT >= m_fadeDur) { m_lSnap.clear(); f = 1.0f; }
        }
        if (clipMissing) {
            for (const LayerSnap& s : m_lSnap) { m_lCT.push_back({s.clip, s.time}); m_lW.push_back(s.w); }
        } else {
            for (const LayerSnap& s : m_lSnap) { m_lCT.push_back({s.clip, s.time}); m_lW.push_back(s.w * (1.0f - f)); }
            for (std::size_t i = 0; i < ctCur.size(); ++i) { m_lCT.push_back(ctCur[i]); m_lW.push_back(wCur[i] * f); }
        }
        EvaluateTransitions(dt, ma, cur);
    }
    /// Any State first, then the current state's own transitions (first match
    /// wins). While a transition is still blending, its interruption setting
    /// decides which of the target's / source's transitions may cut it short.
    void EvaluateTransitions(float dt, AnimBackend* ma, const State* cur) {
        if (m_trActive) {
            m_trLeft -= dt;
            if (const State* src = FindState(m_trFrom)) {   // the source keeps its own clock
                int c = src->blendTree ? -1 : MotionClip(ma, *src, src->clip);
                float len = c >= 0 ? ma->ClipLength(c) : 1.0f;
                m_trSrcNorm += len > 1e-5f ? dt * std::fabs(StateSpeed(*src)) / len : 0.0f;
            }
            if (m_trLeft <= 0.0f) m_trActive = false;
        }
        if (hold) return;
        // A locked state (death) ignores Any State. Jump, fall and later actions
        // cannot leave it; its own transitions (clip end, revive) still run below.
        if (!cur || cur->interruptible) {
            for (const Transition& tr : anyState) {
                if (tr.to == m_current && !tr.canInterruptSelf) continue;
                if (Try(tr, ma, cur)) return;
            }
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
        AnimBackend* ma = Anim();
        if (!ma || m_lCT.empty() || layerWeight <= 0.0f) return;
        ma->PoseLayer(m_lCT, m_lW, maskBone, std::fmin(layerWeight, 1.0f), additive,
                      maskBones.empty() ? nullptr : &maskBones);
    }
    // Looked up by name every frame (states may be edited at runtime); the last
    // hit is tried first, so the usual case is one string compare.
    const State* FindState(const std::string& name) const {
        if (name.empty()) return nullptr;
        if (m_findHint < states.size() && states[m_findHint].name == name) return &states[m_findHint];
        for (std::size_t i = 0; i < states.size(); ++i)
            if (states[i].name == name) { m_findHint = i; return &states[i]; }
        return nullptr;
    }
    /// Warn once per state whose clip isn't loaded, and remember it so OnClipEnd
    /// is true without testing the clip still on screen.
    void NoteMissingClip(const State& st) {
        m_missingClip = true;
        if (!m_missingWarned.insert(st.name).second) return;
        OKAY_WARN("Animator: state \"", st.name, "\" has no clip \"", st.clip,
                  "\" — holding the pose and moving on");
    }
    bool Test(Cond c, const std::string& param, float value, AnimBackend* ma, const State* cur) const {
        switch (c) {
            case Cond::OnClipEnd:
                // A missing clip has already ended: don't test the clip still on screen.
                if (m_missingClip) return true;
                // A blend tree "ends" after one full cycle; a clip when it finishes.
                // An imported stand-in finishes on the child model, not the body.
                if (m_importHold) return m_importHold->ClipFinished();
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
    bool Satisfied(const Transition& tr, AnimBackend* ma, const State* cur) const {
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
    bool Try(const Transition& tr, AnimBackend* ma, const State* cur, bool presatisfied = false) {
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
        AnimBackend* ma = Anim();
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
        if (first) {
            m_ikW = std::fmin(std::fmax(st.ikWeight, 0.0f), 1.0f);
            m_fbW = st.fullBodyIkWeight < 0.0f ? m_ikW : std::fmin(std::fmax(st.fullBodyIkWeight, 0.0f), 1.0f);
        }
        m_bLo = m_bHi = -1; m_bW = 0.0f;
        m_missingClip = false;
        if (layer > 0) {   // layers crossfade from the pose on screen, frozen
            // A second change while a fade is still running recaptures that mix,
            // so the pose already leaving isn't thrown away.
            m_lSnap.clear();
            for (std::size_t i = 0; i < m_lCT.size() && i < m_lW.size(); ++i)
                if (m_lW[i] > 1e-5f) m_lSnap.push_back(LayerSnap{m_lCT[i].first, m_lCT[i].second, m_lW[i]});
            m_lCur = LayerPlay{st.name, 0.0f, offset};
            if (!st.blendTree) {
                int c = MotionClip(ma, st, st.clip);
                if (c >= 0) m_lCur.time = offset * ma->ClipLength(c);
                else if (!st.clip.empty()) { NoteMissingClip(st); m_normTime = 1.0f; }
            }
            m_fadeT = 0.0f;
            m_fadeDur = (!m_lSnap.empty() && blend > 0.0f) ? blend : 0.0f;
            if (m_fadeDur <= 0.0f) m_lSnap.clear();
            if (onStateEnter) onStateEnter(st.name);
            if (!st.enterEvent.empty()) ma->FireEvent(st.enterEvent);
            return;
        }
        ma->SetSpeed(StateSpeed(st));
        ma->SetLoop(st.loop);
        if (st.blendTree && st.blendDims == 2) {
            ReleaseImported();
            Weights2D(st.motions, GetFloat(st.blendParam), GetFloat(st.blendParamY), m_bWeights);
            std::vector<int> idx;
            for (std::size_t i = 0; i < st.motions.size(); ++i)
                if (m_bWeights[i] > 0.0f) idx.push_back(MotionClip(ma, st, st.motions[i].clip));
            ma->CaptureCrossfadeMany(idx, blend);
            if (!st.loop) ma->RestartBlendPhase();
            else ma->ResetBlendPhase(offset);
            ma->SetExternalDrive(true);
        } else if (st.blendTree) {
            ReleaseImported();
            // Crossfade from the current pose into the tree, which BlendClips then poses.
            int lo, hi; float w;
            PickMotions(st, GetFloat(st.blendParam), lo, hi, w);
            ma->CaptureCrossfade(lo >= 0 ? MotionClip(ma, st, st.motions[lo].clip) : -1,
                                 hi >= 0 ? MotionClip(ma, st, st.motions[hi].clip) : -1, blend);
            // A one-shot tree starts at the beginning. A looping tree keeps the
            // transition's normalized offset.
            if (!st.loop) ma->RestartBlendPhase();
            else ma->ResetBlendPhase(offset);
            ma->SetExternalDrive(true);
        } else {
            ma->SetExternalDrive(false);
            // A named clip that fails to play: leave the pose already showing,
            // stop it advancing, and let OnClipEnd move on. An empty clip name
            // is "play nothing". fallbackClip and a child model's stand-in
            // (imported hold) are tried before that hold.
            // Jump holds its last frame even if a saved graph still has it
            // flagged to loop. Fall and every other state keep their own flag.
            ma->SetLoop(st.loop && st.name != "Jump");
            const std::string play = ClipToPlay(ma, st);
            if (ma->FindClip(play) >= 0) {
                ReleaseImported();
                if (!ma->Play(play, blend)) {
                    if (!play.empty()) { NoteMissingClip(st); ma->SetSpeed(0.0f); }
                } else if (offset > 0.0f && ma->ActiveClip() >= 0) {
                    ma->SetTime(offset * ma->ClipLength(ma->ActiveClip()));
                }
            } else if (!HoldImported(st, blend)) {
                if (!play.empty()) { NoteMissingClip(st); ma->SetSpeed(0.0f); }
                else ma->Play(play, blend);
            }
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
    float m_ikW = 1.0f, m_fbW = 1.0f, m_ikBlend = 0.25f;
    float m_clock = 0.0f;
    std::vector<HistoryEntry> m_history;
    // the transition currently blending (interruption rules)
    bool  m_trActive = false;
    float m_trLeft = 0.0f, m_trSrcNorm = 0.0f;
    std::string m_trFrom;
    Transition::Interrupt m_trRule = Transition::Interrupt::NextState;
    int   m_interruptions = 0;
    // layer playback. m_lSnap is the pose on screen when this state was entered
    // (one clip, or a fade already in progress), held still while it blends out.
    struct LayerSnap { int clip = -1; float time = 0.0f; float w = 0.0f; };
    LayerPlay m_lCur;
    std::vector<LayerSnap> m_lSnap;
    bool m_missingClip = false;                       // the current state's clip failed to play
    std::unordered_set<std::string> m_missingWarned;  // warned once per state name
    std::vector<int> m_motionClips;           // per-frame scratch (2D blend tree clip indices)
    float m_ikWritten = -1.0f, m_fbWritten = -1.0f, m_ikRefresh = 0.0f;   // last IK weights pushed / time to the next push
    mutable std::size_t m_findHint = 0;       // FindState's last hit
    float m_fadeT = 0.0f, m_fadeDur = 0.0f;
    bool  m_hooked = false;
    std::vector<std::pair<int, float>> m_lCT;
    std::vector<float> m_lW;
    mutable std::unique_ptr<CharacterAnimBackend> m_charBackend;   // the backend on a Character body
    ModelAnimator* m_importHold = nullptr;     // child model playing a missing clip's stand-in
};

/// How long stance changes take on the body `go` (its own base Animator, or
/// its model child's), or the defaults when it has none.
inline StanceTimes StanceTimesOf(GameObject* go) {
    if (!go) return StanceTimes{};
    for (AnimStateMachine* sm : go->GetComponents<AnimStateMachine>())
        if (sm->layer == 0) return sm->stanceTimes;
    if (go->transform)
        for (Transform* c : go->transform->Children())
            if (c && c->gameObject)
                for (AnimStateMachine* sm : c->gameObject->GetComponents<AnimStateMachine>())
                    if (sm->layer == 0) return sm->stanceTimes;
    return StanceTimes{};
}

} // namespace okay
