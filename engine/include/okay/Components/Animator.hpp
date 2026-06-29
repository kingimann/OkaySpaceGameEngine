#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Animation/AnimationClip.hpp"

namespace okay {

/// Plays an AnimationClip and drives its GameObject's Transform from the clip's
/// tracks ("position.x/y/z", "rotation.z", "scale.x/y/z"). A lightweight
/// counterpart to Unity's Animator.
class Animator : public Behaviour {
public:
    AnimationClip clip;
    float speed   = 1.0f;
    bool  playing = true;

    float Time() const { return m_time; }
    void  SetTime(float t) { m_time = t; ApplyAt(t); }
    void  Play()  { playing = true; }
    void  Pause() { playing = false; }
    void  Restart() { m_time = 0.0f; ApplyAt(0.0f); }

    void Update(float dt) override {
        if (!playing) return;
        m_time += dt * speed;
        ApplyAt(m_time);
    }

private:
    void ApplyAt(float time) {
        if (!transform) return;
        float len = clip.Length();
        float t = time;
        if (clip.loop && len > 0.0f) t = std::fmod(std::fmod(time, len) + len, len);
        else if (len > 0.0f) t = Mathf::Clamp(time, 0.0f, len);

        Vec3 pos = transform->localPosition;
        Vec3 scl = transform->localScale;
        bool f;
        float v;
        v = clip.Evaluate("position.x", t, f); if (f) pos.x = v;
        v = clip.Evaluate("position.y", t, f); if (f) pos.y = v;
        v = clip.Evaluate("position.z", t, f); if (f) pos.z = v;
        v = clip.Evaluate("scale.x", t, f);    if (f) scl.x = v;
        v = clip.Evaluate("scale.y", t, f);    if (f) scl.y = v;
        v = clip.Evaluate("scale.z", t, f);    if (f) scl.z = v;
        transform->localPosition = pos;
        transform->localScale = scl;

        // Rotation: support full euler (x/y/z) so 3D rigs (character parts) animate,
        // not just 2D spin. Any present axis rebuilds the rotation.
        bool fx, fy, fz;
        float rx = clip.Evaluate("rotation.x", t, fx);
        float ry = clip.Evaluate("rotation.y", t, fy);
        float rz = clip.Evaluate("rotation.z", t, fz);
        if (fx || fy || fz) transform->localRotation = Quat::Euler(fx ? rx : 0.0f, fy ? ry : 0.0f, fz ? rz : 0.0f);
    }

    float m_time = 0.0f;
};

} // namespace okay
