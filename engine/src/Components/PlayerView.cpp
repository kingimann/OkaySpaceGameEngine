#include "okay/Components/PlayerView.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/PlayerCamera.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ControllerFeel.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/FirstPersonHand.hpp"
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

// A FirstPersonHand already owns the arm pose. The view toggle must not fight it.
bool HandOwnsArm(GameObject* go, Camera* cam) {
    if (go && go->GetComponent<FirstPersonHand>()) return true;
    return cam && cam->gameObject && cam->gameObject->GetComponent<FirstPersonHand>();
}

// The lens is in front of the face (player -Z is forward). The head stays
// drawn until then: skipping it while this camera is still looking at the
// body is what leaves a neck where the head was.
bool LensPastFace(GameObject* go, Camera* cam) {
    if (!go || !go->transform || !cam || !cam->transform) return false;
    const Vec3 local = go->transform->InverseTransformPoint(cam->transform->Position());
    return local.z < -0.16f;
}

// First-person pitch is positive looking up. A third-person orbit is the opposite.
float LookUpDegrees(GameObject* go) {
    if (!go) return 0.0f;
    if (auto* tp = go->GetComponent<ThirdPersonController>()) return -tp->pitch;
    if (auto* fp = go->GetComponent<FirstPersonController>()) return fp->pitch;
    return 0.0f;
}

Pose FirstFromThird(const ThirdPersonController& tp, const Vec3& eyeOffset) {
    // Same socket as a first-person controller (stand 1.6, crouch 1.2, prone 0.4).
    // The orbit's cameraHeight is the look-at point, about the collar, and sitting
    // the eye there looks out through the neck and down into the open chest.
    const float eyeY = StanceValue(tp.stanceLevel(), 1.6f, 1.2f, 0.4f) + eyeOffset.y;
    const Quat yawQ = Quat::Euler(0, tp.yaw, 0);
    const float roll = -tp.lean() * tp.leanAngle;
    // Third-person pitch is positive looking down. The eye starts just in front
    // of the face and moves further forward and up as that pitch increases.
    float down = tp.pitch > 0.0f ? std::min(tp.pitch, 85.0f) / 85.0f : 0.0f;
    float h = 1.0f;
    Character* ch = tp.gameObject ? tp.gameObject->GetComponent<Character>() : nullptr;
    if (ch && ch->height > 0.0f) h = ch->height;
    const float fwd = (0.20f + down * 0.18f) * h;
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

bool FirstPersonOwns(GameObject* go) {
    if (!go || !go->GetComponent<FirstPersonController>()) return false;
    if (!go->GetComponent<ThirdPersonController>()) return true;
    if (auto* v = go->GetComponent<PlayerView>()) return v->TargetFirst();
    return true;
}

bool PlayerHasBothViews(GameObject* go) {
    return go && go->GetComponent<FirstPersonController>() && go->GetComponent<ThirdPersonController>();
}

bool FirstPersonWritesEye(GameObject* go) {
    if (!go || !go->GetComponent<ThirdPersonController>()) return true;
    if (auto* view = go->GetComponent<PlayerView>(); view && view->Driving()) {
        if (view->Blending()) return true;
        return view->TargetFirst();
    }
    return FirstPersonOwns(go);
}

bool ThirdPersonWritesCamera(GameObject* go) {
    if (!go || !go->GetComponent<FirstPersonController>()) return true;
    if (auto* view = go->GetComponent<PlayerView>(); view && view->Driving()) {
        if (view->Blending()) return false;
        return !view->TargetFirst();
    }
    return !FirstPersonOwns(go);
}

void HandOffToFirstPerson(GameObject* go) {
    if (!go) return;
    auto* fp = go->GetComponent<FirstPersonController>();
    auto* tp = go->GetComponent<ThirdPersonController>();
    if (!fp || !tp || !tp->HasDriven()) return;
    // Orbit pitch is positive looking down. Its rest is the third-person camera
    // tilt, not the player's gaze: sitting on that rest becomes a level horizon,
    // and a look away from rest carries over one for one.
    fp->OnTeleported();
    fp->AcceptHandoff(tp->yaw, -(tp->pitch - tp->restPitch()),
                      static_cast<int>(tp->stance()), tp->lean(), tp->IsGrounded());
}

void HandOffToThirdPerson(GameObject* go) {
    if (!go) return;
    auto* fp = go->GetComponent<FirstPersonController>();
    auto* tp = go->GetComponent<ThirdPersonController>();
    if (!fp || !tp || !fp->HasDriven()) return;
    tp->OnTeleported();
    tp->AcceptHandoff(fp->yaw, tp->restPitch() - fp->pitch,
                      static_cast<int>(fp->stance()), fp->lean(), fp->IsGrounded());
}

void PlayerView::SetView(int view) {
    const View next = (view == 0) ? View::First : View::Third;
    m_target = next;
    m_scripted = true;
    if (!m_live) m_blend = (m_target == View::Third) ? 1.0f : 0.0f;
    if (snapSwitch) m_blend = (m_target == View::Third) ? 1.0f : 0.0f;
    NoteIllegalPitch();
}

int PlayerView::GetView() const {
    if (!Driving()) {
        if (PlayerHasBothViews(gameObject)) return m_target == View::First ? 0 : 1;
        return NativeThird(gameObject) ? 1 : 0;
    }
    return m_target == View::First ? 0 : 1;
}

void PlayerView::Toggle() {
    if (!m_live && !m_armed) ArmDefault();
    m_target = (m_target == View::First) ? View::Third : View::First;
    if (snapSwitch) m_blend = (m_target == View::Third) ? 1.0f : 0.0f;
    NoteIllegalPitch();
}

void PlayerView::RememberRestPitch() {
    if (m_haveRestPitch || !gameObject) return;
    if (auto* tp = gameObject->GetComponent<ThirdPersonController>()) m_restPitch = tp->pitch;
    else if (auto* fp = gameObject->GetComponent<FirstPersonController>()) m_restPitch = fp->pitch;
    m_haveRestPitch = true;
}

void PlayerView::NoteIllegalPitch() {
    // Called when the target changes, before the controller clamps. An angle
    // the orbit cannot hold is remembered so the clamp stays open while it
    // eases home. A legal angle is not marked, and leaving third person drops it.
    if (!gameObject || m_target != View::Third) {
        if (m_target != View::Third) m_pitchHoming = false;
        return;
    }
    RememberRestPitch();
    float pitch = 0.0f, lo = -20.0f, hi = 70.0f;
    if (auto* tp = gameObject->GetComponent<ThirdPersonController>()) {
        pitch = tp->pitch; lo = tp->minPitch; hi = tp->maxPitch;
    } else if (auto* fp = gameObject->GetComponent<FirstPersonController>()) {
        pitch = fp->pitch; lo = -70.0f; hi = 20.0f;
    } else return;
    if (pitch < lo || pitch > hi) {
        if (!m_pitchHoming) m_homeFrom = ClampLookPitch(pitch);
        m_pitchHoming = true;
    }
}

void PlayerView::EasePitchHome(float dt) {
    // A look that third person cannot hold (a first-person look down) eases all
    // the way back to the orbit pitch over the same blend. Stopping at the
    // window edge would leave a top-down camera. A pitch that was already a
    // legal orbit angle is left alone, and a reverse clears the correction.
    if (!Driving() || m_target != View::Third || !gameObject) {
        m_pitchHoming = false;
        return;
    }
    RememberRestPitch();
    float* pitch = nullptr;
    if (auto* tp = gameObject->GetComponent<ThirdPersonController>()) pitch = &tp->pitch;
    else if (auto* fp = gameObject->GetComponent<FirstPersonController>()) pitch = &fp->pitch;
    if (!pitch) { m_pitchHoming = false; return; }
    NoteIllegalPitch();
    if (!m_pitchHoming) return;
    const float goal = m_restPitch;
    if (std::fabs(*pitch - goal) < 0.05f || snapSwitch || blendTime <= 1e-3f || !Blending()) {
        *pitch = goal;
        m_pitchHoming = false;
        return;
    }
    const float span = std::max(0.05f, std::fabs(m_homeFrom - goal));
    const float step = span / std::max(0.05f, blendTime) * std::max(0.0f, dt);
    float d = goal - *pitch;
    if (d > step) d = step;
    if (d < -step) d = -step;
    *pitch += d;
}

float PlayerView::Ease01(float u) const {
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    switch (switchEase) {
        case SwitchEase::Smooth: return u * u * (3.0f - 2.0f * u);
        case SwitchEase::In:     return u * u;
        case SwitchEase::Out:    return 1.0f - (1.0f - u) * (1.0f - u);
        case SwitchEase::Linear:
        default:                 return u;
    }
}

float PlayerView::EasedThird() const {
    return Ease01(m_blend);
}

bool PlayerView::CameraNearEyes(Camera* cam) const {
    if (!gameObject || !gameObject->transform || !cam || !cam->transform) return false;
    // The blend is a straight slide from the orbit to the eyes, and that line
    // goes through the skull. Hide the head once the lens is within about one
    // head of it, whether the lens is still behind the hair or already in
    // front of the face. A walk brings the back of the head up to the lens
    // before the old "in front of the face" test ever fired.
    Vec3 head = gameObject->transform->TransformPoint(Vec3{eyeOffset.x, 1.62f, 0.0f});
    float size = 0.32f;
    if (Character* ch = gameObject->GetComponent<Character>()) {
        if (GameObject* part = ch->Part(HumanoidMotion::B_HEAD)) {
            if (part->transform) head = part->transform->Position();
            if (auto* mr = part->GetComponent<MeshRenderer>()) {
                Vec3 lo, hi;
                mr->mesh.Bounds(lo, hi);
                Vec3 ext = hi - lo;
                if (part->transform) {
                    const Vec3 sc = part->transform->LossyScale();
                    ext.x = std::fabs(ext.x * sc.x);
                    ext.y = std::fabs(ext.y * sc.y);
                    ext.z = std::fabs(ext.z * sc.z);
                }
                const float m = std::max(ext.x, std::max(ext.y, ext.z));
                if (m > 0.05f) size = m;
            }
        }
    }
    const float dist = (cam->transform->Position() - head).Magnitude();
    // Hide before the skull fills the frame. The straight slide passes the
    // back of the head while the lens is still more than a metre out, which
    // the old 0.90 m test still drew. A resting orbit (about 5 m) stays outside.
    return dist < std::max(size * 2.2f + 0.25f, 1.45f);
}

void PlayerView::PullCameraIn(Camera* cam, const Vec3& eye, Vec3& want) const {
    if (!cam || !gameObject) return;
    Scene* sc = gameObject->scene();
    if (!sc || !sc->physicsEnabled) return;
    Vec3 delta = want - eye;
    const float dist = delta.Magnitude();
    if (dist < 0.05f) return;
    const Vec3 dir = delta * (1.0f / dist);
    const float radius = 0.18f;
    const float skin = 0.12f;
    Physics3D::IgnoreHierarchy own(gameObject);
    const RaycastHit3D hit = sc->physics3D().SphereCast(
        *sc, eye, dir, radius, dist + skin, gameObject, Physics3D::kAllLayers, false);
    if (!hit.hit) return;
    // A probe that starts inside a solid reports distance 0. Pulling to that
    // origin pins the lens on the buried eye for the whole blend, so V cannot
    // climb back out. Leave the lerped pose; KeepLensClear lifts a lens that
    // is actually under the ground.
    if (hit.distance < 0.02f) return;
    const float allow = std::max(0.05f, hit.distance - skin);
    if (allow < dist) want = eye + dir * allow;
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
    if (snapSwitch || blendTime <= 1e-3f) { m_blend = goal; return; }
    const float speed = 1.0f / blendTime;
    const float step = speed * std::max(0.0f, dt);
    if (m_blend < goal) m_blend = std::min(goal, m_blend + step);
    else                m_blend = std::max(goal, m_blend - step);
}

void PlayerView::DestLimits(bool thirdController, float nativeMin, float nativeMax, float& lo, float& hi) const {
    if (m_target == View::First) {
        if (thirdController) { lo = -89.0f; hi = 89.0f; }
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
    if (Driving()) {
        float dlo, dhi;
        DestLimits(thirdController, nativeMin, nativeMax, dlo, dhi);
        if (!Blending()) { lo = dlo; hi = dhi; }
        else {
            lo = std::min(lo, dlo);
            hi = std::max(hi, dhi);
        }
    }
    // Hold the window open around an angle that is easing home. Clamping it to
    // the orbit edge first would freeze a look-down as a top-down camera.
    if (m_pitchHoming) {
        lo = std::min(lo, std::min(m_homeFrom, m_restPitch));
        hi = std::max(hi, std::max(m_homeFrom, m_restPitch));
    }
    // Homing must not hold an angle past straight up or down. That is the flip.
    ClampPitchWindow(lo, hi);
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
    // A Player Controller owns its toggle: its View setting decides, not a script.
    if (PlayerController* pc = PlayerController::Of(self)) return pc->toggle();
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
    if (!host || PlayerController::Of(host)) return nullptr;
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
        // The controller owns this camera. Only undo a body setup this view
        // applied earlier; don't clear an ignore the controller set itself.
        if (m_fpApplied) ApplyHead();
        else if (Character* ch = gameObject ? gameObject->GetComponent<Character>() : nullptr) {
            ch->hideOwnHead = false;
            if (ch->PartsBuilt()) ch->ApplyOwnerVisibility(false);
        }
        return;
    }
    StepBlend(dt);
    EasePitchHome(dt);
    ApplyHead();
}

void PlayerView::ApplyHead() {
    if (!gameObject) return;
    Character* ch = gameObject->GetComponent<Character>();
    Camera* cam = ViewCamera(gameObject);
    const float into = Driving() ? FirstAmount() : 0.0f;
    const bool near = Driving() && CameraNearEyes(cam);
    // Build the rig on the way in, and pose it this frame. Waiting until the
    // lens arrives, or leaving the new parts at the bind pose, is the frame
    // where the body vanishes or stands in a T.
    if (ch && Driving() && (TargetFirst() || near) && !ch->driveExternal && !ch->PartsBuilt()) {
        ch->separateParts = true;
        ch->BuildParts();
        ch->PoseBuiltParts();
    }
    const bool rig = ch && ch->PartsBuilt();
    const bool ownArm = HandOwnsArm(gameObject, cam);
    if (ch && !ownArm) {
        if (Driving() && into > 0.001f) {
            ch->firstPersonArm = true;
            ch->fpArmBob = true;
            ch->fpSteady = false;
            ch->ApplyFirstPersonHandDrive(false);
            ch->fpPitch = LookUpDegrees(gameObject);
            ch->SetViewmodelBlend(into);
        } else {
            ch->SetViewmodelBlend(Driving() ? 0.0f : -1.0f);
            ch->firstPersonArm = false;
            ch->fpArmBase = -1;
            ch->fpPitch = 0.0f;
        }
    } else if (ch) {
        ch->SetViewmodelBlend(Driving() ? into : -1.0f);
    }
    // Head and neck drop out of this camera once the lens is within a head of
    // the skull, and come back the moment it pulls away. The mesh stays active
    // and keeps casting shadows, so every other camera still sees the head.
    // The target view does not matter: a reverse mid-blend follows the camera.
    if (near) {
        if (rig) {
            ch->hideOwnHead = false;
            ch->ApplyOwnerVisibility(true);
            if (GameObject* head = ch->Part(HumanoidMotion::B_HEAD)) {
                head->active = true;
                if (head->transform) head->transform->localScale = Vec3::One;
                if (auto* mr = head->GetComponent<MeshRenderer>()) mr->castShadows = true;
            }
        }
        if (cam) {
            bool hideFromOwner = true;
            if (auto* fpc = gameObject->GetComponent<FirstPersonController>())
                hideFromOwner = !fpc->showBody;
            cam->ignoreObject = hideFromOwner ? gameObject : nullptr;
            if (hideFromOwner) {
                if (m_savedNear < 0.0f)
                    m_savedNear = cam->nearClip > 0.06f ? cam->nearClip : 0.3f;
                if (cam->nearClip > 0.06f) cam->nearClip = 0.04f;
            }
        }
        m_fpApplied = true;
        return;
    }
    if (ch) ch->hideOwnHead = false;
    // Not at the eyes yet. The body stays drawn. While the copy is easing in,
    // point this camera at the viewmodel anchor so the hands and the held copy
    // are visible without skipping the head.
    const bool copy = ch && ch->OwnerCopyVisible();
    if (ch && rig && !copy) ch->ApplyOwnerVisibility(false);
    if (cam) {
        if (copy) {
            if (GameObject* anchor = ch->EnsureViewAnchor())
                cam->ignoreObject = anchor;
        } else {
            cam->ignoreObject = nullptr;
        }
        if (m_savedNear >= 0.0f) {
            cam->nearClip = m_savedNear;
            m_savedNear = -1.0f;
        }
    }
    m_fpApplied = false;
}

namespace {

bool PoseFinite(const Vec3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

Vec3 StandingEye(GameObject* go) {
    float h = 1.6f;
    if (go)
        if (auto* fp = go->GetComponent<FirstPersonController>()) h = fp->standEyeHeight;
    if (!go || !go->transform) return Vec3{0.0f, h, -0.2f};
    return go->transform->TransformPoint(Vec3{0.0f, h, -0.20f});
}

/// The blend start is the eye the first-person controller just wrote. A pose
/// that is non-finite, metres away from that eye, or under the feet is a stale
/// orbit or a zero — not a place to start the slide.
void RejectStaleEye(GameObject* go, Vec3& pos) {
    if (!go || !go->transform) return;
    const Vec3 eye = StandingEye(go);
    const Vec3 feet = go->transform->Position();
    const bool bad = !PoseFinite(pos) || (pos - eye).Magnitude() > 4.0f || pos.y < feet.y - 0.5f;
    if (bad) pos = eye;
}

/// The straight slide from the orbit to the eyes passes through the skull.
/// Bow over the shoulder only on that stretch (t near the eyes). t near 1
/// (just leaving the orbit, and the settled orbit) is unchanged.
Vec3 ShoulderBow(Transform* body, const Vec3& pos, float t, float shoulder) {
    if (!body || !(t > 0.03f && t < 0.45f)) return pos;
    const float u = (t - 0.03f) / 0.42f;
    const float bow = std::sin(u * 3.14159265f);
    const float side = shoulder < 0.0f ? -1.0f : 1.0f;
    const float mag = std::max(0.55f, std::fabs(shoulder));
    const Vec3 right = body->Rotation() * Vec3{1.0f, 0.0f, 0.0f};
    return pos + right * (side * mag * bow) + Vec3{0.0f, 0.45f * bow, 0.0f};
}

} // namespace

void PlayerView::PlaceCamera(float dt) {
    if (!Driving() || !gameObject || !transform) return;
    Camera* cam = ViewCamera(gameObject);
    if (!cam || !cam->transform) return;

    // Both controllers: a settled view is whichever controller just placed the
    // camera (the eye, or the orbit). A blend reads the eye the first-person
    // controller wrote and the orbit the third-person controller stored.
    if (PlayerHasBothViews(gameObject)) {
        auto* tpc = gameObject->GetComponent<ThirdPersonController>();
        if (!Blending()) {
            m_pivot.Reset();
            m_arm.Reset();
            m_aimT = 0.0f;
            m_stanceDrop = 0.0f;
            if (m_target == View::First && (eyeOffset.x != 0.0f || eyeOffset.y != 0.0f || eyeOffset.z != 0.0f))
                cam->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * eyeOffset);
            return;
        }
        if (tpc && tpc->HasPresentedOrbit()) {
            Pose fp = ReadPose(cam->transform);
            if (eyeOffset.x != 0.0f || eyeOffset.y != 0.0f || eyeOffset.z != 0.0f)
                fp.position = fp.position + fp.rotation * eyeOffset;
            RejectStaleEye(gameObject, fp.position);
            Pose tpPose;
            tpPose.position = tpc->PresentedOrbitPosition();
            tpPose.rotation = tpc->PresentedOrbitRotation();
            if (!PoseFinite(tpPose.position)) tpPose.position = fp.position;
            const float t = EasedThird();
            Vec3 pos = Vec3::Lerp(fp.position, tpPose.position, t);
            pos = ShoulderBow(transform, pos, t, shoulderOffset);
            PullCameraIn(cam, fp.position, pos);
            cam->transform->SetPosition(pos);
            cam->transform->SetRotation(Quat::Slerp(fp.rotation, tpPose.rotation, t));
            return;
        }
    }

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

    RejectStaleEye(gameObject, fp.position);
    if (!PoseFinite(tp.position)) tp.position = fp.position;
    const float t = EasedThird();
    Vec3 pos = Vec3::Lerp(fp.position, tp.position, t);
    pos = ShoulderBow(transform, pos, t, shoulderOffset);
    // The straight slide can cut a wall the orbit arm would have stopped at.
    // Sweep from the eyes and keep the lens on the near side of whatever it hits.
    PullCameraIn(cam, fp.position, pos);
    cam->transform->SetPosition(pos);
    cam->transform->SetRotation(Quat::Slerp(fp.rotation, tp.rotation, t));
}

void PlayerView::KeepLensClear(Camera* cam) const {
    if (!cam || !cam->transform || !gameObject || !gameObject->transform) return;
    const Vec3 feet = gameObject->transform->Position();
    Vec3 p = cam->transform->Position();
    if (!PoseFinite(p)) {
        cam->transform->SetPosition(feet + Vec3{0.0f, 1.6f, 0.0f});
        return;
    }
    Scene* sc = gameObject->scene();
    if (sc && sc->physicsEnabled) {
        // Start above the slab. A ray that begins inside it reports no normal
        // and would leave the lens where it is.
        const float startY = std::max(p.y, feet.y + 1.2f) + 0.5f;
        Physics3D::IgnoreHierarchy own(gameObject);
        const RaycastHit3D hit = sc->physics3D().Raycast(
            *sc, Vec3{p.x, startY, p.z}, Vec3{0.0f, -1.0f, 0.0f},
            startY - p.y + 1.0f, gameObject, Physics3D::kAllLayers, false);
        // Below the surface, or sitting on it so the near plane is the ground.
        // A lens that is already up in the air is left where the orbit put it.
        if (hit.hit && hit.normal.y > 0.5f && p.y < hit.point.y + 0.20f) {
            p.y = std::max(hit.point.y + 0.45f, feet.y + 0.45f);
            cam->transform->SetPosition(p);
        }
    } else if (p.y < feet.y + 0.25f) {
        p.y = feet.y + 0.45f;
        cam->transform->SetPosition(p);
    }
}

void PlayerView::LateUpdate(float dt) {
    if (!Driving()) return;
    // Place the lens first. The head is skipped once that lens is within a
    // head of the skull, which PlaceCamera has just done.
    PlaceCamera(dt);
    KeepLensClear(ViewCamera(gameObject));
    ApplyHead();
    // The lens is in its final spot for this frame. The arm copy has to be
    // placed after that, or it is posed on the orbit camera and fills the view.
    if (Character* ch = gameObject ? gameObject->GetComponent<Character>() : nullptr)
        ch->RefreshOwnerArm();
}

} // namespace okay
