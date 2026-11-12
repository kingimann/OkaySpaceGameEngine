#include "okay/Components/Volume.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Components/NoCode.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace okay {

// ===========================================================================
// VolumeGeometry
// ===========================================================================

const char* VolumeGeometry::ShapeName(VolumeShape s) {
    switch (s) {
        case VolumeShape::Box: return "Box";
        case VolumeShape::Sphere: return "Sphere";
        case VolumeShape::Cylinder: return "Cylinder";
    }
    return "Box";
}

bool VolumeGeometry::Valid() const {
    return std::isfinite(size.x) && std::isfinite(size.y) && std::isfinite(size.z) &&
           size.x > 0.0f && size.y > 0.0f && size.z > 0.0f;
}

Vec3 VolumeGeometry::ToUnit(const Transform& t, const Vec3& world) const {
    const Vec3 local = t.InverseTransformPoint(world);
    constexpr float inf = std::numeric_limits<float>::infinity();
    auto axis = [](float l, float o, float s) {
        if (!(s > 1e-6f) || !std::isfinite(s)) return inf;
        return (l - o) / (s * 0.5f);
    };
    return {axis(local.x, offset.x, size.x), axis(local.y, offset.y, size.y), axis(local.z, offset.z, size.z)};
}

float VolumeGeometry::Depth(const Vec3& u) const {
    if (!std::isfinite(u.x) || !std::isfinite(u.y) || !std::isfinite(u.z)) return -1.0f;
    switch (shape) {
        case VolumeShape::Box:
            return 1.0f - std::max({std::fabs(u.x), std::fabs(u.y), std::fabs(u.z)});
        case VolumeShape::Sphere:
            return 1.0f - std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        case VolumeShape::Cylinder:
            return std::min(1.0f - std::fabs(u.y), 1.0f - std::sqrt(u.x * u.x + u.z * u.z));
    }
    return -1.0f;
}

float VolumeGeometry::DragFace(int axis, int sign, float delta) {
    if (axis < 0 || axis > 2 || (sign != -1 && sign != 1)) return 0.0f;
    float* sz = axis == 0 ? &size.x : axis == 1 ? &size.y : &size.z;
    float* off = axis == 0 ? &offset.x : axis == 1 ? &offset.y : &offset.z;
    // Moving the plus face by +d grows the size by d; moving the minus face by +d shrinks it.
    float grow = (float)sign * delta;
    if (*sz + grow < kMinSize) grow = kMinSize - *sz;
    *sz += grow;
    const float applied = (float)sign * grow;   // along +axis
    *off += applied * 0.5f;
    return applied;
}

void VolumeGeometry::FromGroundDrag(const Vec3& a, const Vec3& b, float height, Vec3& outPosition, Vec3& outSize) {
    const float w = std::max(std::fabs(b.x - a.x), kMinSize);
    const float d = std::max(std::fabs(b.z - a.z), kMinSize);
    const float h = std::max(height, kMinSize);
    outSize = {w, h, d};
    outPosition = {(a.x + b.x) * 0.5f, a.y + h * 0.5f, (a.z + b.z) * 0.5f};
}

// ===========================================================================
// Volume: names and colours
// ===========================================================================

const char* Volume::KindName(Kind k) {
    switch (k) {
        case Kind::Checkpoint: return "Checkpoint";
        case Kind::Kill: return "Kill";
        case Kind::Force: return "Force";
        case Kind::Atmosphere: return "Atmosphere";
        case Kind::Ambience: return "Ambience";
    }
    return "Checkpoint";
}

const char* Volume::KindHint(Kind k) {
    switch (k) {
        case Kind::Checkpoint: return "A body inside makes the middle of the volume's floor its respawn point.";
        case Kind::Kill: return "A body that enters is hurt, then sent back to its last checkpoint (or where it began).";
        case Kind::Force: return "Pushes the bodies inside every physics step: a wind, a draught, an updraft, a current.";
        case Kind::Atmosphere: return "While the camera is inside, the scene's fog and vignette fade to this volume's.";
        case Kind::Ambience: return "Plays the Audio Source on this object while the camera is inside, fading in and out.";
    }
    return "";
}

Color Volume::KindColor(Kind k) {
    switch (k) {
        case Kind::Checkpoint: return Color::FromBytes(80, 220, 130);
        case Kind::Kill: return Color::FromBytes(240, 70, 70);
        case Kind::Force: return Color::FromBytes(90, 160, 255);
        case Kind::Atmosphere: return Color::FromBytes(190, 120, 240);
        case Kind::Ambience: return Color::FromBytes(250, 200, 70);
    }
    return Color::FromBytes(200, 200, 200);
}

// ===========================================================================
// Respawn memory (per body, by instance id)
// ===========================================================================

namespace {

struct RespawnRec {
    Vec3 home;   bool hasHome = false;     // where the body was first seen
    Vec3 check;  bool hasCheck = false;    // the last checkpoint it reached
};

std::unordered_map<std::uint64_t, RespawnRec>& Respawns() {
    static std::unordered_map<std::uint64_t, RespawnRec> m;
    return m;
}

} // namespace

bool Volume::RespawnPoint(const GameObject* body, Vec3& out) {
    if (!body) return false;
    auto it = Respawns().find(body->InstanceId());
    if (it == Respawns().end()) return false;
    if (it->second.hasCheck) { out = it->second.check; return true; }
    if (it->second.hasHome) { out = it->second.home; return true; }
    return false;
}

void Volume::SetRespawnPoint(const GameObject* body, const Vec3& where) {
    if (!body) return;
    RespawnRec& r = Respawns()[body->InstanceId()];
    r.check = where; r.hasCheck = true;
}

// ===========================================================================
// Volume: bodies
// ===========================================================================

bool Volume::Contains(const Vec3& world) const {
    return transform && Geometry().Contains(*transform, world);
}

bool Volume::Wanted(GameObject* g) const {
    if (activatorTag.empty()) return g->GetComponent<Rigidbody3D>() != nullptr;
    if (g->tag == activatorTag) return true;
    return g->GetComponent<Rigidbody3D>() && NoCodeMatches(g, activatorTag);
}

void Volume::Bodies(std::vector<GameObject*>& out) const {
    out.clear();
    Scene* s = GetScene();
    if (!s || !gameObject) return;
    std::unordered_set<GameObject*> seen;
    auto consider = [&](GameObject* g) {
        if (!g || !g->active || g->IsDestroyed() || !g->transform) return;
        if (g->IsSelfOrDescendantOf(gameObject)) return;
        if (!seen.insert(g).second) return;
        if (Wanted(g)) out.push_back(g);
    };
    for (Rigidbody3D* rb : s->FindObjectsOfType<Rigidbody3D>())
        if (rb && rb->enabled) consider(rb->gameObject);
    if (!activatorTag.empty())
        for (const auto& up : s->Objects()) consider(up.get());
    // A child follows its parent: when both match, only the parent is the body.
    std::unordered_set<GameObject*> set(out.begin(), out.end());
    out.erase(std::remove_if(out.begin(), out.end(), [&](GameObject* g) {
        for (const Transform* p = g->transform->Parent(); p; p = p->Parent())
            if (p->gameObject && set.count(p->gameObject)) return true;
        return false;
    }), out.end());
}

bool Volume::ViewerPosition(Vec3& out) const {
    Scene* s = GetScene();
    if (!s || !s->mainCamera || !s->mainCamera->transform) return false;
    out = s->mainCamera->transform->Position();
    return true;
}

void Volume::Respawn(GameObject* body) {
    Vec3 to;
    if (!RespawnPoint(body, to)) {
        OKAY_WARN("Kill volume '", gameObject->name, "': '", body->name,
                  "' has no respawn point yet (it started inside the volume), so it was left alone.");
        return;
    }
    body->transform->SetPosition(to);
    if (auto* rb = body->GetComponent<Rigidbody3D>()) {
        rb->velocity = Vec3::Zero;
        rb->angularVelocity = Vec3::Zero;
        rb->WakeUp();
    }
}

void Volume::UpdateBodies() {
    std::vector<GameObject*> bodies;
    Bodies(bodies);
    Scene* s = GetScene();
    std::unordered_set<std::uint64_t> now;
    for (GameObject* g : bodies) {
        const Vec3 p = g->transform->Position();
        const bool in = Contains(p);
        RespawnRec& rec = Respawns()[g->InstanceId()];
        if (!in && !rec.hasHome) { rec.home = p; rec.hasHome = true; }
        if (!in) continue;
        now.insert(g->InstanceId());
        if (m_inside.count(g->InstanceId())) continue;   // already inside: acted on entry
        if (kind == Kind::Checkpoint) {
            rec.check = Geometry().WorldFloorCenter(*transform) + Vec3{0.0f, 0.1f, 0.0f};
            rec.hasCheck = true;
            if (!eventName.empty() && s) BroadcastLogicSignal(*s, g, eventName, true);
        } else if (kind == Kind::Kill) {
            DamageHealthOn(g, killDamage);
            if (!eventName.empty() && s) BroadcastLogicSignal(*s, g, eventName, true);
            if (killAction == (int)KillAction::Destroy) { if (s) s->Destroy(g); }
            else Respawn(g);
        }
    }
    m_inside = std::move(now);
}

// ===========================================================================
// Volume: Force
// ===========================================================================

void Volume::FixedUpdate(float step) {
    if (kind != Kind::Force || !transform || !ScriptsRun(gameObject) || step <= 0.0f) return;
    std::vector<GameObject*> bodies;
    Bodies(bodies);
    const Vec3 centre = Geometry().WorldCenter(*transform);
    Vec3 dir = forceDirection;
    if (forceLocal) dir = transform->Rotation() * dir;
    const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dl > 1e-6f) dir = dir * (1.0f / dl); else dir = Vec3::Zero;
    std::unordered_set<std::uint64_t> now;
    for (GameObject* g : bodies) {
        auto* rb = g->GetComponent<Rigidbody3D>();
        if (!rb || rb->bodyType != Rigidbody3D::BodyType::Dynamic) continue;
        const Vec3 p = g->transform->Position();
        if (!Contains(p)) continue;
        now.insert(g->InstanceId());
        Vec3 a = dir;
        if (forceShape == (int)ForceShape::Radial) {
            Vec3 d = p - centre;
            const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            a = l > 1e-5f ? d * (1.0f / l) : Vec3::Zero;
        }
        rb->AddForce(a * forceStrength, ForceMode::Acceleration);
    }
    m_inside = std::move(now);
}

// ===========================================================================
// Volume: Atmosphere (fog + vignette) and Ambience (sound)
// ===========================================================================

namespace {

float Smooth(float t) { t = Mathf::Clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

float EdgeWeight(const VolumeGeometry& g, const Transform& t, const Vec3& p, float blend) {
    const float d = g.DepthAt(t, p);
    if (d < 0.0f) return 0.0f;
    if (blend <= 1e-4f) return 1.0f;
    return Smooth(d / blend);
}

Color LerpColor(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

} // namespace

void Volume::ReleaseAtmosphere() {
    if (!m_atmoEngaged) return;
    m_atmoEngaged = false;
    Scene* s = GetScene();
    if (!s) return;
    auto& rs = s->renderSettings;
    rs.fog = m_base.fog; rs.fogColor = m_base.color; rs.fogStart = m_base.start; rs.fogEnd = m_base.end;
    rs.vignette = m_base.vignette; rs.fogMode = m_base.mode;
}

void Volume::UpdateAtmosphere() {
    Scene* s = GetScene();
    if (!s || !transform) return;
    // One volume at a time drives the scene's settings: the first live Atmosphere volume. The others only report.
    Volume* lead = nullptr;
    std::vector<Volume*> all;
    for (Volume* v : s->FindObjectsOfType<Volume>()) {
        if (!v || v->kind != Kind::Atmosphere || !v->enabled || !v->gameObject || !v->gameObject->ActiveInHierarchy() || !v->transform) continue;
        if (!lead) lead = v;
        all.push_back(v);
    }
    Vec3 eye;
    const bool haveEye = ViewerPosition(eye);
    m_weight = haveEye ? EdgeWeight(Geometry(), *transform, eye, blend) : 0.0f;
    if (lead != this) {
        if (m_atmoEngaged) ReleaseAtmosphere();   // a lead appeared in front of us
        return;
    }
    float total = 0.0f;
    for (Volume* v : all) {
        v->m_weight = haveEye ? EdgeWeight(v->Geometry(), *v->transform, eye, v->blend) : 0.0f;
        total += v->m_weight;
    }
    auto& rs = s->renderSettings;
    if (total <= 0.0f) { ReleaseAtmosphere(); return; }
    if (!m_atmoEngaged) {
        m_atmoEngaged = true;
        m_base.fog = rs.fog; m_base.color = rs.fogColor; m_base.start = rs.fogStart; m_base.end = rs.fogEnd;
        m_base.vignette = rs.vignette; m_base.mode = rs.fogMode;
    }
    // Start from the scene's own look and move toward each volume by its weight. With no fog of its own the
    // scene starts far away (and in the volume's colour), so the fog closes in smoothly instead of flashing on.
    Color col = m_base.color;
    float start = m_base.start, end = m_base.end, vig = m_base.vignette;
    if (!m_base.fog || m_base.mode != 0) {
        const Volume* first = all.front();
        col = first->fogColor;
        start = std::max(first->fogEnd * 4.0f, 500.0f);
        end = start * 2.0f;
    }
    for (Volume* v : all) {
        const float w = v->m_weight;
        if (w <= 0.0f) continue;
        col = LerpColor(col, v->fogColor, w);
        start = start + (v->fogStart - start) * w;
        end = end + (v->fogEnd - end) * w;
        vig = vig + (v->vignette - vig) * w;
    }
    rs.fog = true; rs.fogMode = 0; rs.fogColor = col; rs.fogStart = start; rs.fogEnd = std::max(end, start + 0.01f);
    rs.vignette = Mathf::Clamp(vig, 0.0f, 1.0f);
}

void Volume::UpdateAmbience() {
    Vec3 eye;
    const bool haveEye = ViewerPosition(eye);
    const bool in = haveEye && transform && Contains(eye);
    m_weight = in ? 1.0f : 0.0f;
    auto* src = gameObject ? gameObject->GetComponent<AudioSource>() : nullptr;
    if (!src) return;
    if (in && !m_viewerWasInside) {
        if (src->IsPlaying()) src->FadeTo(1.0f, fadeTime);
        else src->FadeIn(fadeTime);
    } else if (!in && m_viewerWasInside) {
        src->FadeOut(fadeTime);
    }
    m_viewerWasInside = in;
}

// ===========================================================================
// Volume: lifecycle
// ===========================================================================

void Volume::Start() {
    m_inside.clear();
    m_weight = 0.0f;
    m_viewerWasInside = false;
}

void Volume::Update(float) {
    if (!gameObject || !transform || !ScriptsRun(gameObject)) return;
    switch (kind) {
        case Kind::Checkpoint:
        case Kind::Kill: UpdateBodies(); break;
        case Kind::Atmosphere: UpdateAtmosphere(); break;
        case Kind::Ambience: UpdateAmbience(); break;
        case Kind::Force: break;   // FixedUpdate
    }
}

void Volume::OnDisable() { ReleaseAtmosphere(); }
void Volume::OnDestroy() { ReleaseAtmosphere(); }

} // namespace okay
