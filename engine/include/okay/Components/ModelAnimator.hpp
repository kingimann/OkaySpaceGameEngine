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
                if (loop && t1 > len) { fire(t0, len); fire(0.0f, std::fmod(t1, len)); }
                else                  fire(t0, t1);
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
            m_clock = len > 0.0f ? (loop ? std::fmod(t1, len) : std::fmin(t1, len)) : t1;
        }

        if (!driveByMovement || dt <= 0.0f || !transform) return;
        Vec3 p = transform->Position();
        if (!m_haveLast) { m_lastPos = p; m_haveLast = true; return; }
        float dx = p.x - m_lastPos.x, dz = p.z - m_lastPos.z;
        m_lastPos = p;
        float spd = std::sqrt(dx*dx + dz*dz) / dt;
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

    /// Switch to a clip by index: push each node's tracks onto that node's Animator.
    /// With blendTime > 0 the current pose is captured first and eased into the new
    /// clip over that many seconds (see LateUpdate).
    void PlayIndex(int i) {
        if (i < 0 || i >= (int)clips.size() || !gameObject) return;
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
        const NodeClip* rootNC = rootMotion ? RootMotionClip() : nullptr;
        for (NodeClip& nc : clips[i].nodes) {
            GameObject* g = sc->Find(nc.node);
            if (!g) continue;
            Animator* an = g->GetComponent<Animator>();
            if (!an) an = g->AddComponent<Animator>();
            nc.clip.loop = loop;
            an->clip = nc.clip;
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
    Vec3 m_lastPos{0, 0, 0};
    bool m_haveLast = false;
};

} // namespace okay
