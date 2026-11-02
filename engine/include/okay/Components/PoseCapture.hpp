#pragma once
// ---------------------------------------------------------------------------
// Pose Capture — in Play, save the pose that is on the model right now as a
// one-frame clip, or record the next few seconds into a clip. The clip is
// added to the Model Animator, so the Animator can play it.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <string>
#include <vector>

namespace okay {

class PoseCapture : public Component {
public:
    std::string clipName = "Captured";
    float recordSeconds = 1.0f;

    bool IsRecording() const { return m_recording; }
    float RecordedTime() const { return m_time; }
    int LastClip() const { return m_last; }

    /// One frame of the current pose, stored on the Model Animator. -1 if there
    /// is no model. Replaces a clip that already has this name.
    int CaptureFrame() {
        ModelAnimator* ma = FindModel();
        if (!ma) return -1;
        ModelAnimator::Clip clip;
        clip.name = clipName.empty() ? "Captured" : clipName;
        Sample(clip, 0.0f);
        Sample(clip, 1.0f / 60.0f);
        m_last = Store(ma, std::move(clip));
        if (m_last >= 0) ma->PlayIndex(m_last, true, 0.0f);
        return m_last;
    }

    /// Record `seconds` of motion (0 uses recordSeconds). A later CaptureFrame
    /// or a finished timer stores the clip.
    void StartRecord(float seconds) {
        if (seconds <= 0.0f) seconds = recordSeconds;
        if (seconds <= 0.0f) seconds = 1.0f;
        m_build = ModelAnimator::Clip{};
        m_build.name = clipName.empty() ? "Captured" : clipName;
        m_time = 0.0f;
        m_left = seconds;
        m_recording = true;
        Sample(m_build, 0.0f);
    }

    /// Stop early and store what was recorded. -1 if nothing was sampled.
    int StopRecord() {
        m_recording = false;
        ModelAnimator* ma = FindModel();
        if (!ma || m_build.nodes.empty()) return -1;
        if (m_time < 1.0f / 60.0f) Sample(m_build, 1.0f / 60.0f);
        m_last = Store(ma, std::move(m_build));
        m_build = {};
        if (m_last >= 0) ma->PlayIndex(m_last, true, 0.0f);
        return m_last;
    }

    void Update(float dt) override {
        if (!m_recording) return;
        if (dt < 0.0f) dt = 0.0f;
        m_time += dt;
        m_left -= dt;
        Sample(m_build, m_time);
        if (m_left <= 0.0f) StopRecord();
    }

private:
    bool m_recording = false;
    float m_time = 0.0f;
    float m_left = 0.0f;
    int m_last = -1;
    ModelAnimator::Clip m_build;

    ModelAnimator* FindModel() const {
        if (!gameObject) return nullptr;
        if (auto* m = gameObject->GetComponent<ModelAnimator>()) return m;
        for (Transform* t = gameObject->transform; t; t = t->Parent())
            if (t->gameObject) if (auto* m = t->gameObject->GetComponent<ModelAnimator>()) return m;
        if (!gameObject->scene()) return nullptr;
        for (const auto& up : gameObject->scene()->Objects())
            if (up && up->IsSelfOrDescendantOf(gameObject))
                if (auto* m = up->GetComponent<ModelAnimator>()) return m;
        return nullptr;
    }

    void Sample(ModelAnimator::Clip& clip, float time) const {
        ModelAnimator* ma = FindModel();
        if (!ma || !ma->gameObject || !ma->gameObject->scene()) return;
        for (const auto& up : ma->gameObject->scene()->Objects()) {
            if (!up || !up->transform) continue;
            if (!up->IsSelfOrDescendantOf(ma->gameObject)) continue;
            ModelAnimator::NodeClip* node = nullptr;
            for (auto& n : clip.nodes) if (n.node == up->name) { node = &n; break; }
            if (!node) {
                clip.nodes.push_back({});
                node = &clip.nodes.back();
                node->node = up->name;
                node->clip.smoothKeys = false;
                node->clip.loop = false;
            }
            const Vec3 p = up->transform->localPosition;
            const Quat q = up->transform->localRotation;
            const Vec3 s = up->transform->localScale;
            node->clip.AddKey("position.x", time, p.x);
            node->clip.AddKey("position.y", time, p.y);
            node->clip.AddKey("position.z", time, p.z);
            node->clip.AddKey("rotation.qx", time, q.x);
            node->clip.AddKey("rotation.qy", time, q.y);
            node->clip.AddKey("rotation.qz", time, q.z);
            node->clip.AddKey("rotation.qw", time, q.w);
            node->clip.AddKey("scale.x", time, s.x);
            node->clip.AddKey("scale.y", time, s.y);
            node->clip.AddKey("scale.z", time, s.z);
        }
    }

    static int Store(ModelAnimator* ma, ModelAnimator::Clip clip) {
        for (int i = 0; i < (int)ma->clips.size(); ++i) {
            if (ma->clips[i].name == clip.name) { ma->clips[i] = std::move(clip); return i; }
        }
        ma->clips.push_back(std::move(clip));
        return (int)ma->clips.size() - 1;
    }
};

} // namespace okay
