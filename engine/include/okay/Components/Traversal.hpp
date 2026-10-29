#pragma once
// Vault, mantle and ledge-grab motion. The clips live in HumanoidActions.
// canVault / canMantle / canLedgeGrab on the controllers default off, so
// walking into a wall does nothing until a game opts in. vault() still probes.
// Limb IK for hands-on-ledge is not wired (that component is owned elsewhere).
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <unordered_map>
#include <vector>

namespace okay {

enum class TraverseKind { None, Vault, MantleLow, MantleHigh, Ledge };

struct TraverseProbe {
    TraverseKind kind = TraverseKind::None;
    Vec3 from{}, to{};
    Vec3 lip{};          ///< stand-on-top point, kept when `to` is a hang
    float height = 0.0f;
    float duration = 0.5f;
};

struct TraverseJob {
    Vec3 from{}, to{};
    float t = 0.0f;
    float dur = 0.5f;
    float gravity = 1.0f;
    bool hadGravity = false;
    std::vector<Collider3D*> cols;
    std::vector<bool> wasEnabled;
    bool rememberLip = false;
    Vec3 lip{};
};

inline std::unordered_map<GameObject*, TraverseJob>& TraverseJobs() {
    static std::unordered_map<GameObject*, TraverseJob> jobs;
    return jobs;
}

inline bool TraverseBusy(GameObject* go) { return go && TraverseJobs().count(go) != 0; }

inline std::unordered_map<GameObject*, Vec3>& HangPoints() {
    static std::unordered_map<GameObject*, Vec3> pts;
    return pts;
}

/// Gravity to put back when a hang ends. The ledge move leaves gravity at 0.
inline std::unordered_map<GameObject*, float>& HangGravity() {
    static std::unordered_map<GameObject*, float> g;
    return g;
}

inline void RestoreHangGravity(GameObject* go) {
    if (!go) return;
    auto& m = HangGravity();
    auto it = m.find(go);
    if (it == m.end()) return;
    if (auto* rb = go->GetComponent<Rigidbody3D>()) rb->gravityScale = it->second;
    m.erase(it);
}

inline TraverseProbe ProbeTraverse(GameObject* go, const Vec3& fwd, bool airborne) {
    TraverseProbe p;
    if (!go || !go->transform || !go->scene()) return p;
    Scene& sc = *go->scene();
    const Vec3 feet = go->transform->Position();
    Vec3 f = fwd; f.y = 0.0f;
    const float fl = f.Magnitude();
    if (fl < 1e-4f) return p;
    f = f * (1.0f / fl);
    auto ray = [&](const Vec3& o, const Vec3& d, float dist) {
        return sc.physics3D().Raycast(sc, o, d, dist, go, Physics3D::kAllLayers, false);
    };
    // A face in front, then the lip above it.
    const RaycastHit3D wall = ray(feet + Vec3{0, 0.4f, 0}, f, 0.85f);
    if (!wall.hit) return p;
    const RaycastHit3D down = ray(wall.point + f * 0.25f + Vec3{0, 2.2f, 0}, Vec3{0, -1, 0}, 2.4f);
    if (!down.hit) return p;
    p.height = down.point.y - feet.y;
    if (p.height < 0.25f || p.height > 2.05f) return p;
    const RaycastHit3D across = ray(down.point + Vec3{0, 0.08f, 0}, f, 0.9f);
    const bool thin = !across.hit || across.distance > 0.55f;
    const Vec3 stand = down.point + f * (thin ? 0.75f : 0.35f);
    // The landing needs a floor and clear space at chest height.
    const RaycastHit3D floor = ray(stand + Vec3{0, 1.2f, 0}, Vec3{0, -1, 0}, 1.6f);
    const RaycastHit3D blocked = ray(stand + Vec3{0, 0.9f, 0}, f, 0.35f);
    if (!floor.hit || blocked.hit) return p;
    p.from = feet;
    p.to = Vec3{stand.x, floor.point.y, stand.z};
    p.lip = p.to;
    if (p.height < 0.6f) { p.kind = TraverseKind::MantleLow; p.duration = 0.5f; }
    else if (p.height < 1.1f && thin) { p.kind = TraverseKind::Vault; p.duration = 0.6f; }
    else if (p.height < 1.1f) { p.kind = TraverseKind::MantleLow; p.duration = 0.55f; }
    else if (airborne) {
        p.kind = TraverseKind::Ledge; p.duration = 0.4f;
        p.to = down.point - f * 0.25f + Vec3{0, -1.05f, 0};
    }
    else { p.kind = TraverseKind::MantleHigh; p.duration = 0.8f; }
    return p;
}

inline void BeginTraverse(GameObject* go, const TraverseProbe& p) {
    if (!go || p.kind == TraverseKind::None) return;
    TraverseJob job;
    job.from = p.from; job.to = p.to; job.dur = p.duration;
    if (auto* rb = go->GetComponent<Rigidbody3D>()) {
        job.hadGravity = true; job.gravity = rb->gravityScale; rb->gravityScale = 0.0f;
        rb->velocity = Vec3{0, 0, 0};
    }
    // A climb out of a hang should restore the gravity from before the grab,
    // not the 0 the hang left on the body.
    if (auto git = HangGravity().find(go); git != HangGravity().end()) {
        job.gravity = git->second; job.hadGravity = true; HangGravity().erase(git);
    }
    for (Collider3D* c : go->GetComponents<Collider3D>()) {
        job.cols.push_back(c); job.wasEnabled.push_back(c->enabled); c->enabled = false;
    }
    TraverseJobs()[go] = std::move(job);
}

inline void TickTraverse(GameObject* go, float dt) {
    auto& jobs = TraverseJobs();
    auto it = jobs.find(go);
    if (it == jobs.end()) return;
    TraverseJob& j = it->second;
    j.t += dt / (j.dur > 0.05f ? j.dur : 0.05f);
    const float u = j.t < 0.0f ? 0.0f : (j.t > 1.0f ? 1.0f : j.t);
    const float s = u * u * (3.0f - 2.0f * u);
    // A small hop so the body clears the lip. Visual only; the clip plays too.
    Vec3 pos = Vec3::Lerp(j.from, j.to, s);
    pos.y += 0.35f * 4.0f * s * (1.0f - s);
    if (go->transform) go->transform->SetPosition(pos);
    if (auto* rb = go->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
    if (j.t < 1.0f) return;
    if (go->transform) go->transform->SetPosition(j.to);
    if (j.rememberLip) {
        HangPoints()[go] = j.lip;
        if (j.hadGravity) HangGravity()[go] = j.gravity;   // stay weightless on the lip
    } else if (auto* rb = go->GetComponent<Rigidbody3D>()) {
        if (j.hadGravity) rb->gravityScale = j.gravity;
    }
    if (auto* rb = go->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
    for (std::size_t i = 0; i < j.cols.size(); ++i)
        if (j.cols[i]) j.cols[i]->enabled = j.wasEnabled[i];
    jobs.erase(it);
}

/// Stop a move and put the colliders back. A hang keeps its saved gravity.
inline void AbortTraverse(GameObject* go) {
    if (!go) return;
    auto& jobs = TraverseJobs();
    auto it = jobs.find(go);
    if (it == jobs.end()) return;
    TraverseJob& j = it->second;
    if (j.rememberLip && j.hadGravity) HangGravity()[go] = j.gravity;
    else if (j.hadGravity) {
        if (auto* rb = go->GetComponent<Rigidbody3D>()) rb->gravityScale = j.gravity;
    }
    for (std::size_t i = 0; i < j.cols.size(); ++i)
        if (j.cols[i]) j.cols[i]->enabled = j.wasEnabled[i];
    jobs.erase(it);
}

} // namespace okay
