#pragma once
// ---------------------------------------------------------------------------
// ModelAnimator — a named CLIP LIBRARY + switcher for an imported model. A glTF can
// carry several animations (idle / walk / run); this holds them all and plays one at a
// time across the model's nodes. Lives on the import root.
//
// Each clip is a set of (nodeName -> AnimationClip) tracks. Play(name) pushes those
// clips onto an Animator on each target node (creating one if needed), so the tested
// Animator does the actual driving and switching is just "load a different clip". The
// library serializes, so a saved model keeps all its animations.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Scene/Transform.hpp"
#include <functional>
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
    struct Clip      { std::string name; std::vector<NodeClip> nodes; std::vector<ClipEvent> events; };

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
    std::string rootMotionNode;   ///< bone to read ("" = auto: first node with position tracks)
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

    void Start() override {
        if (transform) { m_lastPos = transform->Position(); m_haveLast = true; }
        if (autoPlay && !clips.empty()) PlayIndex(active);
    }

    void Update(float dt) override {
        // Locomotion speed (world XZ units/sec), measured from how the object moved.
        float spd = -1.0f;
        if (driveByMovement && dt > 0.0f && transform) {
            Vec3 p = transform->Position();
            if (m_haveLast) {
                float dx = p.x - m_lastPos.x, dz = p.z - m_lastPos.z;
                spd = std::sqrt(dx*dx + dz*dz) / dt;
            }
            m_lastPos = p; m_haveLast = true;
        }

        // Continuous 1D blend (idle<->walk<->run by speed): drives the bones
        // directly and replaces the discrete machinery below while it's active.
        // A one-shot (attack etc.) suspends locomotion until it finishes.
        if (spd >= 0.0f && smoothLocomotion && !m_once && SmoothBlendStep(dt, spd)) return;

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
            // Root motion: the ground translation the root bone would have made
            // this frame becomes movement of the whole object (world space, so it
            // respects the model's rotation and import scale).
            if (rootMotion && transform && len > 0.0f) {
                if (const NodeClip* rn = RootMotionClip()) {
                    auto posAt = [&](float tm) {
                        // Sample just INSIDE the end: looping curves wrap t==len back
                        // to t==0, which would cancel each loop's translation in the
                        // wrap delta below.
                        float cap = len - std::fmax(1e-5f, len * 1e-6f);
                        if (tm > cap) tm = cap;
                        bool f; Vec3 p{0, 0, 0};
                        float v = rn->clip.Evaluate("position.x", tm, f); if (f) p.x = v;
                        v = rn->clip.Evaluate("position.z", tm, f);       if (f) p.z = v;
                        return p;
                    };
                    float e0 = loop ? std::fmod(std::fmod(t0, len) + len, len) : std::fmin(t0, len);
                    float e1 = loop ? std::fmod(std::fmod(t1, len) + len, len) : std::fmin(t1, len);
                    Vec3 d;
                    if (loop && e1 < e0)   // wrapped: end-of-clip stretch + start stretch
                        d = (posAt(len) - posAt(e0)) + (posAt(e1) - posAt(0.0f));
                    else
                        d = posAt(e1) - posAt(e0);
                    if (d.x != 0.0f || d.z != 0.0f) {
                        Vec3 wd = transform->LocalToWorldMatrix().MultiplyVector(d);
                        transform->SetPosition(transform->Position() + wd);
                    }
                }
            }
            m_clock = len > 0.0f ? ((loop && !m_once) ? std::fmod(t1, len) : std::fmin(t1, len)) : t1;
            // A finished one-shot returns to the clip that was playing before it
            // (crossfaded like any switch).
            if (m_once && len > 0.0f && m_clock >= len) {
                m_once = false;
                int rt = m_onceReturnTo; m_onceReturnTo = -1;
                if (rt >= 0 && rt < (int)clips.size() && rt != active) PlayIndex(rt);
            }
        }

        // Discrete locomotion: switch to the clip for this speed band (crossfaded
        // by blendTime, since Play goes through PlayIndex). Suspended by a one-shot.
        if (spd < 0.0f || m_once) return;
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
        for (const BlendFrom& s : m_blend) {
            GameObject* g = sc->Find(s.node);
            if (!g || !g->transform) continue;
            Transform* t = g->transform;
            t->localPosition = s.pos + (t->localPosition - s.pos) * w;
            t->localScale    = s.scl + (t->localScale    - s.scl) * w;
            Quat b = t->localRotation;
            float d = s.rot.x*b.x + s.rot.y*b.y + s.rot.z*b.z + s.rot.w*b.w;
            if (d < 0.0f) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
            Quat r{s.rot.x + (b.x - s.rot.x) * w, s.rot.y + (b.y - s.rot.y) * w,
                   s.rot.z + (b.z - s.rot.z) * w, s.rot.w + (b.w - s.rot.w) * w};
            t->localRotation = r.Normalized();
        }
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
        // Capture the CURRENT pose of every node the new clip animates, by name
        // (names survive object churn; a missing node just stops blending).
        m_blend.clear(); m_blendW = 0.0f;
        if (blendTime > 0.001f) {
            for (const NodeClip& nc : clips[i].nodes)
                if (GameObject* g = sc->Find(nc.node); g && g->transform)
                    m_blend.push_back({nc.node, g->transform->localPosition,
                                       g->transform->localRotation, g->transform->localScale});
        }
        m_clock = 0.0f;   // the event clock restarts with the node Animators
        const NodeClip* rootNC = (rootMotion || inPlace) ? RootMotionClip() : nullptr;
        for (NodeClip& nc : clips[i].nodes) {
            GameObject* g = sc->Find(nc.node);
            if (!g) continue;
            Animator* an = g->GetComponent<Animator>();
            if (!an) an = g->AddComponent<Animator>();
            nc.clip.loop = loop;
            an->clip = nc.clip;
            if (once) an->clip.loop = false;   // a one-shot holds its final pose
            // Root motion: the object moves instead of the bone — strip the bone's
            // ground translation from what the Animator plays (Y bounce stays).
            if (rootNC == &nc) {
                an->clip.RemoveTrack("position.x");
                an->clip.RemoveTrack("position.z");
            }
            an->speed = speed;
            an->playing = true;
            an->Restart();
        }
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
        bool f; float v;
        v = c.Evaluate("position.x", t, f); if (f) pos.x = v;
        v = c.Evaluate("position.y", t, f); if (f) pos.y = v;
        v = c.Evaluate("position.z", t, f); if (f) pos.z = v;
        v = c.Evaluate("scale.x", t, f);    if (f) scl.x = v;
        v = c.Evaluate("scale.y", t, f);    if (f) scl.y = v;
        v = c.Evaluate("scale.z", t, f);    if (f) scl.z = v;
        bool qx, qy, qz, qw;
        float vqx = c.Evaluate("rotation.qx", t, qx);
        float vqy = c.Evaluate("rotation.qy", t, qy);
        float vqz = c.Evaluate("rotation.qz", t, qz);
        float vqw = c.Evaluate("rotation.qw", t, qw);
        if (qx || qy || qz || qw) {
            float ln = std::sqrt(vqx*vqx + vqy*vqy + vqz*vqz + vqw*vqw);
            if (ln < 1e-8f) { vqw = 1.0f; ln = 1.0f; }
            rot = Quat{vqx/ln, vqy/ln, vqz/ln, vqw/ln};
        } else {
            bool fx, fy, fz;
            float rx = c.Evaluate("rotation.x", t, fx);
            float ry = c.Evaluate("rotation.y", t, fy);
            float rz = c.Evaluate("rotation.z", t, fz);
            if (fx || fy || fz) rot = Quat::Euler(fx ? rx : 0.0f, fy ? ry : 0.0f, fz ? rz : 0.0f);
        }
    }

    // ---- Clip surgery (the editor's animation tools) ----
    /// Reverse a clip in time (plays backwards); event times flip with it.
    void ReverseClip(int i) {
        if (i < 0 || i >= (int)clips.size()) return;
        float len = ClipLength(i);
        for (auto& nc : clips[i].nodes) nc.clip.Reverse();
        for (auto& ev : clips[i].events) ev.time = len - ev.time;
    }
    /// Cut a clip down to [t0, t1] (boundaries sampled exactly); events outside
    /// the window drop, the rest shift to the new zero.
    void TrimClip(int i, float t0, float t1) {
        if (i < 0 || i >= (int)clips.size() || t1 <= t0) return;
        for (auto& nc : clips[i].nodes) nc.clip.Trim(t0, t1);
        std::vector<ClipEvent> kept;
        for (auto& ev : clips[i].events)
            if (ev.time >= t0 && ev.time <= t1) kept.push_back({ev.time - t0, ev.name});
        clips[i].events = std::move(kept);
    }
    /// Stretch/compress a clip in time (2 = twice as long / half speed).
    void ScaleClipTime(int i, float factor) {
        if (i < 0 || i >= (int)clips.size() || factor <= 1e-4f) return;
        for (auto& nc : clips[i].nodes) nc.clip.ScaleTime(factor);
        for (auto& ev : clips[i].events) ev.time *= factor;
    }
    /// Duplicate a clip (returns the new index, -1 on failure) — trim the copy
    /// to cut a long take into pieces without losing the original.
    int DuplicateClip(int i) {
        if (i < 0 || i >= (int)clips.size()) return -1;
        Clip copy = clips[i];
        copy.name += " copy";
        clips.push_back(std::move(copy));
        return (int)clips.size() - 1;
    }
    /// Remove a clip entirely (locomotion mappings by that name go stale — the
    /// editor clears them).
    void RemoveClip(int i) {
        if (i < 0 || i >= (int)clips.size()) return;
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
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return false;
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

        // Root-motion source bone of each blended clip (fetched once; also pins
        // that bone's X/Z during the drive pass below).
        const NodeClip* rmA = nullptr;
        const NodeClip* rmB = nullptr;
        if (rootMotion || inPlace) {
            int keep = active;
            active = A; rmA = RootMotionClip();
            active = B; rmB = RootMotionClip();
            active = keep;
        }
        // Root motion: blend both clips' ground deltas by the same weight.
        if (rootMotion && transform) {
            Vec3 d{0, 0, 0};
            if (rmA) d = d + GroundDelta(*rmA, prevPhase * lenA, tA, lenA) * (1.0f - w);
            if (rmB) d = d + GroundDelta(*rmB, prevPhase * lenB, tB, lenB) * w;
            if (d.x != 0.0f || d.z != 0.0f)
                transform->SetPosition(transform->Position() +
                                       transform->LocalToWorldMatrix().MultiplyVector(d));
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

        // Drive the union of both clips' nodes directly (their Animators pause so
        // nothing fights the blend). Nodes in only one clip use it unblended.
        auto findIn = [](const Clip& c, const std::string& n) -> const NodeClip* {
            for (const auto& nc : c.nodes) if (nc.node == n) return &nc;
            return nullptr;
        };
        auto drive = [&](const std::string& name, const NodeClip* a, const NodeClip* b) {
            GameObject* g = sc->Find(name);
            if (!g || !g->transform) return;
            if (Animator* an = g->GetComponent<Animator>()) an->playing = false;
            Transform* t = g->transform;
            Vec3 pA = t->localPosition, sA = t->localScale; Quat rA = t->localRotation;
            Vec3 pB = pA, sB = sA; Quat rB = rA;
            if (a) EvalClipInto(a->clip, tA, pA, rA, sA);
            if (b) EvalClipInto(b->clip, tB, pB, rB, sB);
            float bw = (a && b) ? w : (b ? 1.0f : 0.0f);
            Vec3 p = pA + (pB - pA) * bw;
            Vec3 s = sA + (sB - sA) * bw;
            float dq = rA.x*rB.x + rA.y*rB.y + rA.z*rB.z + rA.w*rB.w;
            if (dq < 0.0f) { rB.x = -rB.x; rB.y = -rB.y; rB.z = -rB.z; rB.w = -rB.w; }
            Quat r{rA.x + (rB.x - rA.x) * bw, rA.y + (rB.y - rA.y) * bw,
                   rA.z + (rB.z - rA.z) * bw, rA.w + (rB.w - rA.w) * bw};
            if ((rootMotion || inPlace) && ((a && a == rmA) || (b && b == rmB))) { p.x = t->localPosition.x; p.z = t->localPosition.z; }
            t->localPosition = p;
            t->localScale    = s;
            t->localRotation = r.Normalized();
        };
        for (const auto& nc : clips[A].nodes) drive(nc.node, &nc, findIn(clips[B], nc.node));
        if (A != B)
            for (const auto& nc : clips[B].nodes)
                if (!findIn(clips[A], nc.node)) drive(nc.node, nullptr, &nc);
        return true;
    }

    /// The active clip's root-motion source: the named node, else the first node
    /// with a ground-translation track. Null when the clip has none.
    const NodeClip* RootMotionClip() const {
        if (active < 0 || active >= (int)clips.size()) return nullptr;
        const NodeClip* first = nullptr;
        for (const NodeClip& nc : clips[active].nodes) {
            bool hasPos = nc.clip.HasTrack("position.x") || nc.clip.HasTrack("position.z");
            if (!hasPos) continue;
            if (!rootMotionNode.empty()) { if (nc.node == rootMotionNode) return &nc; }
            else if (!first) first = &nc;
        }
        return rootMotionNode.empty() ? first : nullptr;
    }

private:
    struct BlendFrom { std::string node; Vec3 pos; Quat rot; Vec3 scl; };
    std::vector<BlendFrom>   m_blend;         ///< pose at the last clip switch (crossfade source)
    float                    m_blendW = 1.0f; ///< crossfade progress (1 = done)
    std::vector<std::string> m_firedEvents;   ///< event names fired since the last consume
    float                    m_clock = 0.0f;  ///< playback clock for event firing
    float                    m_phase = 0.0f;  ///< shared normalized cycle for smooth locomotion
    bool                     m_once = false;        ///< current playback is a one-shot
    int                      m_onceReturnTo = -1;   ///< clip to return to after the one-shot
    Vec3 m_lastPos{0, 0, 0};
    bool m_haveLast = false;
};

} // namespace okay
