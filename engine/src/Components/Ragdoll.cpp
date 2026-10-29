#include "okay/Components/Ragdoll.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/DeathLock.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/NPCController.hpp"
#include <cmath>

namespace okay {

namespace {
constexpr float kStep = 1.0f / 60.0f;
constexpr int   kMaxSteps = 4;
using namespace humanoid;

// The particle a bone's direction is measured from (-1 for the hips).
constexpr int kParentBone[BoneCount] = {
    -1, Hips, Torso,
    Torso, LUpperArm, LForeArm,
    Torso, RUpperArm, RForeArm,
    Hips, LThigh, LShin,
    Hips, RThigh, RShin,
};
// The bone a bone points at; -1 means a tip particle past its end.
constexpr int kChildBone[BoneCount] = {
    Torso, Head, -1,
    LForeArm, LHand, -1,
    RForeArm, RHand, -1,
    LShin, LFoot, -1,
    RShin, RFoot, -1,
};

template <typename T> bool Is(Component* c) { return dynamic_cast<T*>(c) != nullptr; }

bool Animates(Component* c) {
    return Is<Character>(c) || Is<Animator>(c) || Is<ModelAnimator>(c) || Is<AnimStateMachine>(c)
        || Is<AimIK>(c) || Is<ChainIK>(c) || Is<FootIK>(c) || Is<FullBodyIK>(c) || Is<LimbIK>(c)
        || Is<LookAtIK>(c) || Is<HumanoidRetarget>(c) || Is<IPlayerSim>(c) || Is<NPCController>(c)
        || Is<Rigidbody3D>(c);
}

void CollectTree(Transform* t, std::vector<Transform*>& out) {
    if (!t) return;
    out.push_back(t);
    for (Transform* c : t->Children()) CollectTree(c, out);
}
} // namespace

bool Ragdoll::Build() {
    m_built = false;
    m_p.clear(); m_links.clear(); m_childOf.clear(); m_restDir.clear();
    m_restSide.clear(); m_sideOf.clear();
    for (int& i : m_boneParticle) i = -1;
    if (!gameObject) return false;

    Transform* bones[BoneCount] = {};
    if (auto* ch = gameObject->GetComponent<Character>()) {
        // The baked body is one mesh: split it into its part rig to move the bones.
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

    for (int b = 0; b < BoneCount; ++b) {
        m_boneParticle[b] = (int)m_p.size();
        Particle p; p.bone = bones[b]; p.pos = p.prev = bones[b]->Position();
        m_p.push_back(p);
    }
    const Vec3 up = (m_p[Torso].pos - m_p[Hips].pos).Normalized();
    const float torsoLen = Vec3::Distance(m_p[Torso].pos, m_p[Hips].pos);
    const Vec3 fwd = gameObject->transform ? gameObject->transform->Forward() : Vec3::Forward;

    // Tips past the head, hands and feet, kept as a bone-local offset.
    m_childOf.assign(BoneCount, -1);
    for (int b = 0; b < BoneCount; ++b) {
        if (kChildBone[b] >= 0) { m_childOf[b] = kChildBone[b]; continue; }
        const Vec3 from = m_p[kParentBone[b]].pos, at = m_p[b].pos;
        Vec3 off;
        if (b == Head)                 off = up * (0.6f * torsoLen);
        else if (b == LFoot || b == RFoot) off = fwd * (0.45f * Vec3::Distance(from, at));
        else                           off = (at - from).Normalized() * (0.4f * Vec3::Distance(from, at));
        Particle tip; tip.bone = bones[b];
        tip.offset = bones[b]->Rotation().Inverse() * off;
        tip.pos = tip.prev = at + off;
        m_childOf[b] = (int)m_p.size();
        m_p.push_back(tip);
    }

    auto link = [&](int a, int b, bool minOnly = false, float scale = 1.0f) {
        m_links.push_back({a, b, Vec3::Distance(m_p[a].pos, m_p[b].pos) * scale, minOnly});
    };
    for (int b = 0; b < BoneCount; ++b) {
        if (kParentBone[b] >= 0) link(kParentBone[b], b);
        if (kChildBone[b] < 0) link(b, m_childOf[b]);
    }
    // A rigid torso: hips, chest, shoulders and thighs braced together.
    const int box[] = {Hips, Torso, LUpperArm, RUpperArm, LThigh, RThigh};
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            if (!(box[i] == Hips && box[j] == Torso)) link(box[i], box[j]);
    // A stiff neck, and the hand / foot tips held to their limb.
    link(Head, LUpperArm); link(Head, RUpperArm);
    link(m_childOf[Head], Torso);
    link(m_childOf[LHand], LForeArm); link(m_childOf[RHand], RForeArm);
    link(m_childOf[LFoot], LShin);    link(m_childOf[RFoot], RShin);
    // Elbows and knees bend but don't fold flat.
    link(LUpperArm, LHand, true, 0.45f); link(RUpperArm, RHand, true, 0.45f);
    link(LThigh, LFoot, true, 0.6f);     link(RThigh, RFoot, true, 0.6f);
    link(m_childOf[Head], Hips, true, 0.85f);

    // Twist references for the hips (thighs) and chest (shoulders).
    m_sideOf.assign(BoneCount * 2, -1);
    m_sideOf[Hips * 2] = LThigh;  m_sideOf[Hips * 2 + 1] = RThigh;
    m_sideOf[Torso * 2] = LUpperArm; m_sideOf[Torso * 2 + 1] = RUpperArm;
    m_sideOf[Head * 2] = LUpperArm;  m_sideOf[Head * 2 + 1] = RUpperArm;
    m_restDir.assign(BoneCount, Vec3::Up);
    m_restSide.assign(BoneCount, Vec3::Right);
    for (int b = 0; b < BoneCount; ++b) {
        const Quat inv = bones[b]->Rotation().Inverse();
        m_restDir[b] = inv * (m_p[m_childOf[b]].pos - m_p[b].pos).Normalized();
        if (m_sideOf[b * 2] >= 0)
            m_restSide[b] = inv * (m_p[m_sideOf[b * 2 + 1]].pos - m_p[m_sideOf[b * 2]].pos).Normalized();
    }
    m_built = true;
    return true;
}

void Ragdoll::CapturePose() {
    m_rest.clear();
    std::vector<Transform*> all;
    for (const Particle& p : m_p) if (p.bone && p.offset.SqrMagnitude() == 0.0f) all.push_back(p.bone);
    for (Transform* t : all) m_rest.push_back({t, t->localPosition, t->localRotation});
}

void Ragdoll::SampleBones(bool keepVelocity) {
    for (Particle& p : m_p) {
        if (!p.bone) continue;
        const Vec3 now = p.bone->Position() + p.bone->Rotation() * p.offset;
        p.prev = keepVelocity ? p.pos : now;
        p.pos = now;
    }
}

bool Ragdoll::Owns(const GameObject* go) const {
    for (Transform* t = go ? go->transform : nullptr; t; t = t->Parent())
        if (gameObject && t == gameObject->transform) return true;
    return false;
}

void Ragdoll::Collide(Particle& p, const Vec3& from) {
    p.contact = false;
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return;
    // Cast a little below the move too, so a body resting on a floor keeps its contact.
    Vec3 d = p.pos - from;
    float len = d.Magnitude();
    d = len > 1e-6f ? d * (1.0f / len) : Vec3::Down;
    for (const RaycastHit3D& h : sc->physics3D().RaycastAll(*sc, from, d, len + radius * 1.5f, nullptr,
                                                             Physics3D::kAllLayers, false)) {
        if (h.gameObject && Owns(h.gameObject)) continue;
        p.contact = true;
        p.planePoint = h.point;
        p.planeNormal = h.normal;
        return;
    }
}

void Ragdoll::Push(Particle& p) const {
    if (!p.contact) return;
    const float depth = radius - Vec3::Dot(p.pos - p.planePoint, p.planeNormal);
    if (depth > 0.0f) p.pos = p.pos + p.planeNormal * depth;
}

void Ragdoll::Rub(Particle& p) const {
    if (!p.contact || Vec3::Dot(p.pos - p.planePoint, p.planeNormal) > radius * 1.5f) return;
    // Stop the speed into the surface and scrub the slide; slow slides stick.
    const Vec3 v = p.pos - p.prev;
    const Vec3 vn = p.planeNormal * Vec3::Dot(v, p.planeNormal);
    Vec3 vt = v - vn;
    vt = vt.Magnitude() < 0.05f * kStep ? Vec3::Zero : vt * (1.0f - Mathf::Clamp01(friction));
    p.prev = p.pos - vt - (Vec3::Dot(v, p.planeNormal) > 0.0f ? vn : Vec3::Zero);
}

void Ragdoll::Step(float h, float gravityScale) {
    const Vec3 g{0.0f, -gravity * gravityScale * h * h, 0.0f};
    const float keep = 1.0f - Mathf::Clamp01(damping);
    const float vmax = maxSpeed * h;
    std::vector<Vec3> start(m_p.size());
    for (std::size_t i = 0; i < m_p.size(); ++i) {
        Particle& p = m_p[i];
        start[i] = p.pos;
        const Vec3 v = Vec3::ClampMagnitude((p.pos - p.prev) * keep, vmax);
        p.prev = p.pos;
        p.pos = p.pos + v + g;
    }
    for (std::size_t i = 0; i < m_p.size(); ++i) Collide(m_p[i], start[i]);
    for (int it = 0; it < Mathf::Max(1, iterations); ++it) {
        for (const Link& l : m_links) {
            Particle& a = m_p[l.a];
            Particle& b = m_p[l.b];
            const Vec3 d = b.pos - a.pos;
            const float dist = d.Magnitude();
            if (dist < 1e-6f || (l.minOnly && dist >= l.len)) continue;
            const Vec3 fix = d * (0.5f * (dist - l.len) / dist);
            a.pos = a.pos + fix;
            b.pos = b.pos - fix;
        }
        for (Particle& p : m_p) Push(p);
    }
    for (Particle& p : m_p) Rub(p);
}

void Ragdoll::PoseBones() {
    for (int b = 0; b < BoneCount; ++b) {
        Transform* t = m_p[b].bone;
        if (!t) continue;
        const Vec3 target = m_p[m_childOf[b]].pos - m_p[b].pos;
        if (target.SqrMagnitude() < 1e-10f) continue;
        Quat q = t->Rotation();
        q = (Quat::FromToRotation(q * m_restDir[b], target) * q).Normalized();
        if (m_sideOf[b * 2] >= 0) {
            const Vec3 axis = target.Normalized();
            const Vec3 have = Vec3::ProjectOnPlane(q * m_restSide[b], axis);
            const Vec3 want = Vec3::ProjectOnPlane(m_p[m_sideOf[b * 2 + 1]].pos - m_p[m_sideOf[b * 2]].pos, axis);
            if (have.SqrMagnitude() > 1e-8f && want.SqrMagnitude() > 1e-8f)
                q = (Quat::FromToRotation(have, want) * q).Normalized();
        }
        t->SetRotation(q);
        t->SetPosition(m_p[b].pos);
    }
}

void Ragdoll::SetDriven(bool limp) {
    if (limp) {
        m_switched.clear();
        std::vector<Transform*> tree;
        CollectTree(gameObject ? gameObject->transform : nullptr, tree);
        for (Transform* t : tree) {
            if (!t->gameObject) continue;
            for (Component* c : t->gameObject->GetComponents<Component>()) {
                if (c == this || !c->enabled || !Animates(c)) continue;
                if (auto* rb = dynamic_cast<Rigidbody3D*>(c)) rb->velocity = Vec3::Zero;
                c->enabled = false;
                m_switched.push_back(c);
            }
        }
    } else {
        for (Component* c : m_switched) c->enabled = true;
        m_switched.clear();
    }
}

bool Ragdoll::Activate() {
    if (m_active) return true;
    if (!m_built && !Build()) return false;
    if (!m_hadPrev) SampleBones(false);
    else {
        // Bones moved by last frame's animation: keep that motion, rescaled to one step.
        const float scale = m_lastDt > 1e-5f ? kStep / m_lastDt : 1.0f;
        for (Particle& p : m_p) p.prev = p.pos - (p.pos - p.prev) * scale;
    }
    CapturePose();
    SetDriven(true);
    m_active = true;
    m_react = 0.0f;
    m_accum = 0.0f;
    return true;
}

void Ragdoll::Deactivate() {
    if (!m_active) return;
    m_active = false;
    for (const Pose& r : m_rest) { r.t->localPosition = r.lpos; r.t->localRotation = r.lrot; }
    SetDriven(false);
    m_hadPrev = false;
}

int Ragdoll::Nearest(const Vec3& point) const {
    int best = -1; float bd = 1e30f;
    for (int i = 0; i < (int)m_p.size(); ++i) {
        const float d = (m_p[i].pos - point).SqrMagnitude();
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void Ragdoll::Hit(const Vec3& point, const Vec3& velocity) {
    if (!m_built && !Build()) return;
    if (!m_active) {
        if (!hitReactions) return;
        if (m_react <= 0.0f) SampleBones(false);
        m_react = Mathf::Max(0.01f, hitRecover);
    }
    const int i = Nearest(point);
    if (i < 0) return;
    // The struck particle takes the full push, its neighbours half.
    m_p[i].prev = m_p[i].prev - velocity * kStep;
    for (const Link& l : m_links) {
        if (l.minOnly) continue;
        const int o = l.a == i ? l.b : (l.b == i ? l.a : -1);
        if (o >= 0) m_p[o].prev = m_p[o].prev - velocity * (0.5f * kStep);
    }
}

bool Ragdoll::IsSettled(float speed) const {
    const float lim = speed * kStep;
    for (const Particle& p : m_p)
        if ((p.pos - p.prev).SqrMagnitude() > lim * lim) return false;
    return true;
}

void Ragdoll::WatchHealth() {
    if (!gameObject) return;
    // HealthStat, or a script die() on a body without one.
    auto* hp = gameObject->GetComponent<HealthStat>();
    const bool dead = DeathLocksInput(gameObject);
    if (dead && !m_wasDead && ragdollOnDeath && Activate()) {
        Vec3 from;
        Vec3 away = hp && hp->LastHitFrom(from) ? Vec3::ProjectOnPlane(m_p[Hips].pos - from, Vec3::Up) : Vec3::Zero;
        if (away.SqrMagnitude() < 1e-6f)
            away = gameObject->transform ? gameObject->transform->Forward() * -1.0f : Vec3::Back;
        away = away.Normalized();
        for (Particle& p : m_p) p.prev = p.prev - away * (0.3f * deathImpulse * kStep);
        Hit(m_p[Torso].pos, away * deathImpulse);
    } else if (!dead && m_wasDead && m_active) {
        Deactivate();
    } else if (hp && !dead && m_lastHealth >= 0.0f && hp->health < m_lastHealth - 1e-4f && !m_active) {
        int at = Torso;
        Vec3 dir = gameObject->transform ? gameObject->transform->Forward() * -1.0f : Vec3::Back;
        Vec3 from;
        if (hp->LastHitFrom(from) && m_built) {
            at = Nearest(from);
            dir = (m_p[at].pos - from).Normalized();
        }
        if (m_built || Build()) Hit(m_p[at].pos, dir * hitImpulse);
    }
    m_wasDead = dead;
    m_lastHealth = hp ? hp->health : -1.0f;
}

void Ragdoll::Start() {
    Build();
    m_wasDead = DeathLocksInput(gameObject);
    if (auto* hp = gameObject ? gameObject->GetComponent<HealthStat>() : nullptr) {
        m_lastHealth = hp->health;
        hp->keepBodyOnDeath = ragdollOnDeath;
    }
}

void Ragdoll::Update(float) { WatchHealth(); }

void Ragdoll::LateUpdate(float dt) {
    if (!m_built || dt <= 0.0f) return;
    m_lastDt = dt;
    if (!m_active && m_react <= 0.0f) {
        // Track the animated bones so a ragdoll keeps the body's motion.
        SampleBones(m_hadPrev);
        m_hadPrev = true;
        return;
    }
    if (!m_active) {
        // Flinch: simulate without gravity, then spring back to the animation.
        std::vector<Vec3> anim(m_p.size());
        for (std::size_t i = 0; i < m_p.size(); ++i)
            anim[i] = m_p[i].bone->Position() + m_p[i].bone->Rotation() * m_p[i].offset;
        m_react -= dt;
        const float t = Mathf::Clamp01(1.0f - m_react / Mathf::Max(0.01f, hitRecover));
        Step(Mathf::Min(dt, kStep), 0.0f);
        const float pull = 0.2f + 0.8f * t;
        for (std::size_t i = 0; i < m_p.size(); ++i) {
            const Vec3 moved = Vec3::Lerp(m_p[i].pos, anim[i], pull);
            m_p[i].prev = m_p[i].prev + (moved - m_p[i].pos);
            m_p[i].pos = moved;
        }
        m_p[Hips].pos = m_p[Hips].prev = anim[Hips];
        if (m_react <= 0.0f) { m_react = 0.0f; m_hadPrev = false; return; }
        PoseBones();
        return;
    }
    m_accum = Mathf::Min(m_accum + dt, kStep * kMaxSteps);
    while (m_accum >= kStep) { Step(kStep, 1.0f); m_accum -= kStep; }
    PoseBones();
}

} // namespace okay
