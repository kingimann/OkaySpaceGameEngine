#include "okay/Components/PlayerView.hpp"
#include "okay/Components/PlayerCamera.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/Stance.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>

namespace okay {
namespace {

struct Pose { Vec3 position; Quat rotation; };

Camera* ChildCamera(Transform* t) {
    if (!t) return nullptr;
    for (Transform* c : t->Children())
        if (c && c->gameObject)
            if (auto* cam = c->gameObject->GetComponent<Camera>()) return cam;
    return nullptr;
}

Camera* ViewCamera(GameObject* go) {
    if (!go) return nullptr;
    if (Camera* cam = ChildCamera(go->transform)) return cam;
    Scene* sc = go->scene();
    return sc ? sc->mainCamera : nullptr;
}

bool NativeThird(GameObject* go) {
    return go && go->GetComponent<ThirdPersonController>() && !go->GetComponent<FirstPersonController>();
}

Pose ReadPose(Transform* t) {
    Pose p;
    p.position = t->Position();
    p.rotation = t->Rotation();
    return p;
}

Pose FirstFromThird(const ThirdPersonController& tp, const Vec3& eyeOffset) {
    const float drop = StanceValue(tp.stanceLevel(), 0.0f, tp.crouchHeightDrop, tp.proneHeightDrop);
    const float eyeY = tp.cameraHeight - drop + eyeOffset.y;
    const Quat yawQ = Quat::Euler(0, tp.yaw, 0);
    const float roll = -tp.lean() * tp.leanAngle;
    // Third-person pitch is positive looking down. The eye starts just in front
    // of the face and moves further forward and up as that pitch increases.
    float down = tp.pitch > 0.0f ? std::min(tp.pitch, 85.0f) / 85.0f : 0.0f;
    float h = 1.0f;
    Character* ch = tp.gameObject ? tp.gameObject->GetComponent<Character>() : nullptr;
    if (ch && ch->height > 0.0f) h = ch->height;
    const float fwd = (0.17f + down * 0.18f) * h;
    const float lift = down * 0.20f * h;
    Pose p;
    p.position = tp.transform->Position() + yawQ * Vec3{
        eyeOffset.x + tp.lean() * tp.leanOffset, eyeY + lift, eyeOffset.z - fwd};
    // The eye pitch is the opposite of the orbit pitch, so the look direction
    // doesn't jump when the camera reaches the head.
    p.rotation = yawQ * Quat::Euler(-tp.pitch, 0, roll);
    if (ch) p.position = ch->ClearFirstPersonEye(p.position);
    return p;
}

} // namespace

void PlayerView::SetView(int view) {
    m_target = (view == 0) ? View::First : View::Third;
    m_scripted = true;
    if (!m_live) m_blend = (m_target == View::Third) ? 1.0f : 0.0f;
}

int PlayerView::GetView() const {
    if (!Driving()) return NativeThird(gameObject) ? 1 : 0;
    return m_target == View::First ? 0 : 1;
}

void PlayerView::Toggle() {
    if (!m_live && !m_armed) ArmDefault();
    m_target = (m_target == View::First) ? View::Third : View::First;
}

bool PlayerView::Blending() const {
    const float goal = (m_target == View::Third) ? 1.0f : 0.0f;
    return std::fabs(m_blend - goal) > 1e-3f;
}

void PlayerView::ArmDefault() {
    if (m_scripted || m_armed) return;
    m_target = defaultView;
    m_blend = (m_target == View::Third) ? 1.0f : 0.0f;
    m_armed = true;
}

void PlayerView::StepBlend(float dt) {
    const float goal = (m_target == View::Third) ? 1.0f : 0.0f;
    const float speed = blendTime <= 1e-3f ? 1.0e6f : (1.0f / blendTime);
    const float step = speed * std::max(0.0f, dt);
    if (m_blend < goal) m_blend = std::min(goal, m_blend + step);
    else                m_blend = std::max(goal, m_blend - step);
}

void PlayerView::DestLimits(bool thirdController, float nativeMin, float nativeMax, float& lo, float& hi) const {
    if (m_target == View::First) {
        if (thirdController) { lo = -85.0f; hi = 85.0f; }
        else { lo = nativeMin; hi = nativeMax; }
    } else if (thirdController) {
        lo = nativeMin; hi = nativeMax;
    } else {
        // First-person pitch is positive looking up. Orbit pitch is positive
        // looking down, so the third-person window (-20..70) flips.
        lo = -70.0f; hi = 20.0f;
    }
}

void PlayerView::LookLimits(bool thirdController, float nativeMin, float nativeMax, float& lo, float& hi) const {
    lo = nativeMin; hi = nativeMax;
    if (!Driving()) return;
    float dlo, dhi;
    DestLimits(thirdController, nativeMin, nativeMax, dlo, dhi);
    if (!Blending()) { lo = dlo; hi = dhi; return; }
    lo = std::min(lo, dlo);
    hi = std::max(hi, dhi);
}

void PlayerView::ConformPitch(float& pitch, bool thirdController, float nativeMin, float nativeMax, float dt) {
    if (!Driving() || !Blending()) return;
    float dlo, dhi;
    DestLimits(thirdController, nativeMin, nativeMax, dlo, dhi);
    if (pitch >= dlo && pitch <= dhi) return;
    const float goal = Mathf::Clamp(pitch, dlo, dhi);
    const float span = std::max(1.0f, std::fabs(pitch - goal));
    const float step = span / std::max(0.05f, blendTime) * dt * 1.15f;
    float d = goal - pitch;
    if (d > step) d = step;
    if (d < -step) d = -step;
    pitch += d;
}

PlayerView* PlayerView::FindForScript(GameObject* self, bool create) {
    auto climb = [](GameObject* g) -> PlayerView* {
        for (int n = 0; g && n < 8; ++n) {
            if (auto* v = g->GetComponent<PlayerView>()) return v;
            Transform* p = g->transform ? g->transform->Parent() : nullptr;
            g = p ? p->gameObject : nullptr;
        }
        return nullptr;
    };
    if (PlayerView* v = climb(self)) return v;
    Scene* sc = self ? self->scene() : nullptr;
    if (sc) {
        auto found = sc->FindObjectsOfType<PlayerView>();
        if (!found.empty()) return found[0];
    }
    if (!create) return nullptr;
    auto hasCtrl = [](GameObject* g) {
        return g && (g->GetComponent<FirstPersonController>() || g->GetComponent<ThirdPersonController>());
    };
    GameObject* host = hasCtrl(self) ? self : nullptr;
    if (!host && sc) {
        for (auto* c : sc->FindObjectsOfType<ThirdPersonController>())
            if (c && c->gameObject) { host = c->gameObject; break; }
        if (!host)
            for (auto* c : sc->FindObjectsOfType<FirstPersonController>())
                if (c && c->gameObject) { host = c->gameObject; break; }
    }
    if (!host) return nullptr;
    PlayerView* v = host->AddComponent<PlayerView>();
    v->allowToggle = true;
    if (host->GetComponent<FirstPersonController>() && !host->GetComponent<ThirdPersonController>())
        v->defaultView = View::First;
    v->ArmDefault();
    return v;
}

void PlayerView::Update(float dt) {
    if (!transform || Game::Paused()) return;
    if (allowToggle && !Input::UICaptured()) {
        const bool key = toggleKey && Input::GetKeyDown(toggleKey);
        const bool pad = toggleGamepadButton >= 0 && Input::GetGamepadButtonDown(toggleGamepadButton);
        if (key || pad) Toggle();
    }
    if (!Driving()) {
        if (Character* ch = gameObject ? gameObject->GetComponent<Character>() : nullptr) {
            ch->hideOwnHead = false;
            if (ch->PartsBuilt()) ch->ApplyOwnerVisibility(false);
        }
        return;
    }
    StepBlend(dt);
    ApplyHead();
}

void PlayerView::ApplyHead() {
    if (!gameObject) return;
    // Hide the head once the camera is well on its way to the eyes, so it is
    // gone before the near plane meets it, and back as soon as we leave.
    const bool fp = FirstAmount() > 0.35f;
    Character* ch = gameObject->GetComponent<Character>();
    Camera* cam = ViewCamera(gameObject);
    // A part rig can hide the head and neck from this camera only. The baked
    // mesh cannot, so build the rig on the way into first person.
    if (ch && fp && !ch->driveExternal && !ch->PartsBuilt()) {
        ch->separateParts = true;
        ch->BuildParts();
    }
    if (ch && ch->PartsBuilt()) {
        ch->hideOwnHead = false;
        if (cam) {
            cam->ignoreObject = fp ? gameObject : nullptr;
            // 0.3 m is the depth of the chest, so the default plane cuts it open.
            if (fp && cam->nearClip > 0.06f) cam->nearClip = 0.04f;
        }
        ch->ApplyOwnerVisibility(fp);
        if (GameObject* head = ch->Part(HumanoidMotion::B_HEAD))
            if (head->transform) head->transform->localScale = Vec3::One;
        return;
    }
    if (ch) ch->hideOwnHead = fp;
    // No rig (a custom model that refused one): shrink the head in the shared
    // mesh. Third person sees that too; the part rig above does not have to.
    if (cam) {
        cam->ignoreObject = nullptr;
        if (fp && cam->nearClip > 0.06f) cam->nearClip = 0.04f;
    }
}

void PlayerView::PlaceCamera(float dt) {
    if (!Driving() || !gameObject || !transform) return;
    Camera* cam = ViewCamera(gameObject);
    if (!cam || !cam->transform) return;

    const bool nativeThird = NativeThird(gameObject);
    const float nativeT = nativeThird ? 1.0f : 0.0f;
    const bool settledNative = std::fabs(m_blend - nativeT) <= 1e-3f;
    if (settledNative) {
        // The controller just placed its own camera. Leave it, apart from a
        // first-person eye offset (zero does nothing). Drop the orbit state so
        // the next switch starts from the distance the spring arm wants.
        m_pivot.Reset();
        m_arm.Reset();
        m_aimT = 0.0f;
        m_stanceDrop = 0.0f;
        if (!nativeThird && (eyeOffset.x != 0.0f || eyeOffset.y != 0.0f || eyeOffset.z != 0.0f))
            cam->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * eyeOffset);
        return;
    }

    Pose native = ReadPose(cam->transform);
    Pose fp, tp;
    if (nativeThird) {
        tp = native;
        if (auto* tpc = gameObject->GetComponent<ThirdPersonController>())
            fp = FirstFromThird(*tpc, eyeOffset);
        else fp = native;
    } else {
        fp = native;
        if (eyeOffset.x != 0.0f || eyeOffset.y != 0.0f || eyeOffset.z != 0.0f)
            fp.position = fp.position + fp.rotation * eyeOffset;
        auto* fpC = gameObject->GetComponent<FirstPersonController>();
        if (!fpC) { tp = native; }
        else {
            const float dist = thirdPersonDistance >= 0.0f ? thirdPersonDistance : 4.5f;
            const float eye = StanceValue(fpC->stanceLevel(), fpC->standEyeHeight, fpC->crouchEyeHeight, fpC->proneEyeHeight);
            const bool aiming = fpC->IsZooming();
            Scene* sc = gameObject->scene();
            OrbitPose orbit = PlaceThirdPersonOrbit(
                transform, gameObject, sc,
                fpC->yaw, -fpC->pitch,
                dist, std::max(1.2f, dist * 0.45f), shoulderOffset, std::max(shoulderOffset, 0.55f),
                eye, 0.0f, 0.0f, 0.0f, 12.0f,
                m_stanceDrop, aiming, 10.0f, m_aimT, m_shake,
                m_smooth, 0.0f,
                m_pivot, 0.0f, 0.0f, 0.0f,
                true, m_arm, 0.2f, 0.3f, Physics3D::kAllLayers, 30.0f, 6.0f,
                false, dt);
            tp.position = orbit.position;
            tp.rotation = orbit.rotation;
        }
    }

    const float t = Mathf::Clamp(m_blend, 0.0f, 1.0f);
    cam->transform->SetPosition(Vec3::Lerp(fp.position, tp.position, t));
    cam->transform->SetRotation(Quat::Slerp(fp.rotation, tp.rotation, t));
}

void PlayerView::LateUpdate(float dt) {
    if (!Driving()) return;
    // Head flags for a separated rig are applied after the rig is posed.
    ApplyHead();
    PlaceCamera(dt);
}

} // namespace okay
