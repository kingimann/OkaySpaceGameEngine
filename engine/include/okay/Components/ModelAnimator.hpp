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
#include <string>
#include <vector>
#include <cmath>

namespace okay {

class ModelAnimator : public Behaviour {
public:
    struct NodeClip { std::string node; AnimationClip clip; };  ///< one node's tracks for a clip
    struct Clip     { std::string name; std::vector<NodeClip> nodes; };

    std::vector<Clip> clips;
    int   active   = 0;      ///< index of the clip to play
    bool  autoPlay = true;   ///< play `active` on Start
    float speed    = 1.0f;
    bool  loop     = true;

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
    void PlayIndex(int i) {
        if (i < 0 || i >= (int)clips.size() || !gameObject) return;
        active = i;
        Scene* sc = gameObject->scene();
        if (!sc) return;
        for (NodeClip& nc : clips[i].nodes) {
            GameObject* g = sc->Find(nc.node);
            if (!g) continue;
            Animator* an = g->GetComponent<Animator>();
            if (!an) an = g->AddComponent<Animator>();
            nc.clip.loop = loop;
            an->clip = nc.clip;
            an->speed = speed;
            an->playing = true;
            an->Restart();
        }
    }

private:
    Vec3 m_lastPos{0, 0, 0};
    bool m_haveLast = false;
};

} // namespace okay
