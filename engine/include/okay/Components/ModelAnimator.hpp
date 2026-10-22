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
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <cmath>

namespace okay {

class ModelAnimator : public Behaviour {
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
    /// blended per bone every frame — a Unity 1D blend tree.
    bool smoothLocomotion = false;

    /// Push callback for fired clip events; or poll with Consume/NextAnimEvent.
    std::function<void(const std::string&)> onAnimEvent;   // not serialized
    std::vector<std::string> ConsumeAnimEvents() { auto q = std::move(m_firedEvents); m_firedEvents.clear(); return q; }
    std::string NextAnimEvent() {
        if (m_firedEvents.empty()) return {};
        std::string n = m_firedEvents.front();
        m_firedEvents.erase(m_firedEvents.begin());
        return n;
    }

    /// Length (seconds) of a clip = its longest node track.
    float ClipLength(int i) const {
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

    void Start() override {
        if (transform) { m_lastPos = transform->Position(); m_haveLast = true; }
        if (autoPlay && !clips.empty()) PlayIndex(active);
    }

    void Update(float dt) override {
        if (externalDrive) return;   // a state machine blend tree is posing the model
        // Locomotion speed (world XZ units/sec) + vertical speed, measured from
        // how the object moved.
        float spd = -1.0f, vy = 0.0f;
        if (driveByMovement && dt > 0.0f && transform) {
            Vec3 p = transform->Position();
            if (m_haveLast) {
                float dx = p.x - m_lastPos.x, dz = p.z - m_lastPos.z;
                spd = std::sqrt(dx*dx + dz*dz) / dt;
                vy = (p.y - m_lastPos.y) / dt;
            }
            m_lastPos = p; m_haveLast = true;
        }

        // Air states: launch -> jump clip, past the apex -> fall clip, touchdown
        // -> land one-shot; locomotion stands down while airborne. Driven purely
        // by the capsule's vertical motion, so every controller "just works".
        if (spd >= 0.0f && (!jumpClip.empty() || !fallClip.empty())) {
            if (!m_inAir) {
                bool launch = vy > airUpVel;
                if (vy < -airDownVel) m_airT += dt; else m_airT = 0.0f;
                if (launch || m_airT > 0.12f) {          // brief drops (steps) don't count
                    m_inAir = true; m_airT = 0.0f;
                    const std::string& c = (launch && !jumpClip.empty()) ? jumpClip : fallClip;
                    if (!c.empty()) Play(c);
                }
            } else {
                // Apex: a jump turns into a fall once we're heading down.
                if (vy < -1.0f && !fallClip.empty() && CurrentName() == jumpClip) Play(fallClip);
                // Touchdown: vertical motion dies out (the ground stops us).
                if (std::fabs(vy) < 0.4f) m_airT += dt; else m_airT = 0.0f;
                if (m_airT > 0.06f) {
                    m_inAir = false; m_airT = 0.0f;
                    if (!landClip.empty()) PlayOnce(landClip);   // locomotion resumes after
                }
            }
        }

        // Continuous 1D blend (idle<->walk<->run by speed): drives the bones
        // directly and replaces the discrete machinery below while it's active.
        // A one-shot (attack etc.) or an air state suspends locomotion.
        if (spd >= 0.0f && smoothLocomotion && !m_once && !m_inAir && SmoothBlendStep(dt, spd)) return;

        // Fire clip events crossed by this frame's playback window. The clock
        // mirrors the node Animators' time (reset together in PlayIndex).
        if (dt > 0.0f && active >= 0 && active < (int)clips.size()) {
            float len = ClipLength(active);
            float t0 = m_clock, t1 = m_clock + dt * speed;
            if (!clips[active].events.empty() && len > 0.0f) {
                auto fire = [&](float a, float b) {
                    for (const ClipEvent& ev : clips[active].events)
                        if (ev.time > a && ev.time <= b && !ev.name.empty()) {
                            m_firedEvents.push_back(ev.name);
                            if (onAnimEvent) onAnimEvent(ev.name);
                        }
                };
                if (loop && !m_once && t1 > len) { fire(t0, len); fire(0.0f, std::fmod(t1, len)); }
                else                             fire(t0, t1);
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
        const std::string* want = &idleClip;
        if (spd >= runThreshold && !runClip.empty())        want = &runClip;
        else if (spd > walkThreshold && !walkClip.empty())  want = &walkClip;
        if (!want->empty() && *want != CurrentName()) Play(*want);   // switch only on change
    }

    /// Crossfade: eases the pose captured at the moment of a clip switch into the
    /// new clip's pose. Runs in LateUpdate so it blends what the node Animators
    /// just wrote this frame (Update order between objects doesn't matter).
    void LateUpdate(float dt) override {
        if (m_blend.empty() || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc) { m_blend.clear(); return; }
        m_blendW = blendTime > 0.001f ? std::fmin(1.0f, m_blendW + dt / blendTime) : 1.0f;
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

    /// Switch to a clip by name; returns false if there's no such clip.
    bool Play(const std::string& name) {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].name == name) { PlayIndex(i); return true; }
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

    /// True while a PlayOnce one-shot is in flight (HumanoidRetarget yields to
    /// the model's own clip until it finishes).
    bool OneShotActive() const { return m_once; }

    /// True when a non-looping playback (a one-shot, or loop=false) has reached
    /// the end of its clip.
    bool ClipFinished() const {
        if (active < 0 || active >= (int)clips.size()) return false;
        if (!m_once && loop) return false;
        float len = ClipLength(active);
        return len > 0.0f && m_clock >= len;
    }

    /// Playback time (seconds) into the current clip.
    float Time() const { return m_clock; }
    /// Seek the current clip to `t` seconds and pose the model there.
    void SetTime(float t) {
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
    void PlayIndex(int i, bool once = false) {
        if (i < 0 || i >= (int)clips.size() || !gameObject) return;
        m_once = once;
        active = i;
        Scene* sc = gameObject->scene();
        if (!sc) return;
        // Capture the CURRENT pose of every node the new clip animates (a missing
        // node just stops blending).
        m_blend.clear(); m_blendW = 0.0f;
        m_blendVer = sc->StructureVersion();
        if (blendTime > 0.001f) {
            const auto& b = Bind(i);
            for (std::size_t n = 0; n < b.size(); ++n)
                if (Transform* t = b[n])
                    m_blend.push_back({clips[i].nodes[n].node, t, t->localPosition, t->localRotation, t->localScale});
        }
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
    int FindClip(const std::string& name) const {
        if (name.empty()) return -1;
        for (int i = 0; i < (int)clips.size(); ++i) if (clips[i].name == name) return i;
        return -1;
    }

    /// One frame of continuous locomotion blending. Returns false when it can't
    /// run (no scene / no usable clips) so the caller falls back to discrete mode.
    bool SmoothBlendStep(float dt, float spd) {
        if (!gameObject || !gameObject->scene()) return false;
        // The two clips around this speed + the blend weight between them.
        int A, B; float w;
        if (spd <= walkThreshold) {
            A = FindClip(idleClip); B = FindClip(walkClip);
            w = walkThreshold > 1e-4f ? spd / walkThreshold : 1.0f;
        } else {
            A = FindClip(walkClip); B = FindClip(runClip);
            w = (spd - walkThreshold) / std::fmax(runThreshold - walkThreshold, 1e-4f);
        }
        if (w < 0.0f) w = 0.0f; if (w > 1.0f) w = 1.0f;
        return BlendClips(dt, A, B, w);
    }

    /// Set by a driver (e.g. an AnimStateMachine blend-tree state) that poses the
    /// model itself through BlendClips each frame: Update then skips its own clip
    /// logic so the two don't fight. Crossfades (LateUpdate) still apply.
    bool externalDrive = false;

    /// Pose the model as a weighted mix of clips A and B (w = 0 -> A, 1 -> B),
    /// both sampled at one shared normalized phase so footfalls line up. Drives
    /// the bones directly (their Animators pause), blends root motion and fires
    /// the dominant clip's events. Returns false if neither clip exists.
    bool BlendClips(float dt, int A, int B, float w) {
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
            const Clip& dom = clips[active];
            float len = active == A ? lenA : lenB;
            float e0 = prevPhase * len, e1 = m_phase * len;
            auto fire = [&](float a2, float b2) {
                for (const ClipEvent& ev : dom.events)
                    if (ev.time > a2 && ev.time <= b2 && !ev.name.empty()) {
                        m_firedEvents.push_back(ev.name);
                        if (onAnimEvent) onAnimEvent(ev.name);
                    }
            };
            if (e1 < e0) { fire(e0, len); fire(0.0f, e1); }
            else fire(e0, e1);
        }

        // Drive the union of both clips' nodes directly. Nodes in only one clip
        // use it unblended. The A->B node pairing is cached per clip pair.
        const PairMap& pm = Pair(A, B);
        const auto& bA = Bind(A);
        const auto& bB = Bind(B);
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
        return true;
    }
    /// Normalized phase (0..1) of the last BlendClips call.
    float BlendPhase() const { return m_phase; }
    void  ResetBlendPhase(float p = 0.0f) { m_phase = p; }
    /// Capture the current pose of every node clips A and B animate, so the next
    /// `seconds` ease from it (the same crossfade Play uses) — call when switching
    /// into a BlendClips-driven state.
    void CaptureCrossfade(int A, int B, float seconds) {
        m_blend.clear(); m_blendW = 0.0f;
        blendTime = seconds;
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
    std::vector<BlendFrom>   m_blend;         ///< pose at the last clip switch (crossfade source)
    float                    m_blendW = 1.0f; ///< crossfade progress (1 = done)
    std::vector<std::string> m_firedEvents;   ///< event names fired since the last consume
    float                    m_clock = 0.0f;  ///< playback clock for event firing
    float                    m_phase = 0.0f;  ///< shared normalized cycle for smooth locomotion
    bool                     m_once = false;        ///< current playback is a one-shot
    int                      m_onceReturnTo = -1;   ///< clip to return to after the one-shot
    Vec3 m_lastPos{0, 0, 0};
    bool m_haveLast = false;
    bool  m_inAir = false;   // air-state machine (jump/fall/land by vertical motion)
    float m_airT  = 0.0f;    // hysteresis timer for takeoff/touchdown detection
};

} // namespace okay
