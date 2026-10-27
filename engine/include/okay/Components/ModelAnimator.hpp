#pragma once
// ---------------------------------------------------------------------------
// ModelAnimator — a named CLIP LIBRARY + switcher for an imported model. A glTF can
// carry several animations (idle / walk / run); this holds them all and plays one at a
// time across the model's nodes. Lives on the import root.
//
// Each clip is a set of (nodeName -> AnimationClip) tracks. The animator binds every
// node name to its Transform once (re-resolved only when objects are created or
// destroyed) and samples the playing clip straight into those transforms each frame —
// no per-bone components, no clip copies on a switch, no name lookups per frame. The
// library serializes, so a saved model keeps all its animations.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/AnimBackend.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/Stance.hpp"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <cmath>

namespace okay {

class ModelAnimator : public Behaviour, public AnimBackend {
public:
    struct NodeClip  { std::string node; AnimationClip clip; };  ///< one node's tracks for a clip
    /// A named marker on a clip's timeline; fires as playback crosses it
    /// (footstep sounds, hit windows) — same idea as Character's AnimEvents.
    struct ClipEvent { float time = 0.0f; std::string name; };
    struct Clip      { std::string name; std::vector<NodeClip> nodes; std::vector<ClipEvent> events; bool normalized = false; };

    std::vector<Clip> clips;
    int   active   = 0;      ///< index of the clip to play
    bool  autoPlay = true;   ///< play `active` on Start
    float speed    = 1.0f;
    bool  loop     = true;
    /// Crossfade time (seconds) when switching clips: the pose eases from where it
    /// was into the new clip instead of snapping. 0 = instant switch.
    float blendTime = 0.25f;

    /// Root motion: move the OBJECT by the clip's root-bone ground translation
    /// instead of letting the bone slide inside the model — a walk clip then
    /// really walks the character forward. The root bone's X/Z position tracks
    /// are stripped from playback (Y bounce stays on the bone) and their per-frame
    /// delta is applied to this GameObject in world space.
    bool        rootMotion = false;
    std::string rootMotionNode;   ///< bone to read ("" = auto: the node whose ground track travels)
    /// With rootMotion: also move the object VERTICALLY by the root's Y track
    /// (jump/climb/vault clips really lift the object, not just the bone).
    bool        rootMotionY = false;
    /// With rootMotion: also turn the object by the root bone's turning (yaw) —
    /// turn-in-place and curved-walk clips really rotate the character.
    bool        rootMotionRotation = false;
    /// What root motion moves: 0 = this object, 1 = its parent (the player /
    /// controller object the model sits under). A Rigidbody3D on the target gets
    /// the motion as velocity so collisions still apply.
    int         rootMotionApplyTo = 0;
    /// When set, root motion is handed here (world-space move, yaw degrees)
    /// instead of being applied — for custom controllers.
    std::function<void(const Vec3&, float)> onRootMotion;   // not serialized
    /// The root motion applied during the last update (world units / degrees).
    Vec3  LastRootDelta() const { return m_lastRootDelta; }
    float LastRootYaw() const { return m_lastRootYaw; }
    /// Animate IN PLACE: strip the root bone's ground translation WITHOUT moving
    /// the object — for models driven by a controller (the controller moves the
    /// capsule; the clip should only cycle the limbs). Without this, a walk/run
    /// clip's baked forward travel slides the model away from its collider and
    /// snaps it back every loop. Ignored when rootMotion is on (that strips too).
    bool        inPlace = false;

    /// Continuous locomotion (used with driveByMovement): instead of switching
    /// idle/walk/run discretely, the two clips around the current speed are
    /// evaluated at a shared normalized phase (foot cycles stay aligned) and
    /// blended per bone every frame — a 1D blend tree.
    bool smoothLocomotion = false;

    /// Push callback for fired clip events; or poll with Consume/NextAnimEvent.
    std::function<void(const std::string&)> onAnimEvent;   // not serialized
    /// Report an animation event: queued for anim_event(), sent to onAnimEvent,
    /// and to every other component's OnAnimationEvent (visual scripts' handlers).
    void FireEvent(const std::string& name) override {
        if (name.empty()) return;
        m_firedEvents.push_back(name);
        if (m_firedEvents.size() > 64) m_firedEvents.erase(m_firedEvents.begin());   // nobody polling
        if (onAnimEvent) onAnimEvent(name);
        if (!gameObject) return;
        const auto& cs = gameObject->Components();   // by index: a handler may add components
        for (std::size_t i = 0; i < cs.size(); ++i)
            if (cs[i].get() != this) cs[i]->OnAnimationEvent(name);
    }
    std::vector<std::string> ConsumeAnimEvents() { auto q = std::move(m_firedEvents); m_firedEvents.clear(); return q; }
    std::string NextAnimEvent() {
        if (m_firedEvents.empty()) return {};
        std::string n = m_firedEvents.front();
        m_firedEvents.erase(m_firedEvents.begin());
        return n;
    }

    /// Length (seconds) of a clip = its longest node track.
    float ClipLength(int i) const override {
        if (i < 0 || i >= (int)clips.size()) return 0.0f;
        float len = 0.0f;
        for (const auto& nc : clips[i].nodes) len = std::fmax(len, nc.clip.Length());
        return len;
    }

    // ---- Locomotion: auto-switch idle/walk/run from how fast this object moves ----
    bool        driveByMovement = false;
    std::string idleClip, walkClip, runClip;   ///< clip names for each state ("" = skip)
    float       walkThreshold = 0.3f;          ///< speed above which it's "walking"
    float       runThreshold  = 3.0f;          ///< speed at/above which it's "running"

    // ---- Air states: jump / fall / land from the object's VERTICAL motion ----
    // Works with any controller (they move the capsule; this watches it move):
    // launching upward plays `jumpClip`, past the apex it switches to `fallClip`,
    // and touching down plays `landClip` once before locomotion resumes. All
    // optional ("" = skip); active only with driveByMovement.
    std::string jumpClip, fallClip, landClip;
    float       airUpVel   = 3.0f;   ///< upward speed that reads as a jump launch
    float       airDownVel = 3.5f;   ///< sustained downward speed that reads as falling
    /// True while the air heuristic says we're off the ground (for gameplay checks).
    bool InAir() const { return m_inAir; }

    // ---- Stances: crouch / prone clips, following the player's stance ----
    // A movement controller's crouch / prone (reported to the player's Character)
    // switches locomotion to these, crossfaded over the stance change times;
    // getting up from prone plays `getUpClip` once. A stance with no clips of
    // its own keeps the standing ones. Active only with driveByMovement.
    std::string crouchClip, crouchWalkClip, proneClip, crawlClip, getUpClip;
    /// The stance whose clips play (0 stand, 1 crouch, 2 prone).
    int StanceClips() const { return m_stance; }

    void Start() override {
        if (transform) { m_lastPos = transform->Position(); m_haveLast = true; }
        if (autoPlay && !clips.empty()) PlayIndex(active);
    }

    void Update(float dt) override {
        if (externalDrive) return;   // a state machine blend tree is posing the model
        // Locomotion speed (world XZ units/sec) + vertical speed, measured from
        // how the object moved.
        float spd = -1.0f, vy = 0.0f;
        const float prevVy = m_lastVy;
        if (driveByMovement && dt > 0.0f && transform) {
            Vec3 p = transform->Position();
            if (m_haveLast) {
                // What moved us since last frame was last frame's physics step:
                // measure it over that frame's time. Over this one's, an uneven
                // frame rate read as the speed jumping between idle and run.
                const float span = m_lastDt > 0.0f ? m_lastDt : dt;
                float dx = p.x - m_lastPos.x, dz = p.z - m_lastPos.z;
                spd = std::sqrt(dx*dx + dz*dz) / span;
                vy = (p.y - m_lastPos.y) / span;
                // A teleport / respawn isn't speed: keep last frame's reading.
                if ((p - m_lastPos).SqrMagnitude() > 4.0f) { spd = m_lastSpd; vy = m_lastVy; }
                // Neither is a one-frame pop (a step lift, a platform's nudge): a
                // reading over twice the last plus 2 m/s waits a frame and counts
                // only if the next frame agrees, so the blend doesn't surge to run.
                else if (spd > m_lastSpd * 2.0f + 2.0f && !m_spikeHeld) { m_spikeHeld = true; spd = m_lastSpd; vy = m_lastVy; }
                else m_spikeHeld = false;
                m_lastSpd = spd; m_lastVy = vy;
            }
            m_lastPos = p; m_haveLast = true; m_lastDt = dt;
            // A held clip (script / visual script Play) or an AnimStateMachine picks
            // the clips: movement-driven selection waits (the position keeps being
            // tracked, so it resumes without a speed spike).
            if (m_held || StateMachineDrives()) spd = -1.0f;
        }

        // Air states: launch -> jump clip, past the apex -> fall clip, touchdown
        // -> land one-shot; locomotion stands down while airborne. Driven purely
        // by the capsule's vertical motion, so every controller "just works".
        if (spd >= 0.0f && (!jumpClip.empty() || !fallClip.empty())) {
            if (!m_inAir) {
                // Rising two frames running: a step up a curb or stair lifts the
                // body in a single frame, which read as a jump.
                bool launch = vy > airUpVel && prevVy > airUpVel;
                if (vy < -airDownVel) m_airT += dt; else m_airT = 0.0f;
                if (launch || m_airT > 0.12f) {          // brief drops (steps) don't count
                    m_inAir = true; m_airT = 0.0f; m_fell = !launch;
                    const std::string& c = (launch && !jumpClip.empty()) ? jumpClip : fallClip;
                    if (!c.empty()) Play(c);
                }
            } else {
                // Apex: a jump turns into a fall once we're heading down.
                if (vy < -1.0f && !fallClip.empty() && CurrentName() == jumpClip) Play(fallClip);
                // Touchdown: vertical motion dies out (the ground stops us). A jump's
                // apex also passes zero, so that counts only once we've been
                // falling, or with ground right under the feet (landing on a ledge
                // on the way up).
                if (vy < -1.0f) m_fell = true;
                if (std::fabs(vy) < 0.4f && (m_fell || GroundUnderFeet())) m_airT += dt; else m_airT = 0.0f;
                if (m_airT > 0.06f) {
                    m_inAir = false; m_airT = 0.0f;
                    if (!landClip.empty()) PlayOnce(landClip);   // locomotion resumes after
                }
            }
        }

        // Stance: the player's crouch / prone picks the clip set.
        if (spd >= 0.0f) {
            const int st = BodyStance();
            if (st != m_stance) {
                const int from = m_stance;
                m_stance = st;
                m_stanceFade = StanceChangeTime(from, st);
                if (from == 2 && st == 0 && !getUpClip.empty() && !m_inAir && PlayOnce(getUpClip)) {
                    m_onceReturnTo = FindClip(idleClip);   // the get-up hands over to standing
                    m_stanceFade = -1.0f;
                }
            }
        }

        // Continuous 1D blend (idle<->walk<->run by speed): drives the bones
        // directly and replaces the discrete machinery below while it's active.
        // A one-shot (attack etc.) or an air clip suspends locomotion. (In the air
        // with no clip for it, e.g. walking off a ledge with no fall clip, it
        // carries on: stopping it snapped the body to a stale frame of the walk.)
        const bool airClip = m_inAir && (CurrentName() == jumpClip || CurrentName() == fallClip);
        if (spd >= 0.0f && smoothLocomotion && !m_once && !airClip) {
            // Blend by the speed eased over ~0.1 s: a body stopped dead by a wall
            // (or a respawn) snapped from the walk to the idle in one frame.
            m_blendSpd += (spd - m_blendSpd) * (1.0f - std::exp(-dt / 0.1f));
            if (SmoothBlendStep(dt, m_blendSpd)) return;
        }

        // Fire clip events crossed by this frame's playback window. The clock
        // mirrors the node Animators' time (reset together in PlayIndex).
        if (dt > 0.0f && active >= 0 && active < (int)clips.size()) {
            float len = ClipLength(active);
            float t0 = m_clock, t1 = m_clock + dt * speed;
            const bool first = m_fromStart && t1 > t0;
            if (t1 > t0) m_fromStart = false;
            if (!clips[active].events.empty() && len > 0.0f && t1 > t0) {
                if (loop && !m_once && t1 > len) { FireWindow(active, t0, len, first); FireWindow(active, 0.0f, std::fmod(t1, len), true); }
                else                             FireWindow(active, t0, t1, first);
            }
            // Root motion: what the root bone travelled this frame moves the object
            // instead (measured in world space through the bone's parent, so FBX
            // armature scales and rotated armatures come out right).
            if (rootMotion && len > 0.0f) {
                const bool wrapLoop = loop && !m_once;
                float e0 = wrapLoop ? std::fmod(std::fmod(t0, len) + len, len) : std::fmin(t0, len);
                float e1 = wrapLoop ? std::fmod(std::fmod(t1, len) + len, len) : std::fmin(t1, len);
                Vec3 d; float yaw = 0.0f;
                RootDelta(active, e0, e1, len, d, yaw);   // none (idle clip): a zero move stops a rigidbody
                ApplyRootMotion(d, yaw, dt);
            }
            m_clock = len > 0.0f ? ((loop && !m_once) ? std::fmod(t1, len) : std::fmin(t1, len)) : t1;
            ApplyPose(active, m_clock);
            // A finished one-shot returns to the clip that was playing before it
            // (crossfaded like any switch).
            if (m_once && len > 0.0f && m_clock >= len) {
                m_once = false;
                int rt = m_onceReturnTo; m_onceReturnTo = -1;
                if (rt >= 0 && rt < (int)clips.size() && rt != active) PlayIndex(rt);
            }
        }

        // Discrete locomotion: switch to the clip for this speed band (crossfaded
        // by blendTime, since Play goes through PlayIndex). Suspended by a
        // one-shot or while airborne (the jump/fall clip owns the body).
        if (spd < 0.0f || m_once || m_inAir) return;
        // 10% hysteresis: a speed hovering on a threshold doesn't flicker clips.
        const std::string cur = CurrentName();
        const float runAt  = cur == runClip ? runThreshold * 0.9f : runThreshold;
        const float walkAt = (cur == walkClip || cur == runClip) ? walkThreshold * 0.9f : walkThreshold;
        const std::string* want = &idleClip;
        if (m_stance != 0) {
            const std::string *still, *moving;
            StanceSet(still, moving);
            want = spd > walkAt * (m_stance == 1 ? 0.5f : 0.25f) ? moving : still;
        }
        else if (spd >= runAt && !runClip.empty())   want = &runClip;
        else if (spd > walkAt && !walkClip.empty())  want = &walkClip;
        if (!want->empty() && *want != CurrentName()) {   // switch only on change
            Play(*want, m_stanceFade);
            m_stanceFade = -1.0f;
        }
    }

    /// Crossfade: eases the pose captured at the moment of a clip switch into the
    /// new clip's pose. Runs in LateUpdate so it blends what the node Animators
    /// just wrote this frame (Update order between objects doesn't matter).
    void LateUpdate(float dt) override {
        CrossfadeStep(dt);
        // Layers (and anything else that poses on top of the base pose) run
        // after the base pose and its crossfade are final.
        // (Owners unregister in their OnDestroy — RemoveComponent and object
        // destruction both call it — so every entry here is alive.)
        for (std::size_t i = 0; i < m_postPose.size(); ++i) m_postPose[i].fn(dt);
    }
    /// Run `fn` every frame after the base pose (layers register here).
    /// The owner must call RemovePostPose(owner) from its OnDestroy.
    void AddPostPose(const void* owner, std::function<void(float)> fn) override {
        RemovePostPose(owner);
        m_postPose.push_back({owner, std::move(fn)});
    }
    void RemovePostPose(const void* owner) override {
        for (std::size_t i = 0; i < m_postPose.size(); ++i)
            if (m_postPose[i].owner == owner) { m_postPose.erase(m_postPose.begin() + i); return; }
    }

    void CrossfadeStep(float dt) {
        if (m_blend.empty() || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc) { m_blend.clear(); return; }
        m_blendW = m_fadeTime > 0.001f ? std::fmin(1.0f, m_blendW + dt / m_fadeTime) : 1.0f;
        float w = m_blendW * m_blendW * (3.0f - 2.0f * m_blendW);   // smoothstep ease
        const bool fresh = sc->StructureVersion() == m_blendVer;
        for (BlendFrom& s : m_blend) {
            if (!fresh) s.t = NodeT(s.node);   // objects came or went: re-resolve by name
            Transform* t = s.t;
            if (!t) continue;
            t->localPosition = s.pos + (t->localPosition - s.pos) * w;
            t->localScale    = s.scl + (t->localScale    - s.scl) * w;
            Quat b = t->localRotation;
            float d = s.rot.x*b.x + s.rot.y*b.y + s.rot.z*b.z + s.rot.w*b.w;
            if (d < 0.0f) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
            Quat r{s.rot.x + (b.x - s.rot.x) * w, s.rot.y + (b.y - s.rot.y) * w,
                   s.rot.z + (b.z - s.rot.z) * w, s.rot.w + (b.w - s.rot.w) * w};
            t->localRotation = r.Normalized();
        }
        m_blendVer = sc->StructureVersion();
        if (m_blendW >= 1.0f) m_blend.clear();
    }

    int ClipCount() const { return (int)clips.size(); }
    std::vector<std::string> ClipNames() const {
        std::vector<std::string> n; n.reserve(clips.size());
        for (const auto& c : clips) n.push_back(c.name);
        return n;
    }
    const std::string& CurrentName() const {
        static std::string none;
        return (active >= 0 && active < (int)clips.size()) ? clips[active].name : none;
    }

    /// Switch to a clip by name; returns false if there's no such clip. `blend` is
    /// this switch's crossfade in seconds (< 0 = blendTime).
    bool Play(const std::string& name, float blend = -1.0f) override {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].name == name) { PlayIndex(i, false, blend); return true; }
        return false;
    }

    /// Play a clip and HOLD it (a script's or visual script's Play): movement-
    /// driven selection (driveByMovement, air states) and a HumanoidRetarget stand
    /// down until ReleaseHold, so the clip isn't replaced the next frame.
    bool PlayHeld(const std::string& name) {
        const int before = active;
        if (!Play(name)) return false;
        if (!m_held) m_heldReturnTo = before;
        m_held = true;
        return true;
    }
    /// End a hold: locomotion picks the clip again, or without it the clip that
    /// played before the hold comes back.
    void ReleaseHold() {
        if (!m_held) return;
        m_held = false;
        if (!driveByMovement && m_heldReturnTo >= 0 && m_heldReturnTo != active) PlayIndex(m_heldReturnTo);
        m_heldReturnTo = -1;
    }
    /// True while the model's own clip owns the pose: a held clip or a one-shot
    /// (HumanoidRetarget yields until it ends).
    bool ClipOverridesPose() const { return m_held || m_once; }

    /// An AnimStateMachine on this object registers itself here: while it's on
    /// the object and enabled it picks the clips, and driveByMovement waits.
    void SetStateMachine(const Component* sm) override { m_stateMachine = sm; }
    bool StateMachineDrives() const {
        if (!m_stateMachine || !gameObject) return false;
        for (const auto& c : gameObject->Components())
            if (c.get() == m_stateMachine) return c->enabled;
        return false;
    }

    /// Play a clip ONCE (attack / jump / hit reaction), then automatically return
    /// to whatever was playing — both switches crossfade by blendTime. Locomotion
    /// stands down until the one-shot finishes.
    bool PlayOnce(const std::string& name) {
        int i = FindClip(name);
        if (i < 0) return false;
        if (!m_once) m_onceReturnTo = active;   // don't chain-return into another one-shot
        PlayIndex(i, /*once=*/true);
        return true;
    }

    /// True when a non-looping playback (a one-shot, or loop=false) has reached
    /// the end of its clip.
    bool ClipFinished() const override {
        if (active < 0 || active >= (int)clips.size()) return false;
        if (!m_once && loop) return false;
        float len = ClipLength(active);
        return len > 0.0f && m_clock >= len;
    }

    /// Playback time (seconds) into the current clip.
    float Time() const { return m_clock; }
    /// Seek the current clip to `t` seconds and pose the model there.
    void SetTime(float t) override {
        if (active < 0 || active >= (int)clips.size()) return;
        float len = ClipLength(active);
        if (len > 0.0f) t = (loop && !m_once) ? std::fmod(std::fmod(t, len) + len, len) : std::fmin(std::fmax(t, 0.0f), len);
        m_clock = t;
        ApplyPose(active, t);
    }

    /// Switch to a clip by index: push each node's tracks onto that node's Animator.
    /// With blendTime > 0 the current pose is captured first and eased into the new
    /// clip over that many seconds (see LateUpdate). `once` plays it exactly one
    /// time and then returns to the previous clip (see PlayOnce).
    void PlayIndex(int i, bool once = false, float blend = -1.0f) {
        if (i < 0 || i >= (int)clips.size() || !gameObject) return;
        m_once = once;
        active = i;
        m_fadeTime = blend >= 0.0f ? blend : blendTime;
        m_fromStart = true;
        if (!gameObject->scene()) return;
        CaptureFade(i, m_fadeTime);
        m_clock = 0.0f;
        NormalizeClip(clips[i]);
        for (NodeClip& nc : clips[i].nodes) nc.clip.loop = loop;
        // Root motion / in place: the object moves instead of the root bone, so the
        // bone's travel is stripped when the pose is sampled (see SampleNode). For
        // jump/fall clips in in-place mode the rise goes too: the CONTROLLER's
        // capsule already flies the jump arc, and the clip's own root rise would
        // stack on top of it (a double-height jump).
        DisableLegacyBoneAnimators(i);
        ApplyPose(i, 0.0f);
    }

    /// Sample a clip's ground translation (X/Z position tracks) at `tm`, capped
    /// just inside `len` — looping curves wrap t==len back to t==0, which would
    /// cancel a loop's translation in wrap deltas.
    static Vec3 GroundPosAt(const NodeClip& rn, float tm, float len) {
        float cap = len - std::fmax(1e-5f, len * 1e-6f);
        if (tm > cap) tm = cap;
        bool f; Vec3 p{0, 0, 0};
        float v = rn.clip.Evaluate("position.x", tm, f); if (f) p.x = v;
        v = rn.clip.Evaluate("position.z", tm, f);       if (f) p.z = v;
        return p;
    }

    /// Ground translation between two clip times, handling a loop wrap (e1 < e0).
    static Vec3 GroundDelta(const NodeClip& rn, float e0, float e1, float len) {
        if (len <= 0.0f) return Vec3{0, 0, 0};
        if (e1 < e0)
            return (GroundPosAt(rn, len, len) - GroundPosAt(rn, e0, len)) +
                   (GroundPosAt(rn, e1, len) - GroundPosAt(rn, 0.0f, len));
        return GroundPosAt(rn, e1, len) - GroundPosAt(rn, e0, len);
    }

    /// Evaluate a clip's TRS tracks at `t` into pos/rot/scl (untouched components
    /// keep their incoming values). Mirrors Animator::ApplyAt — keep in sync.
    static void EvalClipInto(const AnimationClip& c, float t, Vec3& pos, Quat& rot, Vec3& scl) {
        c.SampleTRS(t, pos, rot, scl);
    }

    // ---- Clip surgery (the editor's animation tools) ----
    /// Reverse a clip in time (plays backwards); event times flip with it.
    void ReverseClip(int i) {
        if (i < 0 || i >= (int)clips.size()) return;
        InvalidateBindings();
        float len = ClipLength(i);
        for (auto& nc : clips[i].nodes) nc.clip.Reverse();
        for (auto& ev : clips[i].events) ev.time = len - ev.time;
    }
    /// Cut a clip down to [t0, t1] (boundaries sampled exactly); events outside
    /// the window drop, the rest shift to the new zero.
    void TrimClip(int i, float t0, float t1) {
        if (i < 0 || i >= (int)clips.size() || t1 <= t0) return;
        InvalidateBindings();
        for (auto& nc : clips[i].nodes) nc.clip.Trim(t0, t1);
        std::vector<ClipEvent> kept;
        for (auto& ev : clips[i].events)
            if (ev.time >= t0 && ev.time <= t1) kept.push_back({ev.time - t0, ev.name});
        clips[i].events = std::move(kept);
    }
    /// Stretch/compress a clip in time (2 = twice as long / half speed).
    void ScaleClipTime(int i, float factor) {
        if (i < 0 || i >= (int)clips.size() || factor <= 1e-4f) return;
        InvalidateBindings();
        for (auto& nc : clips[i].nodes) nc.clip.ScaleTime(factor);
        for (auto& ev : clips[i].events) ev.time *= factor;
    }
    /// Bake a clip IN PLACE permanently: strip its root node's ground translation
    /// (and optionally the Y rise) out of the library copy — turns a downloaded
    /// travelling clip into an in-place one for controller-driven characters.
    /// Returns true if a travelling root was found and stripped.
    bool BakeClipInPlace(int i, bool stripY = false) {
        if (i < 0 || i >= (int)clips.size()) return false;
        InvalidateBindings();
        int keep = active; active = i;
        const NodeClip* rn = RootMotionClip();
        active = keep;
        if (!rn) return false;
        for (auto& nc : clips[i].nodes)
            if (&nc == rn) {
                nc.clip.RemoveTrack("position.x");
                nc.clip.RemoveTrack("position.z");
                if (stripY) nc.clip.RemoveTrack("position.y");
                return true;
            }
        return false;
    }

    /// Keys before / after the last OptimizeClips (the import report shows them). Runtime only.
    std::size_t lastOptimizeBefore = 0, lastOptimizeAfter = 0;
    /// Keyframe reduction on every clip (baked imports carry a key per frame on
    /// every channel; most are on straight lines). Returns {before, after} keys.
    std::pair<std::size_t, std::size_t> OptimizeClips(float tolerance = 0.0005f) {
        std::size_t before = 0, after = 0;
        for (auto& c : clips)
            for (auto& nc : c.nodes) {
                before += nc.clip.KeyCount();
                nc.clip.Reduce(tolerance, tolerance, tolerance);
                after += nc.clip.KeyCount();
            }
        lastOptimizeBefore = before; lastOptimizeAfter = after;
        return {before, after};
    }

    /// Duplicate a clip (returns the new index, -1 on failure) — trim the copy
    /// to cut a long take into pieces without losing the original.
    int DuplicateClip(int i) {
        if (i < 0 || i >= (int)clips.size()) return -1;
        InvalidateBindings();
        Clip copy = clips[i];
        copy.name += " copy";
        clips.push_back(std::move(copy));
        return (int)clips.size() - 1;
    }
    /// Remove a clip entirely (locomotion mappings by that name go stale — the
    /// editor clears them).
    void RemoveClip(int i) {
        if (i < 0 || i >= (int)clips.size()) return;
        InvalidateBindings();
        clips.erase(clips.begin() + i);
        if (active >= (int)clips.size()) active = (int)clips.size() - 1;
        if (active < 0) active = 0;
    }

    /// Index of a clip by name (-1 = none).
    int FindClip(const std::string& name) const override {
        if (name.empty()) return -1;
        for (int i = 0; i < (int)clips.size(); ++i) if (clips[i].name == name) return i;
        return -1;
    }

    /// Capture the CURRENT pose of every node clip `i` animates, to ease into
    /// whatever poses the model next over `seconds` (a missing node just stops
    /// blending).
    void CaptureFade(int i, float seconds) {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || i < 0 || i >= (int)clips.size()) return;
        m_fadeTime = seconds;
        m_blend.clear(); m_blendW = 0.0f;
        m_blendVer = sc->StructureVersion();
        if (seconds > 0.001f) {
            const auto& b = Bind(i);
            for (std::size_t n = 0; n < b.size(); ++n)
                if (Transform* t = b[n])
                    m_blend.push_back({clips[i].nodes[n].node, t, t->localPosition, t->localRotation, t->localScale});
        }
    }

    /// The stance the player's Character reports, as far as this model has clips
    /// for it (prone without prone clips crouches if it can, else stands).
    int BodyStance() const {
        int st = 0;
        for (Transform* t = transform; t; t = t->Parent())
            if (auto* ch = t->gameObject ? t->gameObject->GetComponent<Character>() : nullptr) { st = ch->StanceTarget(); break; }
        if (st == 2 && proneClip.empty() && crawlClip.empty()) st = 1;
        if (st == 1 && crouchClip.empty() && crouchWalkClip.empty()) st = 0;
        return st;
    }
    /// The still and moving clips of the current stance (one stands in for a missing other).
    void StanceSet(const std::string*& still, const std::string*& moving) const {
        const std::string& a = m_stance == 2 ? proneClip : crouchClip;
        const std::string& b = m_stance == 2 ? crawlClip : crouchWalkClip;
        still = a.empty() ? &b : &a;
        moving = b.empty() ? &a : &b;
    }
    /// Seconds a stance change takes (the controllers' default stance times).
    static float StanceChangeTime(int from, int to) {
        const StanceTimes t;
        if (from == to) return 0.0f;
        if (from + to == 1) return t.standCrouch;   // stand <-> crouch
        if (from + to == 3) return t.crouchProne;   // crouch <-> prone
        return from == 2 ? t.proneStand : t.standCrouch + t.crouchProne;
    }

    /// One frame of continuous locomotion blending. Returns false when it can't
    /// run (no scene / no usable clips) so the caller falls back to discrete mode.
    bool SmoothBlendStep(float dt, float spd) {
        if (!gameObject || !gameObject->scene()) return false;
        // The two clips around this speed + the blend weight between them.
        int A, B; float w, lo, hi;   // lo/hi: the ground speed each clip is made for
        // A clip's own root travel when it has one, else the threshold it's full at.
        auto clipSpeed = [&](int ci, float thr) { float v = ClipRootSpeed(ci); return v > 1e-3f ? v : thr; };
        const bool still = m_stance != 0 || spd <= walkThreshold;   // A is a standing-still clip
        if (m_stance != 0) {   // crouched / prone: still <-> moving up to that stance's pace
            const std::string *a, *b;
            StanceSet(a, b);
            const float full = walkThreshold * (m_stance == 1 ? 0.5f : 0.25f);
            A = FindClip(*a); B = FindClip(*b);
            w = full > 1e-4f ? spd / full : 1.0f;
            lo = 0.0f; hi = clipSpeed(B, full);
        } else if (spd <= walkThreshold) {
            A = FindClip(idleClip); B = FindClip(walkClip);
            w = walkThreshold > 1e-4f ? spd / walkThreshold : 1.0f;
            lo = 0.0f; hi = clipSpeed(B, walkThreshold);
        } else {
            A = FindClip(walkClip); B = FindClip(runClip);
            w = (spd - walkThreshold) / std::fmax(runThreshold - walkThreshold, 1e-4f);
            lo = clipSpeed(A, walkThreshold); hi = clipSpeed(B, runThreshold);
        }
        if (w < 0.0f) w = 0.0f; if (w > 1.0f) w = 1.0f;
        // Play the steps as fast as the body actually moves, so feet don't skate
        // (unless root motion is what moves it: then the clip sets the pace).
        float clipSpd = lo + (hi - lo) * w, rate = 1.0f;
        if (!rootMotion && clipSpd > 1e-3f) rate = std::fmin(2.0f, std::fmax(0.5f, spd / clipSpd));
        if (still) rate = 1.0f + (rate - 1.0f) * w;   // idle plays at its own pace
        if (m_stanceFade >= 0.0f) { CaptureFade(A >= 0 ? A : B, m_stanceFade); m_stanceFade = -1.0f; }   // ease into the stance's clips
        return BlendClips(dt * rate, A, B, w);
    }

    /// Set by a driver (e.g. an AnimStateMachine blend-tree state) that poses the
    /// model itself through BlendClips each frame: Update then skips its own clip
    /// logic so the two don't fight. Crossfades (LateUpdate) still apply.
    bool externalDrive = false;

    /// Pose the model as a weighted mix of clips A and B (w = 0 -> A, 1 -> B),
    /// both sampled at one shared normalized phase so footfalls line up. Drives
    /// the bones directly (their Animators pause), blends root motion and fires
    /// the dominant clip's events. Returns false if neither clip exists.
    bool BlendClips(float dt, int A, int B, float w) override {
        if (!gameObject || !gameObject->scene()) return false;
        if (w < 0.0f) w = 0.0f; if (w > 1.0f) w = 1.0f;
        if (A >= (int)clips.size()) A = -1;
        if (B >= (int)clips.size()) B = -1;
        if (A < 0 && B < 0) return false;
        if (A < 0) { A = B; w = 1.0f; }
        if (B < 0) { B = A; w = 0.0f; }
        float lenA = ClipLength(A), lenB = ClipLength(B);
        if (lenA <= 0.0f && lenB <= 0.0f) return false;
        if (lenA <= 0.0f) lenA = lenB;
        if (lenB <= 0.0f) lenB = lenA;

        // Shared normalized phase: both clips sample the same fraction of their
        // cycle, so left/right footfalls stay aligned through the blend.
        float prevPhase = m_phase;
        float cycle = lenA + (lenB - lenA) * w;
        m_phase = std::fmod(m_phase + dt * speed / std::fmax(cycle, 1e-4f), 1.0f);
        float tA = m_phase * lenA, tB = m_phase * lenB;
        active = w < 0.5f ? A : B;   // the dominant clip (UI + events)

        // Root motion: both clips' root deltas, blended by the same weight.
        if (rootMotion) {
            Vec3 d{0, 0, 0}, dA, dB; float yaw = 0.0f, yA = 0.0f, yB = 0.0f;
            bool any = false;
            if (RootDelta(A, prevPhase * lenA, tA, lenA, dA, yA)) { d = d + dA * (1.0f - w); yaw += yA * (1.0f - w); any = true; }
            if (RootDelta(B, prevPhase * lenB, tB, lenB, dB, yB)) { d = d + dB * w; yaw += yB * w; any = true; }
            (void)any;
            ApplyRootMotion(d, yaw, dt);
        }

        // Events: the dominant clip's markers, over this frame's phase window.
        {
            float len = active == A ? lenA : lenB;
            float e0 = prevPhase * len, e1 = m_phase * len;
            const bool first = m_fromStart && e1 != e0;
            if (e1 != e0) m_fromStart = false;
            if (e1 < e0) { FireWindow(active, e0, len, first); FireWindow(active, 0.0f, e1, true); }
            else FireWindow(active, e0, e1, first);
        }

        // Drive the union of both clips' nodes directly. Nodes in only one clip
        // use it unblended. The A->B node pairing is cached per clip pair.
        // Bind first: it refreshes the node cache when the scene changed, which
        // also drops the cached pairings - a PairMap taken before it would dangle.
        const auto& bA = Bind(A);
        const auto& bB = Bind(B);
        const PairMap& pm = Pair(A, B);
        auto blendWrite = [&](Transform* t, int ia, int ib) {
            Vec3 pA = t->localPosition, sA = t->localScale; Quat rA = t->localRotation;
            Vec3 pB = pA, sB = sA; Quat rB = rA;
            if (ia >= 0) SampleNode(A, ia, tA, pA, rA, sA);
            if (ib >= 0) SampleNode(B, ib, tB, pB, rB, sB);
            float bw = (ia >= 0 && ib >= 0) ? w : (ib >= 0 ? 1.0f : 0.0f);
            float dq = rA.x*rB.x + rA.y*rB.y + rA.z*rB.z + rA.w*rB.w;
            if (dq < 0.0f) { rB.x = -rB.x; rB.y = -rB.y; rB.z = -rB.z; rB.w = -rB.w; }
            Quat r{rA.x + (rB.x - rA.x) * bw, rA.y + (rB.y - rA.y) * bw,
                   rA.z + (rB.z - rA.z) * bw, rA.w + (rB.w - rA.w) * bw};
            t->localPosition = pA + (pB - pA) * bw;
            t->localScale    = sA + (sB - sA) * bw;
            t->localRotation = r.Normalized();
        };
        for (std::size_t n = 0; n < bA.size(); ++n)
            if (bA[n]) blendWrite(bA[n], (int)n, A != B ? pm.aToB[n] : (int)n);
        if (A != B)
            for (int nb : pm.onlyB)
                if (bB[nb]) blendWrite(bB[nb], -1, nb);
        ApplyShapeWeights(active, active == A ? tA : tB);   // face shapes follow the dominant clip
        return true;
    }
    /// Pose the model as a weighted mix of any number of clips (2D blend trees),
    /// all sampled at one shared normalized phase. Weights are normalized; root
    /// motion is blended by the same weights; the heaviest clip fires events.
    bool BlendMany(float dt, const std::vector<int>& clipIdx, const std::vector<float>& weights) override {
        if (!gameObject || !gameObject->scene()) return false;
        // Scratch kept on the component: no per-frame allocation.
        std::vector<int>& ci = m_bmClip; std::vector<float>& w = m_bmW; std::vector<float>& len = m_bmLen;
        ci.clear(); w.clear(); len.clear();
        float sum = 0.0f;
        for (std::size_t i = 0; i < clipIdx.size() && i < weights.size(); ++i) {
            int c = clipIdx[i];
            if (c < 0 || c >= (int)clips.size() || weights[i] <= 1e-4f) continue;
            float l = ClipLength(c);
            if (l <= 0.0f) continue;
            ci.push_back(c); w.push_back(weights[i]); len.push_back(l); sum += weights[i];
        }
        if (ci.empty()) return false;
        float cycle = 0.0f; int dom = 0;
        for (std::size_t i = 0; i < w.size(); ++i) { w[i] /= sum; cycle += len[i] * w[i]; if (w[i] > w[dom]) dom = (int)i; }
        float prevPhase = m_phase;
        m_phase = std::fmod(m_phase + dt * speed / std::fmax(cycle, 1e-4f), 1.0f);
        active = ci[dom];
        if (rootMotion) {
            Vec3 d{0, 0, 0}; float yaw = 0.0f;
            for (std::size_t i = 0; i < ci.size(); ++i) {
                Vec3 di; float yi;
                if (RootDelta(ci[i], prevPhase * len[i], m_phase * len[i], len[i], di, yi)) { d = d + di * w[i]; yaw += yi * w[i]; }
            }
            ApplyRootMotion(d, yaw, dt);
        }
        {   // events of the dominant clip over this frame's phase window
            float L = len[dom], e0 = prevPhase * L, e1 = m_phase * L;
            const bool first = m_fromStart && e1 != e0;
            if (e1 != e0) m_fromStart = false;
            if (e1 < e0) { FireWindow(ci[dom], e0, L, first); FireWindow(ci[dom], 0.0f, e1, true); }
            else FireWindow(ci[dom], e0, e1, first);
        }
        std::vector<std::pair<int, float>>& ct = m_bmCT;
        ct.clear();
        for (std::size_t i = 0; i < ci.size(); ++i) ct.push_back({ci[i], m_phase * len[i]});
        PoseWeighted(ct, w, nullptr, 1.0f);
        ApplyShapeWeights(ci[dom], m_phase * len[dom]);   // face shapes follow the heaviest clip
        return true;
    }

    /// Sample several (clip, time) pairs, mix them by `weights`, and blend the
    /// result over the current pose by `amount` — only on bones in `mask` when
    /// given. Base for layers and multi-clip blends.
    void PoseWeighted(const std::vector<std::pair<int, float>>& clipTimes, const std::vector<float>& weights,
                      const std::unordered_set<Transform*>* mask, float amount) {
        std::vector<PwAcc>& acc = m_pwAcc;              // scratch kept on the component
        std::unordered_map<Transform*, int>& at = m_pwAt;
        acc.clear(); at.clear();
        for (std::size_t k = 0; k < clipTimes.size() && k < weights.size(); ++k) {
            int c = clipTimes[k].first;
            float wk = weights[k];
            if (c < 0 || c >= (int)clips.size() || wk <= 0.0f) continue;
            const auto& b = Bind(c);
            for (std::size_t n = 0; n < b.size(); ++n) {
                Transform* t = b[n];
                if (!t || (mask && !mask->count(t))) continue;
                auto it = at.find(t);
                int ai;
                if (it == at.end()) { ai = (int)acc.size(); at.emplace(t, ai); acc.push_back({t}); acc.back().ref = t->localRotation; }
                else ai = it->second;
                Vec3 p = t->localPosition, sc = t->localScale; Quat r = t->localRotation;
                SampleNode(c, (int)n, clipTimes[k].second, p, r, sc);
                PwAcc& a = acc[ai];
                if (a.w == 0.0f) a.ref = r;
                if (Quat::Dot(a.ref, r) < 0.0f) r = Quat{-r.x, -r.y, -r.z, -r.w};
                a.p = a.p + p * wk; a.s = a.s + sc * wk;
                a.q = Quat{a.q.x + r.x * wk, a.q.y + r.y * wk, a.q.z + r.z * wk, a.q.w + r.w * wk};
                a.w += wk;
            }
        }
        for (PwAcc& a : acc) {
            if (a.w <= 1e-6f) continue;
            float iw = 1.0f / a.w;
            Vec3 p = a.p * iw, sc = a.s * iw;
            Quat q = Quat{a.q.x * iw, a.q.y * iw, a.q.z * iw, a.q.w * iw}.Normalized();
            Transform* t = a.t;
            if (amount >= 0.999f) { t->localPosition = p; t->localScale = sc; t->localRotation = q; continue; }
            t->localPosition = t->localPosition + (p - t->localPosition) * amount;
            t->localScale    = t->localScale + (sc - t->localScale) * amount;
            t->localRotation = Quat::Slerp(t->localRotation, q, amount).Normalized();
        }
    }

    // ---- AnimBackend: the Animator's fields, and layers by mask bone ----
    int  ActiveClip() const override { return active; }
    void SetSpeed(float s) override { speed = s; }
    void SetLoop(bool l) override { loop = l; }
    void SetExternalDrive(bool on) override { externalDrive = on; }
    void PoseLayer(const std::vector<std::pair<int, float>>& clipTimes, const std::vector<float>& weights,
                   const std::string& maskBone, float amount) override {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        const std::uint64_t ver = sc ? sc->StructureVersion() : 0;
        if (ver != m_maskVer || m_maskName != maskBone) {   // the mask bone's subtree, cached
            m_mask.clear(); m_maskVer = ver; m_maskName = maskBone;
            if (!maskBone.empty())
                if (GameObject* mb = FindNode(maskBone)) {
                    std::vector<Transform*> stack{mb->transform};
                    while (!stack.empty()) {
                        Transform* t = stack.back(); stack.pop_back();
                        m_mask.insert(t);
                        for (Transform* c : t->Children()) stack.push_back(c);
                    }
                }
        }
        if (!maskBone.empty() && m_mask.empty()) return;   // mask bone missing: drive nothing
        PoseWeighted(clipTimes, weights, maskBone.empty() ? nullptr : &m_mask, amount);
    }

    /// CaptureCrossfade for any number of clips (entering a 2D blend tree).
    void CaptureCrossfadeMany(const std::vector<int>& clipIdx, float seconds) override {
        m_blend.clear(); m_blendW = 0.0f;
        m_fadeTime = seconds;
        if (seconds <= 0.001f) return;
        if (gameObject && gameObject->scene()) m_blendVer = gameObject->scene()->StructureVersion();
        for (int ci : clipIdx) {
            if (ci < 0 || ci >= (int)clips.size()) continue;
            const auto& b = Bind(ci);
            for (std::size_t n = 0; n < b.size(); ++n) {
                Transform* t = b[n];
                if (!t) continue;
                bool have = false;
                for (const BlendFrom& bf : m_blend) if (bf.t == t) { have = true; break; }
                if (!have) m_blend.push_back({clips[ci].nodes[n].node, t, t->localPosition, t->localRotation, t->localScale});
            }
        }
    }

    /// Normalized phase (0..1) of the last BlendClips call.
    float BlendPhase() const override { return m_phase; }
    void  ResetBlendPhase(float p = 0.0f) override { m_phase = p; m_fromStart = true; }
    /// Capture the current pose of every node clips A and B animate, so the next
    /// `seconds` ease from it (the same crossfade Play uses) — call when switching
    /// into a BlendClips-driven state.
    void CaptureCrossfade(int A, int B, float seconds) override {
        m_blend.clear(); m_blendW = 0.0f;
        m_fadeTime = seconds;
        if (seconds <= 0.001f) return;
        if (gameObject && gameObject->scene()) m_blendVer = gameObject->scene()->StructureVersion();
        auto grab = [&](int ci) {
            if (ci < 0 || ci >= (int)clips.size()) return;
            const auto& b = Bind(ci);
            for (std::size_t n = 0; n < b.size(); ++n) {
                Transform* t = b[n];
                if (!t) continue;
                bool have = false;
                for (const BlendFrom& bf : m_blend) if (bf.t == t) { have = true; break; }
                if (!have) m_blend.push_back({clips[ci].nodes[n].node, t, t->localPosition, t->localRotation, t->localScale});
            }
        };
        grab(A); grab(B);
    }

    /// The active clip's root-motion source: the named node, else — auto — the
    /// node whose ground translation actually TRAVELS over the clip. glTF rigs
    /// carry (static) position tracks on every joint, so "first node with a
    /// position track" used to pick a random joint and leave the real root
    /// sliding the model out of its collider. Null when nothing travels.
    const NodeClip* RootMotionClip() const {
        int idx = RootIndex(active);
        return idx >= 0 ? &clips[active].nodes[idx] : nullptr;
    }
    /// Index (into clips[ci].nodes) of a clip's root-motion bone, -1 = none.
    /// Cached: the auto search scans every key of every node.
    int RootIndex(int ci) const {
        if (ci < 0 || ci >= (int)clips.size()) return -1;
        if (m_rootCache.size() != clips.size() || m_rootCacheNode != rootMotionNode) {
            m_rootCache.assign(clips.size(), -2);
            m_rootCacheNode = rootMotionNode;
        }
        int& slot = m_rootCache[ci];
        if (slot == -2 || slot >= (int)clips[ci].nodes.size()) {
            const NodeClip* nc = FindRootMotionClip(ci);
            slot = nc ? (int)(nc - clips[ci].nodes.data()) : -1;
        }
        return slot;
    }
    const NodeClip* FindRootMotionClip(int ci) const {
        const int active = ci;
        if (!rootMotionNode.empty()) {
            for (const NodeClip& nc : clips[active].nodes)
                if (nc.node == rootMotionNode &&
                    (nc.clip.HasTrack("position.x") || nc.clip.HasTrack("position.z")))
                    return &nc;
            return nullptr;
        }
        const NodeClip* best = nullptr;
        float bestRange = 0.0f;
        for (const NodeClip& nc : clips[active].nodes) {
            float range = 0.0f;
            for (const char* tr : {"position.x", "position.z"}) {
                auto it = nc.clip.Tracks().find(tr);
                if (it == nc.clip.Tracks().end() || it->second.Empty()) continue;
                float mn = 1e30f, mx = -1e30f;
                for (const auto& k : it->second.Keys()) {
                    mn = std::fmin(mn, k.value);
                    mx = std::fmax(mx, k.value);
                }
                range += mx - mn;
            }
            if (range > bestRange) { bestRange = range; best = &nc; }
        }
        return bestRange > 1e-5f ? best : nullptr;   // nothing travels = nothing to strip
    }

    /// The object named `name` inside THIS model (two characters share bone
    /// names like "mixamorig:Hips" — a scene-wide lookup would let one animate
    /// the other). Falls back to the whole scene for rigs outside the subtree.
    GameObject* FindNode(const std::string& name) const {
        Transform* t = NodeT(name);
        return t ? t->gameObject : nullptr;
    }
    /// The Transform for a node name (cached; see FindNode).
    Transform* NodeT(const std::string& name) const {
        if (!gameObject || !gameObject->transform) return nullptr;
        Scene* sc = gameObject->scene();
        const std::uint64_t ver = sc ? sc->StructureVersion() : 0;
        if (ver != m_nodeVer || m_nodeMap.empty()) {
            m_nodeMap.clear(); m_extMap.clear(); m_bind.clear(); m_pairs.clear();
            m_nodeVer = ver;
            // Depth-first, first name wins (the order the old lookup used).
            std::vector<Transform*> stack{gameObject->transform};
            while (!stack.empty()) {
                Transform* t = stack.back(); stack.pop_back();
                if (t->gameObject) m_nodeMap.emplace(t->gameObject->name, t);
                const auto& ch = t->Children();
                for (auto it = ch.rbegin(); it != ch.rend(); ++it) stack.push_back(*it);
            }
        }
        auto it = m_nodeMap.find(name);
        if (it != m_nodeMap.end()) return it->second;
        // Outside the model: fall back to the whole scene (cached, misses too).
        auto ex = m_extMap.find(name);
        if (ex != m_extMap.end()) return ex->second;
        GameObject* g = sc ? sc->Find(name) : nullptr;
        Transform* t = g ? g->transform : nullptr;
        m_extMap.emplace(name, t);
        return t;
    }
    /// Drop cached node bindings (after renaming bones or editing clip node lists).
    void InvalidateBindings() { m_nodeMap.clear(); m_bind.clear(); m_pairs.clear(); m_rootCache.clear(); }

    /// The Transform each node track of clip `ci` drives (null = node missing).
    const std::vector<Transform*>& Bind(int ci) const {
        static const std::vector<Transform*> none;
        if (ci < 0 || ci >= (int)clips.size()) return none;
        NodeT(std::string());   // refresh the name map if the scene changed
        if (m_bind.size() != clips.size()) m_bind.assign(clips.size(), {});
        auto& b = m_bind[ci];
        if (b.size() != clips[ci].nodes.size()) {
            b.resize(clips[ci].nodes.size());
            for (std::size_t n = 0; n < b.size(); ++n) b[n] = NodeT(clips[ci].nodes[n].node);
        }
        return b;
    }

    /// Sample clip `ci` at time `t` onto every bound node.
    void ApplyPose(int ci, float t) {
        if (ci < 0 || ci >= (int)clips.size()) return;
        const auto& b = Bind(ci);
        for (std::size_t n = 0; n < b.size(); ++n) {
            Transform* tr = b[n];
            if (!tr) continue;
            Vec3 p = tr->localPosition, sc = tr->localScale; Quat r = tr->localRotation;
            SampleNode(ci, (int)n, t, p, r, sc);
            tr->localPosition = p; tr->localRotation = r; tr->localScale = sc;
        }
        ApplyShapeWeights(ci, t);
    }

    /// Blend shape weight tracks of clip `ci` at time `t` onto the nodes' BlendShapes.
    void ApplyShapeWeights(int ci, float t) {
        if (ci < 0 || ci >= (int)clips.size()) return;
        const auto& b = Bind(ci);
        for (std::size_t n = 0; n < b.size(); ++n) {
            const auto& ch = clips[ci].nodes[n].clip.Chan();
            if (ch.weights.empty() || !b[n] || !b[n]->gameObject) continue;
            auto apply = [&](BlendShapes* bs) {
                for (const auto& w : ch.weights) bs->SetWeight(w.first, w.second->Evaluate(t));
            };
            if (auto* bs = b[n]->gameObject->GetComponent<BlendShapes>()) { apply(bs); continue; }
            // A multi-material mesh is split into one child per material, each
            // with its own copy of the shapes: drive them all.
            for (Transform* c : b[n]->Children())
                if (c && c->gameObject)
                    if (auto* bs = c->gameObject->GetComponent<BlendShapes>()) apply(bs);
        }
    }

    /// Sample one node of a clip. The root-motion bone has its travel (and, with
    /// rootMotionRotation, its turning) removed when root motion or in-place is
    /// on: that part moves the object instead (or nothing, in place).
    void SampleNode(int ci, int n, float t, Vec3& p, Quat& r, Vec3& s) const {
        const NodeClip& nc = clips[ci].nodes[n];
        nc.clip.SampleTRS(t, p, r, s);
        if (!(rootMotion || inPlace) || n != RootIndex(ci)) return;
        Transform* bone = Bind(ci)[n];
        if (!bone) return;
        const bool stripY = (rootMotion && rootMotionY) ||
                            (inPlace && !rootMotion && !clips[ci].name.empty() &&
                             (clips[ci].name == jumpClip || clips[ci].name == fallClip));
        // Work in world axes so a rotated / scaled armature strips the right axes.
        Mat4 P = bone->Parent() ? bone->Parent()->LocalToWorldMatrix() : Mat4::Identity();
        Vec3 p0 = p; Quat r0dummy = r; Vec3 s0 = s;
        nc.clip.SampleTRS(0.0f, p0, r0dummy, s0, AnimationClip::kPosX | AnimationClip::kPosY | AnimationClip::kPosZ);
        Vec3 w = P.MultiplyVector(p - p0);
        Vec3 keep{0.0f, stripY ? 0.0f : w.y, 0.0f};
        p = p0 + P.Inverse().MultiplyVector(keep);
        if (rootMotion && rootMotionRotation) {
            Quat pr = bone->Parent() ? bone->Parent()->Rotation() : Quat::Identity;
            Quat q0 = r; Vec3 dp, ds;
            nc.clip.SampleTRS(0.0f, dp, q0, ds, AnimationClip::kRot);
            float dyaw = YawOf(pr * r) - YawOf(pr * q0);
            r = (pr.Conjugate() * Quat::AngleAxis(-dyaw, Vec3::Up) * pr * r).Normalized();
        }
    }

    /// How fast clip `ci` travels on the ground (root bone, world units per
    /// second at speed 1) — blend-tree thresholds from real clip speeds. 0 when
    /// the clip is in place or has no root bone.
    float ClipRootSpeed(int ci) const {
        if (ci < 0 || ci >= (int)clips.size()) return 0.0f;
        const float len = ClipLength(ci);
        Vec3 d; float yaw;
        if (len <= 1e-4f || !RootDelta(ci, 0.0f, len, len, d, yaw)) return 0.0f;
        return std::sqrt(d.x * d.x + d.z * d.z) / len;
    }

    /// World-space root travel (ground plane unless rootMotionY) and yaw change
    /// of clip `ci` between clip times e0 and e1 (e1 < e0 = wrapped a loop).
    bool RootDelta(int ci, float e0, float e1, float len, Vec3& outD, float& outYaw) const {
        outD = Vec3{0, 0, 0}; outYaw = 0.0f;
        int idx = RootIndex(ci);
        if (idx < 0 || len <= 0.0f) return false;
        Transform* bone = Bind(ci)[idx];
        const AnimationClip& c = clips[ci].nodes[idx].clip;
        const float cap = len - std::fmax(1e-5f, len * 1e-6f);
        auto at = [&](float tm, Vec3& p, Quat& q) {
            if (tm > cap) tm = cap;
            Vec3 s; p = Vec3{0, 0, 0}; q = Quat::Identity;
            c.SampleTRS(tm, p, q, s, AnimationClip::kPosX | AnimationClip::kPosY | AnimationClip::kPosZ | AnimationClip::kRot);
        };
        Vec3 pa, pb; Quat qa, qb; Vec3 dLocal; float dyaw = 0.0f;
        Quat pr = (bone && bone->Parent()) ? bone->Parent()->Rotation() : Quat::Identity;
        auto yawSpan = [&](float a, float b) {
            Vec3 x, y; Quat u, v; at(a, x, u); at(b, y, v);
            float d = YawOf(pr * v) - YawOf(pr * u);
            while (d > 180.0f) d -= 360.0f;
            while (d < -180.0f) d += 360.0f;
            return d;
        };
        if (e1 < e0) {
            at(e0, pa, qa); at(len, pb, qb); dLocal = pb - pa;
            at(0.0f, pa, qa); at(e1, pb, qb); dLocal = dLocal + (pb - pa);
            if (rootMotionRotation) dyaw = yawSpan(e0, len) + yawSpan(0.0f, e1);
        } else {
            at(e0, pa, qa); at(e1, pb, qb); dLocal = pb - pa;
            if (rootMotionRotation) dyaw = yawSpan(e0, e1);
        }
        Mat4 P = (bone && bone->Parent()) ? bone->Parent()->LocalToWorldMatrix() : Mat4::Identity();
        Vec3 w = P.MultiplyVector(dLocal);
        if (!rootMotionY) w.y = 0.0f;
        outD = w; outYaw = dyaw;
        return true;
    }

    /// Move (and turn) the root-motion target by a world delta.
    void ApplyRootMotion(const Vec3& d, float yaw, float dt) {
        m_lastRootDelta = d; m_lastRootYaw = yaw;
        if (onRootMotion) { onRootMotion(d, yaw); return; }
        Transform* tgt = transform;
        if (rootMotionApplyTo == 1 && transform && transform->Parent()) tgt = transform->Parent();
        if (!tgt) return;
        Rigidbody3D* rb = tgt->gameObject ? tgt->gameObject->GetComponent<Rigidbody3D>() : nullptr;
        if (rb && dt > 0.0f) {
            rb->velocity.x = d.x / dt; rb->velocity.z = d.z / dt;   // Y stays with gravity / jumps
            if (rootMotionY) rb->velocity.y = d.y / dt;
        } else if (d.x != 0.0f || d.y != 0.0f || d.z != 0.0f) {
            tgt->SetPosition(tgt->Position() + d);
        }
        if (yaw != 0.0f) tgt->SetRotation((Quat::AngleAxis(yaw, Vec3::Up) * tgt->Rotation()).Normalized());
    }

    /// Heading (degrees about world up) of a rotation's forward axis.
    static float YawOf(const Quat& q) {
        Vec3 f = q * Vec3::Forward;
        if (f.x * f.x + f.z * f.z < 1e-8f) f = q * Vec3::Up;   // forward points straight up/down
        return std::atan2(f.x, f.z) * 57.2957795f;
    }

    /// Scenes saved by older builds carried an Animator on every bone holding a
    /// copy of the clip; those would fight the pose — switch them off.
    void DisableLegacyBoneAnimators(int ci) {
        const auto& b = Bind(ci);
        for (Transform* t : b) {
            if (!t || !t->gameObject) continue;
            Animator* an = t->gameObject->GetComponent<Animator>();
            if (!an || !an->playing) continue;
            for (const Clip& c : clips) if (c.name == an->clip.name || an->clip.name.empty()) { an->playing = false; break; }
        }
    }

    /// Every node track spans the whole clip: a bone whose keys end early holds
    /// its last value instead of looping on its own (per-node clips used to loop
    /// at their own length, desyncing limbs after the first cycle).
    static void NormalizeClip(Clip& c) {
        if (c.normalized) return;
        float len = 0.0f;
        for (const auto& nc : c.nodes) len = std::fmax(len, nc.clip.Length());
        for (auto& nc : c.nodes) nc.clip.SpanTracks(len);
        c.normalized = true;
    }

private:
    struct BlendFrom { std::string node; Transform* t; Vec3 pos; Quat rot; Vec3 scl; };
    /// Node pairing between two clips for BlendClips: for each node of A, its
    /// index in B (-1 = none); and B's nodes that A doesn't have.
    struct PairMap { int a = -1, b = -1; std::vector<int> aToB, onlyB; };
    const PairMap& Pair(int A, int B) const {
        for (const PairMap& p : m_pairs)
            if (p.a == A && p.b == B && p.aToB.size() == clips[A].nodes.size()) return p;
        PairMap pm; pm.a = A; pm.b = B;
        std::unordered_map<std::string, int> inB;
        for (std::size_t n = 0; n < clips[B].nodes.size(); ++n) inB.emplace(clips[B].nodes[n].node, (int)n);
        std::vector<char> usedB(clips[B].nodes.size(), 0);
        for (const NodeClip& nc : clips[A].nodes) {
            auto it = inB.find(nc.node);
            int ib = it == inB.end() ? -1 : it->second;
            pm.aToB.push_back(ib);
            if (ib >= 0) usedB[ib] = 1;
        }
        for (std::size_t n = 0; n < usedB.size(); ++n) if (!usedB[n]) pm.onlyB.push_back((int)n);
        if (m_pairs.size() > 8) m_pairs.erase(m_pairs.begin());
        m_pairs.push_back(std::move(pm));
        return m_pairs.back();
    }
    mutable std::unordered_map<std::string, Transform*> m_nodeMap, m_extMap;
    mutable std::uint64_t m_nodeVer = 0;
    mutable std::vector<std::vector<Transform*>> m_bind;
    mutable std::vector<PairMap> m_pairs;
    mutable std::vector<int> m_rootCache;
    mutable std::string m_rootCacheNode;
    std::uint64_t m_blendVer = 0;
    Vec3  m_lastRootDelta{0, 0, 0};
    float m_lastRootYaw = 0.0f;
    struct PostPose { const void* owner; std::function<void(float)> fn; };
    std::vector<PostPose> m_postPose;
    // Per-frame scratch for BlendMany / PoseWeighted (reused, not reallocated).
    struct PwAcc { Transform* t; Vec3 p{0, 0, 0}, s{0, 0, 0}; Quat q{0, 0, 0, 0}; Quat ref{0, 0, 0, 1}; float w = 0.0f; };
    std::vector<int> m_bmClip;
    std::vector<float> m_bmW, m_bmLen;
    std::vector<std::pair<int, float>> m_bmCT;
    std::vector<PwAcc> m_pwAcc;
    std::unordered_map<Transform*, int> m_pwAt;
    std::vector<BlendFrom>   m_blend;         ///< pose at the last clip switch (crossfade source)
    float                    m_blendW = 1.0f; ///< crossfade progress (1 = done)
    std::vector<std::string> m_firedEvents;   ///< event names fired since the last consume
    float                    m_clock = 0.0f;  ///< playback clock for event firing
    float                    m_phase = 0.0f;  ///< shared normalized cycle for smooth locomotion
    bool                     m_once = false;        ///< current playback is a one-shot
    bool                     m_held = false;        ///< a script holds the current clip (PlayHeld)
    int                      m_heldReturnTo = -1;   ///< clip to return to after the hold
    std::unordered_set<Transform*> m_mask;   ///< PoseLayer's mask bone subtree
    std::uint64_t            m_maskVer = 0;
    std::string              m_maskName;
    const Component*         m_stateMachine = nullptr;   ///< registered AnimStateMachine (see StateMachineDrives)
    float                    m_fadeTime = 0.25f;    ///< the running crossfade's length (blendTime, or a transition's blend)
    bool                     m_fromStart = true;    ///< next event window starts at a Play: an event at 0 fires too
    // Fire clip `c`'s events with time in (a, b], or [a, b] when `fromStart`.
    void FireWindow(int c, float a, float b, bool fromStart) {
        for (const ClipEvent& ev : clips[(std::size_t)c].events)
            if ((fromStart ? ev.time >= a : ev.time > a) && ev.time <= b && !ev.name.empty()) FireEvent(ev.name);
    }
    int                      m_onceReturnTo = -1;   ///< clip to return to after the one-shot
    Vec3 m_lastPos{0, 0, 0};
    float m_lastDt = 0.0f;   // the frame m_lastPos was measured in
    float m_lastSpd = 0.0f, m_lastVy = 0.0f;   // last frame's measured speeds
    bool m_spikeHeld = false;                  // a one-frame speed spike is waiting for the next frame to confirm it
    float m_blendSpd = 0.0f;                   // the speed the locomotion blend follows (eased)
    bool m_haveLast = false;
    bool  m_inAir = false;   // air-state machine (jump/fall/land by vertical motion)
    float m_airT  = 0.0f;    // hysteresis timer for takeoff/touchdown detection
    bool  m_fell  = false;   // descended since leaving the ground (past any jump apex)
    int   m_stance = 0;      // the stance whose clips play (see BodyStance)
    float m_stanceFade = -1.0f;   // a stance change's crossfade, for the next switch (< 0 = none)

    // Something solid just under this object's origin (the feet), other than
    // the model itself or the bodies carrying it (the player's capsule
    // contains the ray's start, wherever the player sits in the hierarchy).
    bool GroundUnderFeet() const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || !transform) return false;
        Vec3 p = transform->Position();
        for (const RaycastHit3D& h : sc->physics3D().RaycastAll(*sc, {p.x, p.y + 0.1f, p.z}, Vec3{0, -1, 0}, 0.25f,
                                                                 nullptr, Physics3D::kAllLayers, false)) {
            if (!h.gameObject || h.gameObject->IsSelfOrDescendantOf(gameObject)) continue;
            bool carrier = false;
            for (Transform* t = transform->Parent(); t && !carrier; t = t->Parent()) carrier = t->gameObject == h.gameObject;
            if (!carrier) return true;
        }
        return false;
    }
};

} // namespace okay
