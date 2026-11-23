#include "okay/Core/Profiler.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "CharacterFirstPersonInternal.hpp"
#include <functional>

namespace okay {

using namespace HumanoidMotion;   // the bone indices (B_HIPS ...)
using character_fp::OwnerCamera;

void Character::SetOwnerArmActive(bool on) {
    if (m_ownerArm) m_ownerArm->active = on;
    if (m_ownerFore) m_ownerFore->active = on;
    if (m_ownerHand) m_ownerHand->active = on;
}

void Character::ReleaseOwnerArm() {
    if (m_fpHiddenHeld && !m_fpHiddenHeld->IsDestroyed() && m_fpHiddenHeld->ownerCameraSkip == gameObject)
        m_fpHiddenHeld->ownerCameraSkip = nullptr;
    m_fpHiddenHeld = nullptr;
    SetOwnerArmActive(false);
    if (m_ownerItem && !m_ownerItem->IsDestroyed())
        if (Scene* s = GetScene()) s->Destroy(m_ownerItem);
    m_ownerItem = nullptr;
    if (m_ownerArm && !m_ownerArm->IsDestroyed())
        if (Scene* s = GetScene()) s->Destroy(m_ownerArm);
    m_ownerArm = m_ownerFore = m_ownerHand = nullptr;
    m_ownerArmBase = -2;
}

void Character::ReleaseOwnerArm2() {
    if (m_ownerArm2 && !m_ownerArm2->IsDestroyed())
        if (Scene* s = GetScene()) s->Destroy(m_ownerArm2);
    m_ownerArm2 = m_ownerFore2 = m_ownerHand2 = nullptr;
    m_ownerArmBase2 = -2;
}

void Character::DeactivateOwnerArm2() {
    if (m_ownerArm2) m_ownerArm2->active = false;
    if (m_ownerFore2) m_ownerFore2->active = false;
    if (m_ownerHand2) m_ownerHand2->active = false;
}

namespace {
// The owner's first-person view is the character's own hand brick, not a
// made-up cube and not the sleeve. Scale stays inside the range the player
// tests allow. The brick's local +Y is up (the lit face) and local +Z is the
// long axis, pointed toward screen centre, where a held item sits.
constexpr float kFpHandScale = 0.55f;

void PaintFpHand(GameObject* hand, GameObject* srcPart, const FirstPersonHands::HandSide* side) {
    if (!hand) return;
    auto* mr = hand->GetComponent<MeshRenderer>();
    auto* src = srcPart ? srcPart->GetComponent<MeshRenderer>() : nullptr;
    if (!mr || !src) return;
    mr->mesh = src->mesh;
    mr->mesh.normals.clear();
    mr->color = src->color;
    mr->castShadows = false;
    mr->enabled = true;
    if (side && side->useColour) {
        mr->color = side->colour;
        const int faces = (int)mr->mesh.triangles.size() / 3;
        mr->mesh.triColors.assign((std::size_t)std::max(0, faces), side->colour);
        mr->emissive = Color{side->colour.r * 0.37f, side->colour.g * 0.37f, side->colour.b * 0.37f, 1.0f};
    } else {
        mr->emissive = Color::FromBytes(82, 58, 46);
    }
}

void PaintFpSleeve(GameObject* fore, const FirstPersonHands::HandSide* side, const Color& shirt) {
    if (!fore) return;
    auto* mr = fore->GetComponent<MeshRenderer>();
    if (!mr) return;
    if (!side || !side->sleeve) {
        mr->mesh = Mesh{};
        mr->enabled = false;
        return;
    }
    float L = side->sleeveLength;
    if (L < 0.04f) L = 0.04f;
    if (L > 0.8f) L = 0.8f;
    Mesh m;
    Mesh::AppendBox(m, {-0.055f, -0.055f, -0.02f - L}, {0.055f, 0.055f, -0.02f});
    mr->mesh = std::move(m);
    mr->mesh.normals.clear();
    mr->enabled = true;
    mr->castShadows = false;
    mr->doubleSided = false;
    const Color sc = side->matchShirt ? shirt : side->sleeveColour;
    mr->color = sc;
    mr->emissive = Color{sc.r * 0.37f, sc.g * 0.37f, sc.b * 0.37f, 1.0f};
}

// Optional bar under the hand. Off unless the hand asks for it, so a hold is
// a brick in the palm and not a green sleeve filling the corner. `armInCam`
// is the arm's rotation in camera space.
void PaintFpForearm(GameObject* fore, const FirstPersonHands::HandSide* side, const Quat& armInCam, const Color& shirt) {
    if (!fore) return;
    auto* mr = fore->GetComponent<MeshRenderer>();
    if (!mr) return;
    if (!side || !side->forearm || side->sleeve) {
        mr->mesh = Mesh{};
        mr->enabled = false;
        return;
    }
    Color col = side->matchShirt ? shirt : side->forearmColour;
    float length = side->forearmLength;
    if (length < 0.05f) length = 0.05f;
    if (length > 1.2f) length = 1.2f;
    float rad = side->forearmThickness * 0.5f;
    if (rad < 0.012f) rad = 0.012f;
    if (rad > 0.2f) rad = 0.2f;
    const float bend = side->elbowBend * Mathf::Deg2Rad;
    Vec3 down = armInCam.Conjugate() * Vec3{0.0f, -std::cos(bend), -0.16f - std::sin(bend)};
    if (down.SqrMagnitude() < 1e-8f) down = Vec3{0.0f, -1.0f, 0.0f};
    down = down.Normalized();
    Vec3 hint = armInCam.Conjugate() * Vec3{0.0f, 0.0f, -1.0f};
    Vec3 sidev = hint - down * Vec3::Dot(hint, down);
    if (sidev.SqrMagnitude() < 1e-6f) sidev = Vec3{1.0f, 0.0f, 0.0f};
    sidev = sidev.Normalized();
    Vec3 up = Vec3::Cross(sidev, down).Normalized();
    const float start = 0.02f;
    Mesh m;
    Mesh::AppendBox(m, {-rad, start, -rad}, {rad, start + length, rad});
    for (Vec3& v : m.vertices) v = sidev * v.x + down * v.y + up * v.z;
    mr->mesh = std::move(m);
    mr->mesh.normals.clear();
    mr->enabled = true;
    mr->castShadows = false;
    mr->doubleSided = false;
    mr->color = col;
    mr->emissive = Color{col.r * 0.55f, col.g * 0.55f, col.b * 0.55f, 1.0f};
}

Vec3 FpHandScale(const FirstPersonHands::HandSide* side) {
    if (!side) return Vec3::One;
    return Vec3{side->scale.x * side->size.x, side->scale.y * side->size.y, side->scale.z * side->size.z}
         * side->uniformScale;
}

float FpFovScale(const FirstPersonHands* hands, const Camera* cam) {
    if (!hands || !cam || hands->handFov <= 1.0f || cam->fieldOfView <= 1.0f) return 1.0f;
    return std::tan(cam->fieldOfView * Mathf::Deg2Rad * 0.5f)
         / std::tan(hands->handFov * Mathf::Deg2Rad * 0.5f);
}
}

void Character::EnsureOwnerArm(int base) {
    if (m_ownerArm && m_ownerArm->IsDestroyed()) {
        m_ownerArm = m_ownerFore = m_ownerHand = nullptr;
        m_ownerArmBase = -2;
    }
    if (m_ownerArm && m_ownerFore && m_ownerHand && m_ownerArmBase == base) return;
    ReleaseOwnerArm();
    Scene* s = GetScene();
    if (!s || base < 0 || base + 2 >= (int)m_parts.size()) return;
    GameObject* src[3] = {m_parts[base], m_parts[base + 1], m_parts[base + 2]};
    MeshRenderer* mesh[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (!src[i]) return;
        mesh[i] = src[i]->GetComponent<MeshRenderer>();
        if (!mesh[i] || mesh[i]->mesh.vertices.empty()) return;
    }
    const char* names[3] = {"FP_OwnerArm", "FP_OwnerFore", "FP_OwnerHand"};
    GameObject* made[3] = {};
    for (int i = 0; i < 3; ++i) {
        made[i] = s->CreateGameObject(names[i]);
        auto* mr = made[i]->AddComponent<MeshRenderer>();
        mr->mesh = mesh[i]->mesh;
        mr->doubleSided = false;
        mr->castShadows = false;
        made[i]->firstPersonOnly = true;
        made[i]->firstPersonViewmodel = true;
        made[i]->active = false;
    }
    made[1]->transform->SetParent(made[0]->transform, false);
    made[2]->transform->SetParent(made[1]->transform, false);
    for (int i = 0; i < 3; ++i) made[i]->transient = true;
    // The hand keeps the body's brick (one block, skin or glove). The upper arm
    // stays in the hierarchy and draws nothing. The forearm is filled in while
    // a hold is up, so the hand has an arm under it.
    auto* armMr = made[0]->GetComponent<MeshRenderer>();
    armMr->mesh = Mesh{};
    armMr->enabled = false;
    auto* foreMr = made[1]->GetComponent<MeshRenderer>();
    foreMr->mesh = Mesh{};
    foreMr->enabled = false;
    auto* handMr = made[2]->GetComponent<MeshRenderer>();
    handMr->mesh.normals.clear();
    handMr->castShadows = false;
    // The key light sits in front of the lens, so the face you see is in shadow.
    // A warm lift keeps that face skin-coloured; the shade on each side stays.
    handMr->emissive = Color::FromBytes(82, 58, 46);
    // Under the player, not the camera. A first-person-only object is drawn
    // only for the camera that is hiding its owner, and only when it is still
    // inside that owner's subtree. The anchor is that subtree, so a blend can
    // draw the copy without hiding the body. Parenting it to the camera would
    // hide it from the owner too.
    if (GameObject* anchor = EnsureViewAnchor()) {
        if (anchor->transform) made[0]->transform->SetParent(anchor->transform, false);
    } else if (gameObject && gameObject->transform) {
        made[0]->transform->SetParent(gameObject->transform, false);
    }
    m_ownerArm = made[0];
    m_ownerFore = made[1];
    m_ownerHand = made[2];
    m_ownerArmBase = base;
}

void Character::EnsureOwnerArm2(int base) {
    if (m_ownerArm2 && m_ownerArm2->IsDestroyed()) {
        m_ownerArm2 = m_ownerFore2 = m_ownerHand2 = nullptr;
        m_ownerArmBase2 = -2;
    }
    if (m_ownerArm2 && m_ownerFore2 && m_ownerHand2 && m_ownerArmBase2 == base) return;
    ReleaseOwnerArm2();
    Scene* s = GetScene();
    if (!s || base < 0 || base + 2 >= (int)m_parts.size()) return;
    GameObject* src[3] = {m_parts[base], m_parts[base + 1], m_parts[base + 2]};
    MeshRenderer* mesh[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (!src[i]) return;
        mesh[i] = src[i]->GetComponent<MeshRenderer>();
        if (!mesh[i] || mesh[i]->mesh.vertices.empty()) return;
    }
    const char* names[3] = {"FP_OwnerArm2", "FP_OwnerFore2", "FP_OwnerHand2"};
    GameObject* made[3] = {};
    for (int i = 0; i < 3; ++i) {
        made[i] = s->CreateGameObject(names[i]);
        auto* mr = made[i]->AddComponent<MeshRenderer>();
        mr->mesh = mesh[i]->mesh;
        mr->doubleSided = false;
        mr->castShadows = false;
        made[i]->firstPersonOnly = true;
        made[i]->firstPersonViewmodel = true;
        made[i]->active = false;
    }
    made[1]->transform->SetParent(made[0]->transform, false);
    made[2]->transform->SetParent(made[1]->transform, false);
    for (int i = 0; i < 3; ++i) made[i]->transient = true;
    auto* armMr = made[0]->GetComponent<MeshRenderer>();
    armMr->mesh = Mesh{};
    armMr->enabled = false;
    auto* foreMr = made[1]->GetComponent<MeshRenderer>();
    foreMr->mesh = Mesh{};
    foreMr->enabled = false;
    auto* handMr = made[2]->GetComponent<MeshRenderer>();
    handMr->mesh.normals.clear();
    handMr->castShadows = false;
    handMr->emissive = Color::FromBytes(82, 58, 46);
    if (GameObject* anchor = EnsureViewAnchor()) {
        if (anchor->transform) made[0]->transform->SetParent(anchor->transform, false);
    } else if (gameObject && gameObject->transform) {
        made[0]->transform->SetParent(gameObject->transform, false);
    }
    m_ownerArm2 = made[0];
    m_ownerFore2 = made[1];
    m_ownerHand2 = made[2];
    m_ownerArmBase2 = base;
}

namespace character_fp {
// The camera that is allowed to draw this player's first-person-only arm.
// Prefer the one already hiding the player; otherwise the camera mounted on
// the body. A free orbit camera is not it — posing the arm there parks a
// giant sleeve out in the world.
Camera* OwnerCamera(GameObject* player) {
    if (!player || !player->scene()) return nullptr;
    Scene* sc = player->scene();
    Camera* child = nullptr;
    Camera* ignoring = nullptr;
    for (const auto& o : sc->Objects()) {
        if (!o || !o->active) continue;
        Camera* cam = o->GetComponent<Camera>();
        if (!cam || !cam->enabled || !cam->transform || !o->active) continue;
        if (GameObject* ig = cam->ignoreObject) {
            // The player (head hidden) or the viewmodel anchor (body still drawn).
            if (ig == player || (ig->name == "FP_ViewAnchor" && ig->IsSelfOrDescendantOf(player)))
                ignoring = cam;
        }
        if (!child && o.get() != player && o->IsSelfOrDescendantOf(player)) child = cam;
    }
    if (ignoring) return ignoring;
    return child;
}
}

void Character::RefreshOwnerArm() {
    if (!m_partsBuilt) return;
    PoseOwnerArm(CurrentSourcePose());
}

// A click swings the real arm (Character::Punch), so the owner's arm copy swings and other players
// see the same swing. Only in first person: a third person body has no viewmodel to swing.
void FirstPersonHands::Update(float) {
    if (!swingOnClick || Game::Paused() || !gameObject) return;
    Character* c = gameObject->GetComponent<Character>();
    if (!c) return;
    bool first = false;
    if (auto* v = gameObject->GetComponent<PlayerView>()) first = v->Driving() ? v->FirstAmount() > 0.35f : v->TargetFirst();
    else first = gameObject->GetComponent<FirstPersonController>() != nullptr;
    if (!first) return;
    if (GameInputBlocked(gameObject->scene())) return;
    if (Input::GetMouseButtonDown(attackButton) && !c->Punching()) c->Punch();
}

void Character::ApplyFirstPersonHandDrive(bool showLeftArm, bool showWhileRunning) {
    FirstPersonHands* h = FirstPersonHands::FindOn(gameObject);
    if (h && !h->enabled) h = nullptr;
    int primary = showLeftArm ? B_RUPARM : B_LUPARM;
    bool two = false;
    if (h) {
        primary = h->holdMode == FirstPersonHands::HoldMode::Left ? B_RUPARM : B_LUPARM;
        two = h->holdMode == FirstPersonHands::HoldMode::Two;
        if (!h->right.enabled && h->left.enabled) { primary = B_RUPARM; two = false; }
        else if (h->right.enabled && !h->left.enabled) { primary = B_LUPARM; two = false; }
        else if (!h->right.enabled && !h->left.enabled) {
            fpArmBase = primary;
            fpArmUp = 0.0f;
            m_fpSecond = false;
            return;
        }
    }
    bool holding = HandsOnAHold();
    if (holding) {
        auto* ph = gameObject->GetComponent<ProceduralObjectHolding>();
        GameObject* held = ph ? ph->Held() : nullptr;
        auto* ho = held ? held->GetComponent<HoldableObject>() : nullptr;
        if (ho && ho->WantsTwoHands()) {
            const bool rightOn = !h || h->right.enabled;
            const bool leftOn = !h || h->left.enabled;
            if (rightOn && leftOn) { two = true; primary = B_LUPARM; }
        } else if (ho && ho->style == HoldableObject::Style::OneHandLeft) {
            primary = B_RUPARM;
            two = false;
        } else if (ho && ho->style == HoldableObject::Style::OneHandRight) {
            primary = B_LUPARM;
            two = false;
        }
    }
    const bool run = HideFirstPersonHands();
    // The panel wins when it is on the player. A legacy hand has no panel, so
    // its own Show While Running is the only way a sprint keeps a held item.
    const bool allowRun = h ? h->showWhileRunning : showWhileRunning;
    const bool show = (holding || (h && h->showWhenEmpty)) && (!run || allowRun);
    fpArmBase = primary;
    fpArmUp = show ? 1.0f : 0.0f;
    // The two-hand pose is built around the held item, so with empty hands one arm shows.
    m_fpSecond = two && holding;
}

void Character::PoseOwnerArm(const std::vector<Vec3>&) {
    const int base = fpArmBase;
    const bool on = firstPersonArm && base >= 0 && base + 2 < (int)m_parts.size();
    auto clearHeld = [&]() {
        if (m_fpHiddenHeld && !m_fpHiddenHeld->IsDestroyed() && m_fpHiddenHeld->ownerCameraSkip == gameObject)
            m_fpHiddenHeld->ownerCameraSkip = nullptr;
        m_fpHiddenHeld = nullptr;
        if (m_ownerItem) m_ownerItem->active = false;
    };
    // A run eases the copy off the corner (m_fpShown). It is not popped off.
    // A view blend uses a lower cutoff so the copy can sit on the grip at the
    // start of the ease instead of appearing later in the corner.
    const float hideAt = fpViewWeight >= 0.0f ? 0.02f : 0.08f;
    if (!on || m_fpShown < hideAt) {
        SetOwnerArmActive(false);
        DeactivateOwnerArm2();
        if (m_ownerItem) m_ownerItem->active = false;
        // A sprint keeps the real item out of this camera even when the copy
        // never rose. Leaving first person, or dropping the copy while walking,
        // gives the real grip back.
        const bool release = !on || (fpViewWeight >= 0.0f && !HideFirstPersonHands());
        if (release) clearHeld();
        else if (gameObject) {
            auto* hold = gameObject->GetComponent<ProceduralObjectHolding>();
            GameObject* held = (hold && hold->IsHolding()) ? hold->Held() : nullptr;
            if (held && !held->IsDestroyed()) {
                if (m_fpHiddenHeld && m_fpHiddenHeld != held && !m_fpHiddenHeld->IsDestroyed()
                    && m_fpHiddenHeld->ownerCameraSkip == gameObject)
                    m_fpHiddenHeld->ownerCameraSkip = nullptr;
                held->ownerCameraSkip = OwnerHideTarget() ? OwnerHideTarget() : gameObject;
                m_fpHiddenHeld = held;
            }
        }
        return;
    }
    EnsureOwnerArm(base);
    if (!m_ownerArm || !m_ownerFore || !m_ownerHand || !m_ownerArm->transform) return;
    Transform* armParent = nullptr;
    if (GameObject* anchor = EnsureViewAnchor()) armParent = anchor->transform;
    else if (gameObject) armParent = gameObject->transform;
    if (armParent && m_ownerArm->transform->Parent() != armParent)
        m_ownerArm->transform->SetParent(armParent, false);
    Camera* cam = OwnerCamera(gameObject);
    if (!cam || !cam->transform || !gameObject || !gameObject->transform) {
        SetOwnerArmActive(false);
        DeactivateOwnerArm2();
        return;
    }
    const Quat playerRot = gameObject->transform->Rotation();
    const Quat camRot = cam->transform->Rotation();
    const Quat camLocal = (playerRot.Conjugate() * camRot).Normalized();
    const Vec3 camEuler = camLocal.ToEuler();
    const float viewPitch = camEuler.x;
    // followPitch writes fpPitch from the lens. A manual pitch stays put.
    const bool follow = std::fabs(fpPitch - viewPitch) < 8.0f;
    const Quat basis = follow ? camRot : (playerRot * Quat::Euler(fpPitch, camEuler.y, camEuler.z));

    // Mouse look lags: the arm swings a little against the turn, then catches up.
    // The camera itself is not moved.
    const Vec3 camWorld = camRot.ToEuler();
    if (!m_fpTrailInit) {
        m_fpPrevYaw = camWorld.y; m_fpPrevPitch = camWorld.x; m_fpTrailInit = true;
    }
    const float dy = Mathf::DeltaAngle(m_fpPrevYaw, camWorld.y);
    const float dp = camWorld.x - m_fpPrevPitch;
    m_fpPrevYaw = camWorld.y; m_fpPrevPitch = camWorld.x;
    const float dt = m_fpDt > 1e-4f ? m_fpDt : 0.016f;
    const float decay = std::exp(-dt * 9.0f);
    // Against the turn: a look to the right leaves the arm on the left for a
    // moment (it was already there in the world), then it catches the corner.
    m_fpTrailYaw = Mathf::Clamp(m_fpTrailYaw * decay + dy * 0.55f, -14.0f, 14.0f);
    m_fpTrailPitch = Mathf::Clamp(m_fpTrailPitch * decay - dp * 0.45f, -12.0f, 12.0f);

    // Screen right for the L-named bones (the rig's yaw puts them there).
    const float side = base >= B_RUPARM ? -1.0f : 1.0f;
    const float aspect = cam->viewportAspect > 0.05f ? cam->viewportAspect : (16.0f / 9.0f);
    const float vFov = cam->VerticalFovDegrees(aspect) * Mathf::Deg2Rad;
    const float tanV = std::tan(std::max(0.15f, vFov * 0.5f));
    const float tanH = tanV * aspect;
    const float near = std::max(cam->nearClip, 0.04f);
    // Close enough that the hand reads as yours. Farther than this it floats.
    const float depth = std::max(0.30f, near + 0.14f);

    // The body's hand brick. Local +Z (its long axis) points inward. Local +Y,
    // the broad face, points back at the lens and up so it takes the light.
    // No sleeve.
    float snap = 0.0f;
    if (m_punchT >= 0.0f && m_punchT < 1.0f) {
        if (m_punchT < 0.32f) snap = std::sin(m_punchT / 0.32f * 1.5707963f);
        else snap = std::sin((1.0f - (m_punchT - 0.32f) / 0.68f) * 1.5707963f);
    }
    const float slide = 1.0f - m_fpShown;
    const bool stepping = anim == 2 || anim == 7 || anim == 17
        || (anim == 6 && std::fabs(m_gaitSpeed) > 0.2f);
    float bobS = 0.0f, bobC = 0.0f;
    if (fpArmBob && !fpSteady && stepping) {
        bobS = std::sin(m_gaitPhase * 6.2831853f);
        bobC = std::cos(m_gaitPhase * 6.2831853f);
    }
    FirstPersonHands* fpHands = FirstPersonHands::FindOn(gameObject);
    if (fpHands && !fpHands->enabled) fpHands = nullptr;
    const FirstPersonHands::HandSide* fpSide = nullptr;
    if (fpHands) {
        bobS *= fpHands->bobAmount;
        bobC *= fpHands->bobAmount;
        snap *= fpHands->punchAmount;
        fpSide = base >= B_RUPARM ? &fpHands->left : &fpHands->right;
    }
    // Long axis toward screen centre. The broad face is turned up toward the
    // key light and only a little back at the lens: a face square to the lens
    // is in shadow, because the light sits in front of the camera.
    Vec3 inward{-side * 0.92f, -0.02f, -0.08f};
    inward.y += 0.10f * bobS;
    if (snap > 0.0f) {
        const Vec3 aim{-side * 0.18f, 0.88f, 0.12f};
        inward = inward + (aim - inward) * snap;
    }
    if (inward.SqrMagnitude() < 1e-8f) inward = Vec3{-side, 0.2f, 0.1f};
    inward = inward.Normalized();
    Vec3 topHint{side * 0.04f, 0.86f, 0.40f};
    Vec3 top = topHint - inward * Vec3::Dot(topHint, inward);
    if (top.SqrMagnitude() < 1e-6f) top = Vec3{0.0f, 1.0f, 0.0f};
    top = top.Normalized();
    Quat camSpace = Quat::LookRotation(inward, top);
    camSpace = Quat::Euler(1.6f * bobS, -side * 2.2f * bobC, side * 1.1f * bobS) * camSpace;
    if (fpSide && !m_fpSecond)
        camSpace = Quat::Euler(fpSide->elbowBend, 0.0f, 0.0f) * Quat::Euler(fpSide->wristRotation)
                 * Quat::Euler(fpSide->rotation) * camSpace;
    m_ownerFore->transform->localPosition = Vec3::Zero;
    m_ownerFore->transform->localRotation = Quat::Identity;
    m_ownerFore->transform->localScale = Vec3::One;
    m_ownerHand->transform->localPosition = Vec3::Zero;
    m_ownerHand->transform->localRotation = Quat::Identity;
    m_ownerHand->transform->localScale = (!m_fpSecond) ? FpHandScale(fpSide) : Vec3::One;
    if (!m_fpSecond && base + 2 < (int)m_parts.size()) {
        PaintFpHand(m_ownerHand, m_parts[base + 2], fpSide);
        PaintFpSleeve(m_ownerFore, fpSide, shirt);
    }
    const float fovScale = FpFovScale(fpHands, cam);
    m_ownerArm->transform->localScale = Vec3{kFpHandScale * fovScale, kFpHandScale * fovScale, kFpHandScale * fovScale};
    // Measure in the camera, then correct. One shift is not enough: a nearer
    // corner projects further out than the vertex that was extreme before the move.
    m_ownerArm->transform->SetRotation(cam->transform->Rotation() * camSpace);
    m_ownerArm->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * Vec3{side * 0.16f, -0.08f, -depth});

    // A hold stays on the real hands for everyone else. This camera sees a
    // copy of the prop on the inner face of the cube, and skips the real one.
    // Placed before the screen pin so the item cannot stick off the corner.
    bool showItem = false;
    ProceduralObjectHolding* hold = gameObject->GetComponent<ProceduralObjectHolding>();
    GameObject* held = (hold && hold->IsHolding()) ? hold->Held() : nullptr;
    if (!held || !held->transform || held->IsDestroyed()) {
        clearHeld();
    } else {
        if (m_fpHiddenHeld && m_fpHiddenHeld != held && !m_fpHiddenHeld->IsDestroyed()
            && m_fpHiddenHeld->ownerCameraSkip == gameObject)
            m_fpHiddenHeld->ownerCameraSkip = nullptr;
        held->ownerCameraSkip = OwnerHideTarget() ? OwnerHideTarget() : gameObject;
        m_fpHiddenHeld = held;
        if (!m_ownerItem || m_ownerItem->IsDestroyed()) {
            m_ownerItem = nullptr;
            if (Scene* sc = GetScene()) {
                m_ownerItem = sc->CreateGameObject("FP_OwnerItem");
                auto* mr = m_ownerItem->AddComponent<MeshRenderer>();
                mr->castShadows = false;
                mr->doubleSided = false;
                m_ownerItem->firstPersonOnly = true;
                m_ownerItem->firstPersonViewmodel = true;
                m_ownerItem->transient = true;
                m_ownerItem->active = false;
            }
        }
        if (m_ownerItem && m_ownerItem->transform) {
            m_ownerItem->transform->SetParent(m_ownerHand->transform, false);
            MeshRenderer* src = held->GetComponent<MeshRenderer>();
            if (!src)
                for (Transform* c : held->transform->Children())
                    if (c && c->gameObject)
                        if (MeshRenderer* m = c->gameObject->GetComponent<MeshRenderer>()) { src = m; break; }
            auto* dst = m_ownerItem->GetComponent<MeshRenderer>();
            if (!src || !dst || src->mesh.vertices.empty()) {
                m_ownerItem->active = false;
            } else {
                dst->mesh = src->mesh;
                dst->color = src->color;
                dst->castShadows = false;
                dst->mesh.normals.clear();
                // The prop sits in the palm (local +Y), a little toward the inner
                // end. It is smaller than the hand, and overlaps the palm just
                // enough to read as held.
                auto* handMr = m_ownerHand->GetComponent<MeshRenderer>();
                Vec3 hmn = handMr && !handMr->mesh.vertices.empty() ? handMr->mesh.vertices[0] : Vec3{-0.06f, -0.06f, -0.08f};
                Vec3 hmx = hmn;
                if (handMr)
                    for (const Vec3& v : handMr->mesh.vertices) {
                        hmn.x = std::min(hmn.x, v.x); hmn.y = std::min(hmn.y, v.y); hmn.z = std::min(hmn.z, v.z);
                        hmx.x = std::max(hmx.x, v.x); hmx.y = std::max(hmx.y, v.y); hmx.z = std::max(hmx.z, v.z);
                    }
                const Vec3 handExt = hmx - hmn;
                const float palm = std::min(handExt.x, handExt.z);
                Vec3 mn = src->mesh.vertices[0], mx = mn;
                for (const Vec3& v : src->mesh.vertices) {
                    mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
                    mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
                }
                const float maxExt = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
                float uni = maxExt > 1e-4f ? (0.72f * palm / maxExt) : 1.0f;
                if (fpHands && fpHands->itemScale > 0.0f) uni *= fpHands->itemScale;
                Quat irot = Quat::Euler(-16.0f, -side * 18.0f, side * 8.0f);
                if (fpHands) irot = Quat::Euler(fpHands->itemRotation) * irot;
                if (auto* hoScale = held->GetComponent<HoldableObject>()) {
                    if (hoScale->fpScale > 0.0f) uni *= hoScale->fpScale;
                    irot = Quat::Euler(hoScale->fpRotation) * irot;
                }
                Vec3 center{0.0f, 0.0f, 0.0f};
                float minY = 1.0e9f;
                const float invN = 1.0f / (float)src->mesh.vertices.size();
                for (const Vec3& v : src->mesh.vertices) {
                    const Vec3 p = irot * (v * uni);
                    center = center + p * invN;
                    minY = std::min(minY, p.y);
                }
                const float bite = 0.18f * handExt.y;
                Vec3 place{-center.x, hmx.y - bite - minY, -center.z + 0.12f * handExt.z};
                if (auto* ho = held->GetComponent<HoldableObject>()) {
                    const HoldableObject::Grip& grip = base >= B_RUPARM ? ho->leftGrip : ho->rightGrip;
                    if (grip.enabled) {
                        const Vec3 palmPoint{0.0f, hmx.y - bite, 0.12f * handExt.z};
                        place = palmPoint - irot * (grip.position * uni);
                    }
                }
                if (auto* hoPlace = held->GetComponent<HoldableObject>()) place = place + hoPlace->fpOffset;
                if (fpHands) place = place + fpHands->itemOffset;
                m_ownerItem->transform->localScale = Vec3{uni, uni, uni};
                m_ownerItem->transform->localRotation = irot;
                m_ownerItem->transform->localPosition = place;
                m_ownerItem->active = true;
                showItem = true;
            }
        }
    }

    Vec3 camOff = cam->transform->Rotation().Conjugate() * (m_ownerArm->transform->Position() - cam->transform->Position());
    // Every vertex, not the single extreme. A nearer corner projects further
    // than a far one, so the shift is the tightest bound across the cube.
    const float wantNx = side * (0.72f + 0.55f * slide - 0.40f * snap + 0.04f * bobC);
    // Low in the view, with room under the brick for the forearm to reach the
    // bottom edge. Pinning the hand itself on the bottom hid that arm.
    const float wantNy = -0.50f - 0.45f * slide + 0.28f * snap + 0.055f * bobS;
    float shiftX = side > 0.0f ? 1.0e9f : -1.0e9f;
    float shiftY = -1.0e9f;
    bool any = false;
    auto bound = [&](GameObject* g) {
        if (!g) return;
        auto* mr = g->GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled) return;
        for (const Vec3& v : mr->mesh.vertices) {
            const Vec3 c = cam->transform->InverseTransformPoint(g->transform->TransformPoint(v));
            const float nz = std::max(0.05f, -c.z);
            const float limX = wantNx * tanH * nz - c.x;
            const float limY = wantNy * tanV * nz - c.y;
            shiftX = side > 0.0f ? std::min(shiftX, limX) : std::max(shiftX, limX);
            shiftY = std::max(shiftY, limY);
            any = true;
        }
    };
    bound(m_ownerHand);
    if (showItem) bound(m_ownerItem);
    if (fpSide && fpSide->sleeve && !m_fpSecond) bound(m_ownerFore);
    if (any) { camOff.x += shiftX; camOff.y += shiftY; }
    if (fpSide && !m_fpSecond) {
        camOff = camOff + fpSide->position + fpSide->wristOffset;
        camOff.z -= fpSide->reach;
    }
    const float sway = fpHands ? fpHands->swayAmount : 1.0f;
    const Quat lag = Quat::Euler(m_fpTrailPitch * sway, m_fpTrailYaw * sway, 0.0f);
    camOff = lag * camOff;
    m_ownerArm->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * camOff);
    m_ownerArm->transform->SetRotation(cam->transform->Rotation() * lag * camSpace);

    // Nothing may cross the near plane. Push along the look, away from the lens.
    const float keep = std::max(0.18f, near + 0.12f);
    float minAhead = 1.0e9f;
    auto consider = [&](GameObject* g) {
        if (!g || !g->transform) return;
        auto* mr = g->GetComponent<MeshRenderer>();
        if (!mr) return;
        for (const Vec3& v : mr->mesh.vertices) {
            const Vec3 local = cam->transform->InverseTransformPoint(g->transform->TransformPoint(v));
            minAhead = std::min(minAhead, -local.z);
        }
    };
    consider(m_ownerArm);
    consider(m_ownerHand);
    if (fpSide && (fpSide->sleeve || fpSide->forearm) && !m_fpSecond) consider(m_ownerFore);
    if (showItem) consider(m_ownerItem);
    if (minAhead < keep) {
        const Vec3 fwd = basis * Vec3{0.0f, 0.0f, -1.0f};
        m_ownerArm->transform->SetPosition(m_ownerArm->transform->Position() + fwd * (keep - minAhead));
    }
    if (m_ownerFore) PaintFpForearm(m_ownerFore, m_fpSecond ? nullptr : fpSide, lag * camSpace, shirt);
    SetOwnerArmActive(true);
    if (m_ownerItem && !showItem) m_ownerItem->active = false;
    if (m_fpSecond) FinishTwoHands(cam, depth, snap, bobS, bobC);
    else DeactivateOwnerArm2();
    BlendHeldTowardGrip();
}

void Character::FinishTwoHands(Camera* cam, float depth, float snap, float bobS, float bobC) {
    if (!cam || !cam->transform || !m_ownerArm || !m_ownerHand || !gameObject || !gameObject->transform) {
        DeactivateOwnerArm2();
        return;
    }
    const int baseR = B_LUPARM;
    const int baseL = B_RUPARM;
    if (m_ownerArmBase != baseR) EnsureOwnerArm(baseR);
    EnsureOwnerArm2(baseL);
    if (!m_ownerArm || !m_ownerHand || !m_ownerArm2 || !m_ownerHand2 || !m_ownerArm->transform || !m_ownerArm2->transform) {
        DeactivateOwnerArm2();
        return;
    }
    FirstPersonHands* settings = FirstPersonHands::FindOn(gameObject);
    if (settings && !settings->enabled) settings = nullptr;
    const FirstPersonHands::HandSide* right = settings ? &settings->right : nullptr;
    const FirstPersonHands::HandSide* left = settings ? &settings->left : nullptr;
    const float fovScale = FpFovScale(settings, cam);
    const Vec3 root{kFpHandScale * fovScale, kFpHandScale * fovScale, kFpHandScale * fovScale};
    auto prep = [&](GameObject* arm, GameObject* fore, GameObject* hand, int base, const FirstPersonHands::HandSide* side) {
        if (arm && arm->transform) arm->transform->localScale = root;
        if (fore && fore->transform) {
            fore->transform->localPosition = Vec3::Zero;
            fore->transform->localRotation = Quat::Identity;
            fore->transform->localScale = Vec3::One;
        }
        if (hand && hand->transform) {
            hand->transform->localPosition = Vec3::Zero;
            hand->transform->localRotation = Quat::Identity;
            hand->transform->localScale = FpHandScale(side);
        }
        if (base + 2 < (int)m_parts.size()) PaintFpHand(hand, m_parts[base + 2], side);
        PaintFpSleeve(fore, side, shirt);
    };
    prep(m_ownerArm, m_ownerFore, m_ownerHand, baseR, right);
    prep(m_ownerArm2, m_ownerFore2, m_ownerHand2, baseL, left);

    ProceduralObjectHolding* ph = gameObject->GetComponent<ProceduralObjectHolding>();
    GameObject* held = (ph && ph->IsHolding()) ? ph->Held() : nullptr;
    const bool have = held && held->transform && !held->IsDestroyed() && m_ownerItem && m_ownerItem->transform;
    const float sway = settings ? settings->swayAmount : 1.0f;
    const Quat lag = Quat::Euler(m_fpTrailPitch * sway, m_fpTrailYaw * sway, 0.0f);
    const float slide = 1.0f - m_fpShown;

    if (!have) {
        if (m_ownerItem) m_ownerItem->active = false;
        Vec3 off = cam->transform->Rotation().Conjugate() * (m_ownerArm->transform->Position() - cam->transform->Position());
        if (right) off = off + right->position;
        m_ownerArm->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * off);
        if (right) m_ownerArm->transform->SetRotation(m_ownerArm->transform->Rotation() * Quat::Euler(right->rotation));
        Vec3 leftOff{-off.x, off.y, off.z};
        if (left) leftOff = leftOff + left->position;
        const Quat rel = cam->transform->Rotation().Conjugate() * m_ownerArm->transform->Rotation();
        Vec3 e = rel.ToEuler();
        e.y = -e.y;
        e.z = -e.z;
        if (left) e = e + left->rotation;
        m_ownerArm2->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * leftOff);
        m_ownerArm2->transform->SetRotation(cam->transform->Rotation() * Quat::Euler(e));
        m_ownerArm2->active = true;
        if (m_ownerFore2) m_ownerFore2->active = true;
        if (m_ownerHand2) m_ownerHand2->active = true;
        SetOwnerArmActive(true);
        return;
    }

    MeshRenderer* src = held->GetComponent<MeshRenderer>();
    if (!src)
        for (Transform* c : held->transform->Children())
            if (c && c->gameObject)
                if (MeshRenderer* m = c->gameObject->GetComponent<MeshRenderer>()) { src = m; break; }
    auto* dst = m_ownerItem->GetComponent<MeshRenderer>();
    if (!src || !dst || src->mesh.vertices.empty()) {
        m_ownerItem->active = false;
        DeactivateOwnerArm2();
        return;
    }
    dst->mesh = src->mesh;
    dst->color = src->color;
    dst->castShadows = false;
    dst->mesh.normals.clear();
    Vec3 mn = src->mesh.vertices[0], mx = mn;
    for (const Vec3& v : src->mesh.vertices) {
        mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
        mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
    }
    const Vec3 centre = (mn + mx) * 0.5f;
    const Vec3 half = (mx - mn) * 0.5f;
    // The copy uses the prop's own proportions, fitted so the long side is a
    // held object in front of the lens rather than a wall. itemScale still applies.
    const Vec3 lossy = held->transform->LossyScale();
    const Vec3 ext{mx.x - mn.x, mx.y - mn.y, mx.z - mn.z};
    const float worldLong = std::max(std::fabs(lossy.x * ext.x),
                            std::max(std::fabs(lossy.y * ext.y), std::fabs(lossy.z * ext.z)));
    Vec3 scl = worldLong > 1e-4f ? lossy * (0.20f / worldLong) : Vec3{0.20f, 0.20f, 0.20f};
    if (settings && settings->itemScale > 0.0f) scl = scl * settings->itemScale;
    // Tilt the top toward the lens. A face square to the camera sits in shadow.
    Quat irot = Quat::Euler(-36.0f, 12.0f, 0.0f);
    if (settings) irot = Quat::Euler(settings->itemRotation) * irot;
    // Low enough to sit in the hands, high enough that the crate stays in frame
    // while the forearms run off the bottom.
    Vec3 camOff{0.0f, -0.10f - 0.22f * slide + 0.08f * snap + 0.02f * bobS, -std::max(0.36f, depth + 0.02f)};
    camOff.x += 0.02f * bobC;
    if (settings) camOff = camOff + settings->itemOffset;
    camOff = lag * camOff;
    m_ownerItem->transform->SetParent(gameObject->transform, false);
    m_ownerItem->transform->localScale = scl;
    m_ownerItem->transform->SetRotation(cam->transform->Rotation() * lag * irot);
    m_ownerItem->transform->SetPosition(cam->transform->Position() + cam->transform->Rotation() * camOff);
    // Lift the camera-facing side so a crate does not read as a black slab.
    dst->emissive = Color{src->color.r * 0.22f, src->color.g * 0.22f, src->color.b * 0.22f, 1.0f};
    m_ownerItem->active = true;
    held->ownerCameraSkip = OwnerHideTarget() ? OwnerHideTarget() : gameObject;
    m_fpHiddenHeld = held;

    Vec3 imn{1.0e9f, 1.0e9f, 1.0e9f}, imx{-1.0e9f, -1.0e9f, -1.0e9f};
    for (const Vec3& v : dst->mesh.vertices) {
        const Vec3 c = cam->transform->InverseTransformPoint(m_ownerItem->transform->TransformPoint(v));
        imn.x = std::min(imn.x, c.x); imn.y = std::min(imn.y, c.y); imn.z = std::min(imn.z, c.z);
        imx.x = std::max(imx.x, c.x); imx.y = std::max(imx.y, c.y); imx.z = std::max(imx.z, c.z);
    }
    auto* ho = held->GetComponent<HoldableObject>();
    auto placeHand = [&](GameObject* arm, GameObject* hand, bool isLeft, const FirstPersonHands::HandSide* side) {
        // Grip height comes from the item. The hand itself sits on the near
        // left or right edge, outside the prop, broad face back at the lens.
        float gripY = 0.0f;
        if (ho) {
            const HoldableObject::Grip& g = isLeft ? ho->leftGrip : ho->rightGrip;
            const Vec3 gripLocal = g.enabled ? g.position : (centre + Vec3{isLeft ? -half.x : half.x, 0.0f, 0.0f});
            const Vec3 gripCam = cam->transform->InverseTransformPoint(m_ownerItem->transform->TransformPoint(gripLocal));
            gripY = gripCam.y;
        }
        if (gripY < imn.y) gripY = imn.y;
        if (gripY > imx.y) gripY = imx.y;
        float radius = 0.05f * root.x;
        if (hand) if (auto* mr = hand->GetComponent<MeshRenderer>()) if (!mr->mesh.vertices.empty()) {
            float e = 0.0f;
            for (const Vec3& v : mr->mesh.vertices) e = std::max(e, std::max(std::fabs(v.x), std::max(std::fabs(v.y), std::fabs(v.z))));
            const float sy = hand->transform ? std::fabs(hand->transform->localScale.y) : 1.0f;
            radius = e * sy * root.x;
        }
        const float x = isLeft ? (imn.x - radius * 0.65f) : (imx.x + radius * 0.65f);
        const float z = imx.z - 0.01f;   // the near face; camera looks down -Z
        Vec3 pos = cam->transform->TransformPoint(Vec3{x, gripY, z});
        const Vec3 inward = (cam->transform->Rotation() * Vec3{isLeft ? 0.85f : -0.85f, 0.0f, -0.25f}).Normalized();
        const Vec3 face = (cam->transform->Rotation() * Vec3{isLeft ? -0.2f : 0.2f, 0.78f, 0.50f}).Normalized();
        Quat rot = Quat::LookRotation(inward, face);
        if (side) {
            rot = Quat::Euler(side->elbowBend, 0.0f, 0.0f) * Quat::Euler(side->wristRotation)
                * Quat::Euler(side->rotation) * rot;
            pos = pos + cam->transform->Rotation() * (side->position + side->wristOffset + Vec3{0.0f, 0.0f, -side->reach});
        }
        arm->transform->SetRotation(rot);
        arm->transform->SetPosition(pos);
        arm->active = true;
        if (hand && hand->transform && hand->transform->Parent() && hand->transform->Parent()->gameObject)
            PaintFpForearm(hand->transform->Parent()->gameObject, side,
                           cam->transform->Rotation().Conjugate() * rot, shirt);
        (void)snap;
    };
    placeHand(m_ownerArm, m_ownerHand, false, right);
    placeHand(m_ownerArm2, m_ownerHand2, true, left);
    if (m_ownerFore) m_ownerFore->active = true;
    if (m_ownerHand) m_ownerHand->active = true;
    if (m_ownerFore2) m_ownerFore2->active = true;
    if (m_ownerHand2) m_ownerHand2->active = true;
}

} // namespace okay
