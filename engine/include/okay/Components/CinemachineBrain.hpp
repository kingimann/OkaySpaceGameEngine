#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraMath.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <algorithm>
#include <vector>

namespace okay {

/// The Cinemachine "brain": lives on the real (main) camera and, each frame,
/// drives that camera from the live virtual camera. It solves every
/// VirtualCamera, makes the highest-`priority` enabled one live, and eases the
/// real camera's pose and field of view toward it over `blendTime` whenever the
/// live vcam changes — giving smooth, designer-controlled shot transitions.
///
/// Blends (docs/camera_upgrade.md): the shot being left keeps moving while the
/// blend runs (it does not freeze), a blend that is interrupted starts again from
/// exactly where the picture is (no pop), and a shot can carry its own blend time
/// and style. A cutscene (SequencePlayer) takes the camera over while it plays;
/// when it ends the brain blends back instead of jumping.
class CinemachineBrain : public Behaviour {
public:
    /// Seconds to ease between virtual cameras when the live one changes (0 = cut).
    float blendTime = 1.0f;
    /// How the blend moves: a Cut, a straight line, ease in and out (the default),
    /// ease in, ease out, or your own curve (four numbers, see `customCurve`).
    cam::BlendStyle blendStyle = cam::BlendStyle::EaseInOut;
    /// The custom curve's two control points (x1, y1, x2, y2), like a CSS cubic-bezier.
    float customCurve[4] = {0.25f, 0.1f, 0.25f, 1.0f};
    /// Let a playing cutscene have the camera; blend back when it ends.
    bool yieldToCutscene = true;

    void LateUpdate(float dt) override {
        Scene* s = GetScene();
        if (!s || !transform || !gameObject) return;
        if (!(dt > 0.0f) || !std::isfinite(dt)) dt = 0.0f;

        // Which shot is live: a forced one (script / dialogue), else the highest priority.
        std::vector<VirtualCamera*> vcams = s->FindObjectsOfType<VirtualCamera>();
        VirtualCamera* live = nullptr;
        bool forcedPresent = false;
        for (VirtualCamera* v : vcams) {
            if (!v->enabled || !v->gameObject || !v->gameObject->active) continue;
            if (v == m_forced) { live = v; forcedPresent = true; }
            else if (!forcedPresent && (!live || v->priority > live->priority)) live = v;
        }
        if (!forcedPresent) m_forced = nullptr;
        if (m_active && std::find(vcams.begin(), vcams.end(), m_active) == vcams.end()) m_active = nullptr;
        if (m_from && std::find(vcams.begin(), vcams.end(), m_from) == vcams.end()) FreezeFrom();
        for (VirtualCamera* v : vcams) {
            if (!v->enabled || !v->gameObject || !v->gameObject->active) continue;
            v->Solve(dt, v == live);
        }
        if (!live) return;

        Camera* cam = gameObject->GetComponent<Camera>();

        // A cutscene owns the camera while it plays; blend back when it ends.
        bool cutscene = false;
        if (yieldToCutscene)
            for (SequencePlayer* sp : s->FindObjectsOfType<SequencePlayer>())
                if (sp->IsPlaying()) { cutscene = true; break; }
        if (cutscene) { m_wasCutscene = true; m_blend = 1.0f; m_from = nullptr; m_active = live; return; }
        if (m_wasCutscene) {   // the cutscene just ended: start from wherever the picture is now
            m_wasCutscene = false;
            m_fromPose = CurrentPose(cam);
            m_from = nullptr;
            m_active = live;
            Begin(live, cam);
        }

        // A new live vcam starts a fresh blend.
        if (live != m_active) {
            if (m_active && m_blend >= 1.0f) { m_from = m_active; }          // leave the old shot moving
            else { m_from = nullptr; m_fromPose = m_blend < 1.0f ? m_outPose : CurrentPose(cam); }   // interrupted (or first): from the picture now
            m_active = live;
            Begin(live, cam);
        }
        if (m_blend < 1.0f) m_blend = std::min(1.0f, m_blend + dt / std::max(m_duration, 1e-4f));

        const float amount = cam::BlendCurve(m_style, m_blend, customCurve);
        const cam::CamPose to = PoseOf(live, cam);
        cam::CamPose from = m_from ? PoseOf(m_from, cam) : m_fromPose;
        cam::CamPose out = (m_blend >= 1.0f) ? to : cam::MixPoses(from, to, amount);
        if (m_blend >= 1.0f) m_from = nullptr;
        m_outPose = out;

        transform->SetPosition(out.pos);
        if (!transform->Parent()) transform->localRotation = out.rot;
        if (cam) {
            cam->fieldOfView = out.fov;
            if (live->SolvedOrthoSize() > 0.0f || m_orthoTouched) {
                cam->orthographicSize = out.ortho;
                m_orthoTouched = live->SolvedOrthoSize() > 0.0f || m_blend < 1.0f;
            }
        }
    }

    /// The virtual camera currently driving the brain (read-only; runtime).
    VirtualCamera* LiveCamera() const { return m_active; }
    bool  IsBlending() const { return m_blend < 1.0f; }
    /// 0 at the start of a blend, 1 when it is done (before the curve).
    float BlendProgress() const { return m_blend; }

    /// Make this shot live now, whatever the priorities say, blending in over
    /// `seconds` (< 0 = the shot's own time, else the brain's). It stays live until
    /// ReleaseForced. What scripts, dialogue and the timeline use to cut to a shot.
    void ForceLive(VirtualCamera* v, float seconds = -1.0f) {
        m_forced = v;
        m_forcedSeconds = seconds;
    }
    /// Go back to choosing the shot by priority.
    void ReleaseForced() { m_forced = nullptr; m_forcedSeconds = -1.0f; }
    VirtualCamera* Forced() const { return m_forced; }

private:
    void Begin(VirtualCamera* live, Camera* cam) {
        (void)cam;
        float secs = (m_forced == live && m_forcedSeconds >= 0.0f) ? m_forcedSeconds
                     : (live->blendIn >= 0.0f ? live->blendIn : blendTime);
        cam::BlendStyle st = (live->blendInStyle >= 0 && live->blendInStyle <= (int)cam::BlendStyle::Custom)
                                 ? (cam::BlendStyle)live->blendInStyle : blendStyle;
        if (secs <= 0.0f) st = cam::BlendStyle::Cut;
        m_duration = secs;
        m_style = st;
        m_blend = (st == cam::BlendStyle::Cut) ? 1.0f : 0.0f;
    }
    cam::CamPose CurrentPose(Camera* cam) const {
        cam::CamPose p;
        p.pos = transform->Position();
        p.rot = transform->Rotation();
        p.fov = cam ? cam->fieldOfView : 60.0f;
        p.ortho = cam ? cam->orthographicSize : 5.0f;
        return p;
    }
    cam::CamPose PoseOf(VirtualCamera* v, Camera* cam) const {
        cam::CamPose p;
        p.pos = v->SolvedPosition();
        p.rot = v->SolvedRotation();
        p.fov = v->fieldOfView;
        p.ortho = v->SolvedOrthoSize() > 0.0f ? v->SolvedOrthoSize() : (cam ? cam->orthographicSize : 5.0f);
        return p;
    }
    void FreezeFrom() { m_fromPose = m_outPose; m_from = nullptr; }

    VirtualCamera* m_active = nullptr;
    VirtualCamera* m_from = nullptr;      ///< the shot being left, while it is still tracked
    VirtualCamera* m_forced = nullptr;
    float m_forcedSeconds = -1.0f;
    cam::CamPose m_fromPose, m_outPose;
    float m_blend = 1.0f, m_duration = 1.0f;
    cam::BlendStyle m_style = cam::BlendStyle::EaseInOut;
    bool m_wasCutscene = false;
    bool m_orthoTouched = false;
};

} // namespace okay
