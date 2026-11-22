// ActiveRagdoll - see the header. Connects dyn::DynamicCharacter to a scene.
#include "okay/Components/ActiveRagdoll.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/DeathLock.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cctype>
#include <cmath>

namespace okay {

namespace {
using namespace humanoid;

template <typename T> bool Is(Component* c) { return dynamic_cast<T*>(c) != nullptr; }

// Movement that would fight the body while it is physical.
bool Moves(Component* c) {
    return Is<IPlayerSim>(c) || Is<NPCController>(c) || Is<ClickToMoveController>(c) || Is<FreeRoamController>(c)
        || Is<Platformer25DController>(c) || Is<ThirdPersonShooterController>(c) || Is<TopDownController>(c)
        || Is<TopDownShooterController>(c) || Is<Rigidbody3D>(c);
}

bool ArFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

bool ActiveRagdoll::SceneWorld::Cast(const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) const {
    if (!owner || !owner->gameObject) return false;
    Scene* sc = owner->gameObject->scene();
    if (!sc) return false;
    Vec3 d = to - from;
    const float len = d.Magnitude();
    if (len < 1e-6f || !std::isfinite(len)) return false;
    d = d * (1.0f / len);
    for (const RaycastHit3D& h : sc->physics3D().RaycastAll(*sc, from, d, len, nullptr, Physics3D::kAllLayers, false)) {
        if (h.gameObject && owner->Owns(h.gameObject)) continue;
        point = h.point;
        normal = h.normal;
        return true;
    }
    return false;
}

bool ActiveRagdoll::Owns(const GameObject* go) const {
    for (Transform* t = go ? go->transform : nullptr; t; t = t->Parent())
        if (gameObject && t == gameObject->transform) return true;
    return false;
}

void ActiveRagdoll::ApplyTuning() {
    dyn::Tuning& t = m_dc.tuning;
    t.strength = strength; t.armStrength = armStrength; t.legStrength = legStrength;
    t.spineStrength = spineStrength; t.neckStrength = neckStrength;
    t.balanceForce = balanceForce; t.maxStep = maxStep; t.maxSteps = maxSteps; t.fallAngle = fallAngle;
    t.balanceHelp = balanceHelp; t.uprightHelp = uprightHelp;
    t.stumble = stumble; t.brace = brace; t.protectHead = protectHead; t.grab = grab; t.autoGetUp = autoGetUp;
    t.getUpDelay = getUpDelay; t.getUpTime = getUpTime; t.blendTime = blendTime; t.helperForce = helperForce;
    const int m = mode < 0 ? 0 : (mode > 2 ? 2 : mode);
    if ((int)m_dc.mode != m) m_dc.SetMode((dyn::RagdollMode)m);
}

bool ActiveRagdoll::Build() {
    m_built = false;
    for (Transform*& b : m_bones) b = nullptr;
    if (!gameObject || !gameObject->transform) return false;
    m_world.owner = this;
    Transform* bones[BoneCount] = {};
    Character* ch = gameObject->GetComponent<Character>();
    if (ch) {
        if (!ch->driveExternal && !ch->PartsBuilt()) { ch->separateParts = true; ch->BuildParts(); }
        for (int b = 0; b < BoneCount; ++b)
            if (GameObject* g = ch->Part(b)) bones[b] = g->transform;
    }
    if (!bones[Hips] || !bones[Torso]) {
        BoneMap map = MapHumanoidBones(gameObject);
        if (!map.humanoid) return false;
        for (int b = 0; b < BoneCount; ++b) bones[b] = map.bone[b] ? map.bone[b]->transform : nullptr;
    }
    for (int b = 0; b < BoneCount; ++b) if (!bones[b]) return false;

    dyn::RigInput rig;
    for (int b = 0; b < BoneCount; ++b) { rig.pos[b] = bones[b]->Position(); rig.rot[b] = bones[b]->Rotation(); }
    // Facing: the blocky body's shoes stick out forward of the ankle; an
    // imported rig faces its root's forward.
    Vec3 fwd = gameObject->transform->Forward();
    if (ch) {
        for (int foot : {LFoot, RFoot}) {
            auto* mr = bones[foot]->gameObject ? bones[foot]->gameObject->GetComponent<MeshRenderer>() : nullptr;
            if (!mr || mr->mesh.vertices.empty()) continue;
            Vec3 c;
            for (const Vec3& v : mr->mesh.vertices) c += v;
            c = c * (1.0f / (float)mr->mesh.vertices.size());
            Vec3 w = bones[foot]->Rotation() * Vec3::Scale(c, bones[foot]->LossyScale());
            w.y = 0.0f;
            if (w.Magnitude() > 0.01f) { fwd = w.Normalized(); break; }
        }
    }
    rig.forward = fwd;
    const float rootY = gameObject->transform->Position().y;
    const float footY = std::fmin(rig.pos[LFoot].y, rig.pos[RFoot].y);
    rig.groundY = (footY - rootY) >= -0.05f && (footY - rootY) < 0.3f ? rootY : footY - 0.08f;
    rig.mass = mass;
    m_rootAboveGround = rootY - rig.groundY;
    ApplyTuning();
    if (!m_dc.Build(rig)) return false;
    for (int b = 0; b < BoneCount; ++b) {
        m_bones[b] = bones[b];
        m_animLocalPos[b] = bones[b]->localPosition;
        m_animLocalRot[b] = bones[b]->localRotation;
    }
    m_wrote = false;
    m_built = true;
    m_lastState = m_dc.State();
    return true;
}

void ActiveRagdoll::Start() {
    Build();
    if (auto* hp = gameObject ? gameObject->GetComponent<HealthStat>() : nullptr) {
        m_lastHealth = hp->health;
        if (limpOnDeath) hp->keepBodyOnDeath = true;
    }
    m_wasDead = DeathLocksInput(gameObject);
}

void ActiveRagdoll::OnDisable() {
    if (m_handedOver) HandOver(false);
}

bool ActiveRagdoll::IsDown() const {
    const dyn::CharState s = m_dc.State();
    return s == dyn::CharState::Down || s == dyn::CharState::GettingUp || s == dyn::CharState::Dead
        || s == dyn::CharState::Falling;
}

void ActiveRagdoll::HandOver(bool physical) {
    if (!handOverControl && physical) return;
    if (physical == m_handedOver) return;
    if (physical) {
        m_switched.clear();
        for (Component* c : gameObject->GetComponents<Component>()) {
            if (c == this || !c->enabled || !Moves(c)) continue;
            if (auto* rb = dynamic_cast<Rigidbody3D*>(c)) rb->velocity = Vec3::Zero;
            c->enabled = false;
            m_switched.push_back(c);
        }
    } else {
        for (Component* c : m_switched) c->enabled = true;
        m_switched.clear();
    }
    m_handedOver = physical;
}

void ActiveRagdoll::ReadAnimation(Vec3* pos, Quat* rot) {
    // A bone the animation did not touch since our write still holds our pose:
    // put its last animated value back so the pose does not feed on itself.
    for (int b = 0; b < BoneCount; ++b) {
        Transform* t = m_bones[b];
        if (m_wrote && t->localPosition == m_wroteLocalPos[b] && Quat::Dot(t->localRotation, m_wroteLocalRot[b]) > 0.9999999f) {
            t->localPosition = m_animLocalPos[b];
            t->localRotation = m_animLocalRot[b];
        } else {
            m_animLocalPos[b] = t->localPosition;
            m_animLocalRot[b] = t->localRotation;
        }
    }
    for (int b = 0; b < BoneCount; ++b) { pos[b] = m_bones[b]->Position(); rot[b] = m_bones[b]->Rotation(); }
}

void ActiveRagdoll::WritePose() {
    // Parents first (the enum order), rotations only below the hips so bone
    // lengths stay exact; the hips also take the position.
    for (int b = 0; b < BoneCount; ++b) {
        Vec3 p; Quat q;
        m_dc.BonePose(b, p, q);
        if (b == Hips) m_bones[b]->SetPosition(p);
        m_bones[b]->SetRotation(q);
    }
    for (int b = 0; b < BoneCount; ++b) {
        m_wroteLocalPos[b] = m_bones[b]->localPosition;
        m_wroteLocalRot[b] = m_bones[b]->localRotation;
    }
    m_wrote = true;
}

void ActiveRagdoll::LateUpdate(float dt) {
    if (!m_built || dt <= 0.0f || !std::isfinite(dt)) return;
    ApplyTuning();
    WatchHealth();
    Transform* root = gameObject->transform;
    Vec3 delta, fwd;
    float ground = 0.0f;
    if (m_dc.RootMoveWanted(delta, fwd, &ground)) {
        // Put the root under the body (on the ground it stands on now) before
        // the animation is read again.
        if (ArFinite(delta)) {
            Vec3 p = root->Position() + delta;
            if (std::isfinite(ground)) p.y = ground + m_rootAboveGround;
            root->SetPosition(p);
        }
        const Vec3 have = m_dc.AnimForward();
        const float ang = std::atan2(Vec3::Dot(Vec3::Cross(have, fwd), Vec3::Up), Vec3::Dot(have, fwd));
        if (std::isfinite(ang) && std::fabs(ang) > 1e-4f) {
            const Vec3 pivot = m_bones[Hips]->Position();
            const Quat yaw = Quat::AngleAxis(ang * Mathf::Rad2Deg, Vec3::Up);
            const Vec3 off = root->Position() - pivot;
            root->SetRotation((yaw * root->Rotation()).Normalized());
            root->SetPosition(Vec3{pivot.x, root->Position().y, pivot.z} + yaw * Vec3{off.x, 0.0f, off.z});
        }
        m_dc.RootMoved();
    }
    Vec3 pos[BoneCount]; Quat rot[BoneCount];
    ReadAnimation(pos, rot);
    m_dc.SetAnimationPose(pos, rot);
    m_dc.Tick(dt, &m_world);
    const dyn::CharState s = m_dc.State();
    if (s != m_lastState) {
        const dyn::CharState was = m_lastState;
        m_lastState = s;
        if (onStateChanged) onStateChanged(was, s);
    }
    HandOver(s != dyn::CharState::Animated && s != dyn::CharState::Blending);
    if (rootFollows && (s == dyn::CharState::Down || s == dyn::CharState::GettingUp || s == dyn::CharState::Dead)) {
        // Keep the object (camera targets, name tags) over the body; the
        // physics pose is written in world space below, so it does not move.
        const Vec3 hips = m_dc.Body(Hips).position;
        const Vec3 r = root->Position();
        if (ArFinite(hips)) root->SetPosition(Vec3{hips.x, r.y, hips.z});
    }
    if (m_dc.PhysicsWeight() > 0.0f) WritePose();
    else m_wrote = false;
}

void ActiveRagdoll::WatchHealth() {
    auto* hp = gameObject->GetComponent<HealthStat>();
    const bool dead = DeathLocksInput(gameObject);
    if (dead && !m_wasDead && limpOnDeath) {
        Vec3 from;
        if (hp && hp->LastHitFrom(from)) {
            const Vec3 chest = m_dc.Body(Torso).position;
            Vec3 away = chest - from;
            away.y = 0.0f;
            if (away.SqrMagnitude() > 1e-6f) m_dc.Push(chest, away.Normalized() * (0.6f * mass));
        }
        m_dc.Die();
    } else if (!dead && m_wasDead && m_dc.Dead()) {
        m_dc.Revive();
    } else if (hp && !dead && reactToDamage && m_lastHealth >= 0.0f && hp->health < m_lastHealth - 1e-4f) {
        const float dmg = m_lastHealth - hp->health;
        Vec3 from, at = m_dc.Body(Torso).position;
        Vec3 dir = gameObject->transform->Forward() * -1.0f;
        if (hp->LastHitFrom(from)) {
            Vec3 hit;
            Vec3 d = at - from;
            if (d.SqrMagnitude() > 1e-8f) {
                d = d.Normalized();
                if (m_dc.Raycast(from, d, 1000.0f, &hit) >= 0) at = hit;
                dir = d;
            }
        }
        const float imp = std::fmin(dmg * damageImpulse, 4.0f * mass);
        m_dc.Hit(at, dir * imp);
    }
    m_wasDead = dead;
    m_lastHealth = hp ? hp->health : -1.0f;
}

void ActiveRagdoll::Push(const Vec3& impulse) {
    if (!m_built) return;
    m_dc.Push(m_dc.Body(Torso).position, impulse);
}

void ActiveRagdoll::PushAt(const Vec3& point, const Vec3& impulse) { if (m_built) m_dc.Push(point, impulse); }
void ActiveRagdoll::Hit(const Vec3& point, const Vec3& impulse) { if (m_built) m_dc.Hit(point, impulse); }

bool ActiveRagdoll::Shoot(const Vec3& origin, const Vec3& direction, float power, float maxDistance) {
    if (!m_built) return false;
    Vec3 hit;
    if (m_dc.Raycast(origin, direction, maxDistance, &hit) < 0) return false;
    m_dc.Shot(hit, direction, power);
    return true;
}

void ActiveRagdoll::Explode(const Vec3& centre, float force, float radius) {
    if (m_built) m_dc.Explosion(centre, force, radius);
}

void ActiveRagdoll::SetMode(int m) {
    mode = m < 0 ? 0 : (m > 2 ? 2 : m);
    if (m_built) m_dc.SetMode((dyn::RagdollMode)mode);
}

bool ActiveRagdoll::Trigger(const std::string& raw) {
    if (!m_built) return false;
    std::string n;
    for (char c : raw) if (c != ' ' && c != '_' && c != '-') n += (char)std::tolower((unsigned char)c);
    if (n == "knockdown" || n == "fall" || n == "trip") { m_dc.Knockdown(); return true; }
    if (n == "getup" || n == "standup") { m_dc.GetUp(); return true; }
    if (n == "die" || n == "kill") { m_dc.Die(); return true; }
    if (n == "revive") { m_dc.Revive(); return true; }
    if (n == "limp" || n == "ragdoll") { SetMode(2); return true; }
    if (n == "powered" || n == "active") { SetMode(1); return true; }
    if (n == "reactive" || n == "animated") { SetMode(0); return true; }
    if (n == "stumble" || n == "push") { Push(Vec3::Scale(gameObject->transform->Forward(), Vec3{1, 0, 1}) * (-0.8f * mass)); return true; }
    return false;
}

} // namespace okay
