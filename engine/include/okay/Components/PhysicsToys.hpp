#pragma once
// Physics toys: small ready-to-use components that make physical things happen with no code. Add one from
// Add Component > Physics 3D, set a few numbers in the Inspector, press Play.
//
//   ConveyorBelt   carries bodies standing on it                 (JumpPad already exists: Components/NoCode.hpp)
//   ForceField     pulls (or pushes, or swirls) bodies near it   GravityZone  changes gravity inside a box
//   Turntable      turns a body at a steady speed              MovingPlatform  glides between points, riders go along
//   Explosion      a radial shove (once, on a timer, or from script)   Breakable  breaks into pieces when hit hard
//
// Each one works in a zone it defines for itself (so it needs no trigger collider), looks at bodies with a physics query
// each fixed step, and never touches water, buoyancy or wind code. They are written from this engine's Physics3D queries
// (OverlapBox / OverlapSphere), Rigidbody3D (AddForce, velocity) and Scene (Instantiate-free): plain vector maths, no outside
// code. Header only; the Inspector rows and scene records are in src/Physics/PhysicsToys.cpp.
#include "okay/Components/PhysicsSurface.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay {
namespace toys {

/// The awake-able dynamic bodies whose colliders overlap a box (turned by `rot`), each once. The caller's own object is skipped.
inline std::vector<Rigidbody3D*> BodiesInBox(Scene& scene, const Vec3& c, const Vec3& half, const Quat& rot, int layerMask, GameObject* self) {
    std::vector<Rigidbody3D*> out;
    for (Collider3D* col : scene.physics3D().OverlapBox(scene, c, half, rot, (std::uint32_t)layerMask, false, self)) {
        if (!col || !col->gameObject) continue;
        Rigidbody3D* rb = col->gameObject->GetComponent<Rigidbody3D>();
        if (!rb || rb->bodyType != Rigidbody3D::BodyType::Dynamic || !rb->enabled) continue;
        if (std::find(out.begin(), out.end(), rb) == out.end()) out.push_back(rb);
    }
    return out;
}
inline std::vector<Rigidbody3D*> BodiesInSphere(Scene& scene, const Vec3& c, float r, int layerMask, GameObject* self) {
    std::vector<Rigidbody3D*> out;
    for (Collider3D* col : scene.physics3D().OverlapSphere(scene, c, r, (std::uint32_t)layerMask, false, self)) {
        if (!col || !col->gameObject) continue;
        Rigidbody3D* rb = col->gameObject->GetComponent<Rigidbody3D>();
        if (!rb || rb->bodyType != Rigidbody3D::BodyType::Dynamic || !rb->enabled) continue;
        if (std::find(out.begin(), out.end(), rb) == out.end()) out.push_back(rb);
    }
    return out;
}
inline Vec3 SafeNorm(const Vec3& v, const Vec3& fallback) { float l = v.Magnitude(); return l > 1e-6f ? v * (1.0f / l) : fallback; }

} // namespace toys

/// A belt: bodies standing on its top surface are carried along `direction` (in the belt's own axes) at `speed` metres per second.
class ConveyorBelt : public Behaviour {
public:
    Vec3  direction{0, 0, 1};     ///< which way the belt runs, in the belt's own axes (0,0,1 = along its length)
    float speed = 2.0f;           ///< belt speed in metres per second (negative runs it backwards)
    float grip = 30.0f;           ///< how fast a body catches up with the belt (metres per second, each second)
    float reach = 0.35f;          ///< how far above the belt a body still counts as standing on it
    int   layerMask = -1;         ///< which collision layers are carried (-1 = all)
    void FixedUpdate(float dt) override {
        if (!gameObject || !gameObject->scene() || !gameObject->transform || !(dt > 0.0f)) return;
        auto* box = gameObject->GetComponent<BoxCollider3D>();
        if (!box) return;
        Transform* t = gameObject->transform;
        const Quat rot = t->Rotation();
        const Vec3 up = rot * Vec3{0, 1, 0}, h = box->HalfExtents();
        const Vec3 centre = box->WorldCenter() + up * (h.y + reach * 0.5f);
        const Vec3 dir = toys::SafeNorm(rot * direction, Vec3{0, 0, 1});
        for (Rigidbody3D* rb : toys::BodiesInBox(*gameObject->scene(), centre, {h.x, reach * 0.5f, h.z}, rot, layerMask, gameObject)) {
            const float along = Vec3::Dot(rb->velocity, dir);
            const float want = speed - along;
            const float step = std::max(-grip * dt, std::min(grip * dt, want));
            if (std::fabs(want) < 1e-4f) continue;
            rb->velocity = rb->velocity + dir * step;
            rb->WakeUp();
        }
    }
};

/// A field that pulls or pushes bodies inside `radius`. Mode: 0 Attract, 1 Repel, 2 Gravity well (inverse-square, like a tiny
/// planet), 3 Swirl (round the field's up axis, with a little pull in). Strength is an acceleration (metres per second each second),
/// so a heavy crate and a light ball are pulled alike, like gravity. Turn it on and off with `active` (or SetActive).
class ForceField : public Behaviour {
public:
    enum Mode { Attract = 0, Repel = 1, GravityWell = 2, Swirl = 3 };
    enum Falloff { Constant = 0, Linear = 1, InverseSquare = 2 };
    int   mode = Attract;
    int   falloff = Linear;       ///< how the strength fades with distance (Gravity well always uses inverse-square)
    float radius = 6.0f;          ///< how far the field reaches, metres
    float strength = 25.0f;       ///< acceleration at the centre (Constant: everywhere), metres per second each second
    float coreRadius = 0.6f;      ///< inside this the pull stops growing (so nothing is flung out of a well's middle)
    bool  active = true;
    int   layerMask = -1;
    void SetActive(bool on) { active = on; }
    bool IsActive() const { return active; }
    /// The acceleration the field gives a body at offset `d` from the centre (zero outside the radius). For tests and tools.
    Vec3 AccelerationAt(const Vec3& d, const Vec3& up) const {
        const float dist = d.Magnitude();
        if (!active || dist > radius || radius <= 0.0f) return Vec3::Zero;
        const float core = std::max(coreRadius, 0.05f);
        const float dd = std::max(dist, core);
        float f = 1.0f;
        const int fo = mode == GravityWell ? InverseSquare : falloff;
        if (fo == Linear) f = std::max(0.0f, 1.0f - dist / radius);
        else if (fo == InverseSquare) f = (core * core) / (dd * dd);
        const Vec3 toward = dist > 1e-5f ? d * (-1.0f / dist) : Vec3::Zero;
        switch (mode) {
            case Repel: return toward * (-strength * f);
            case Swirl: {
                Vec3 side = Vec3::Cross(up, toward * -1.0f);
                side = toys::SafeNorm(side, Vec3::Zero);
                return side * (strength * f) + toward * (strength * f * 0.25f);
            }
            default: return toward * (strength * f);
        }
    }
    void FixedUpdate(float) override {
        if (!active || !gameObject || !gameObject->scene() || !gameObject->transform) return;
        const Vec3 c = gameObject->transform->Position();
        const Vec3 up = gameObject->transform->Rotation() * Vec3{0, 1, 0};
        for (Rigidbody3D* rb : toys::BodiesInSphere(*gameObject->scene(), c, radius, layerMask, gameObject)) {
            if (!rb->transform) continue;
            rb->AddForce(AccelerationAt(rb->transform->Position() - c, up), ForceMode::Acceleration);
        }
    }
};

/// A box of space with its own gravity: low gravity, zero-g, a wind-free upward float, or gravity sideways. Inside the box the
/// scene gravity is replaced by `gravity` (metres per second each second).
class GravityZone : public Behaviour {
public:
    Vec3  size{6, 6, 6};          ///< the box, in metres, in the zone object's own axes (scaled by the object's scale)
    Vec3  gravity{0, -2.0f, 0};   ///< gravity inside, world directions. 0,0,0 = floating; 0,-2,0 = like a small moon
    int   layerMask = -1;
    void FixedUpdate(float) override {
        if (!gameObject || !gameObject->scene() || !gameObject->transform) return;
        Scene& scene = *gameObject->scene();
        Transform* t = gameObject->transform;
        const Vec3 s = t->LossyScale();
        const Vec3 half{std::fabs(size.x * s.x) * 0.5f, std::fabs(size.y * s.y) * 0.5f, std::fabs(size.z * s.z) * 0.5f};
        const Vec3 world = scene.physics3D().gravity;
        for (Rigidbody3D* rb : toys::BodiesInBox(scene, t->Position(), half, t->Rotation(), layerMask, gameObject)) {
            const Vec3 delta = (gravity - world) * rb->gravityScale;
            if (delta.SqrMagnitude() < 1e-8f) continue;
            rb->AddForce(delta, ForceMode::Acceleration);
        }
    }
};

/// A physical turntable (the plain Spinner only turns the picture; this one turns a body, so it pushes and carries things). Turns its object about `axis` at `degreesPerSecond`, with a speed-up of `rampTime` seconds. The object becomes a kinematic body
/// (it turns things it touches and carries them round), unless it already has a Rigidbody 3D.
class Turntable : public Behaviour {
public:
    Vec3  axis{0, 1, 0};          ///< what it turns about, in the object's own axes
    float degreesPerSecond = 90.0f;
    float rampTime = 1.0f;        ///< seconds to reach full speed (0 = at once)
    void Start() override {
        if (!gameObject) return;
        if (!gameObject->GetComponent<Rigidbody3D>()) {
            auto* rb = gameObject->AddComponent<Rigidbody3D>();
            rb->bodyType = Rigidbody3D::BodyType::Kinematic; rb->freezeRotation = false;
        }
    }
    void FixedUpdate(float dt) override {
        if (!gameObject || !gameObject->transform) return;
        auto* rb = gameObject->GetComponent<Rigidbody3D>();
        if (!rb) return;
        m_t += dt;
        const float k = rampTime > 1e-4f ? std::min(1.0f, m_t / rampTime) : 1.0f;
        const Vec3 a = toys::SafeNorm(gameObject->transform->Rotation() * axis, Vec3{0, 1, 0});
        rb->freezeRotation = false;
        rb->angularVelocity = a * (degreesPerSecond * 0.0174532925f * k);
        rb->angularDrag = 0.0f;
        rb->WakeUp();
    }
private:
    float m_t = 0.0f;
};

/// A platform that glides through up to four points (given as offsets from where it starts) and carries whatever stands on it.
/// PingPong goes there and back; Loop goes round the points and starts again. It pauses at each point and eases in and out.
class MovingPlatform : public Behaviour {
public:
    enum Mode { PingPong = 0, Loop = 1 };
    int   mode = PingPong;
    int   pointCount = 2;         ///< how many points it visits, 2 to 4 (point 1 is where it starts)
    Vec3  offset2{4, 0, 0};       ///< point 2, as an offset from the start position
    Vec3  offset3{4, 0, 4};       ///< point 3
    Vec3  offset4{0, 0, 4};       ///< point 4
    float speed = 2.0f;           ///< metres per second between points
    float pause = 0.5f;           ///< seconds it waits at each point
    bool  easing = true;          ///< slow down and speed up at the ends of each leg
    /// Where the platform should be after `time` seconds of running (a pure function, so tests can check the path).
    Vec3 PositionAt(float time) const {
        Vec3 p[4] = {Vec3::Zero, offset2, offset3, offset4};
        const int n = std::max(2, std::min(4, pointCount));
        // The legs in order: PingPong 1-2-3-...-n-...-2-1, Loop 1-2-...-n-1.
        std::vector<int> order;
        for (int i = 0; i < n; ++i) order.push_back(i);
        if (mode == PingPong) { for (int i = n - 2; i >= 0; --i) order.push_back(i); } else order.push_back(0);
        const int legs = (int)order.size() - 1;
        std::vector<float> dur((std::size_t)legs);
        float total = 0.0f;
        for (int i = 0; i < legs; ++i) { dur[(std::size_t)i] = std::max(0.05f, (p[order[(std::size_t)i + 1]] - p[order[(std::size_t)i]]).Magnitude() / std::max(0.01f, speed)) + pause; total += dur[(std::size_t)i]; }
        if (total <= 0.0f) return p[0];
        float t = std::fmod(std::max(0.0f, time), total);
        for (int i = 0; i < legs; ++i) {
            const float d = dur[(std::size_t)i];
            if (t <= d) {
                const float move = d - pause;
                float u = move > 1e-5f ? std::min(1.0f, t / move) : 1.0f;
                if (easing) u = u * u * (3.0f - 2.0f * u);
                return p[order[(std::size_t)i]] + (p[order[(std::size_t)i + 1]] - p[order[(std::size_t)i]]) * u;
            }
            t -= d;
        }
        return p[order[(std::size_t)legs]];
    }
    void Start() override {
        if (!gameObject || !gameObject->transform) return;
        m_start = gameObject->transform->Position();
        m_time = 0.0f; m_ready = true;
        auto* rb = gameObject->GetComponent<Rigidbody3D>();
        if (!rb) { rb = gameObject->AddComponent<Rigidbody3D>(); }
        rb->bodyType = Rigidbody3D::BodyType::Kinematic;
    }
    void FixedUpdate(float dt) override {
        if (!m_ready || !gameObject || !gameObject->transform || !(dt > 0.0f)) return;
        auto* rb = gameObject->GetComponent<Rigidbody3D>();
        if (!rb) return;
        m_time += dt;
        const Vec3 target = m_start + PositionAt(m_time);
        rb->velocity = (target - gameObject->transform->Position()) * (1.0f / dt);   // driven by speed, so riders are carried
        rb->WakeUp();
    }
private:
    Vec3 m_start{0, 0, 0}; float m_time = 0.0f; bool m_ready = false;
};

/// A radial shove. `Trigger()` it from a script, set `onStart`, or set `delay` for a timer. Everything with a body inside `radius`
/// gets an impulse (so a light body flies further than a heavy one), strongest at the centre, plus an optional `damage` hook.
class Explosion : public Behaviour {
public:
    float force = 25.0f;          ///< impulse at the centre (kg metres per second): a 1 kg body flies at this speed
    float radius = 6.0f;
    float upwardBias = 0.4f;      ///< extra upward kick so things fly up as well as out
    float damage = 0.0f;          ///< passed to `onDamage` (scaled down with distance)
    float delay = 1.0f;           ///< seconds after Start before it goes off (only with `onStart`)
    bool  onStart = false;        ///< go off by itself `delay` seconds after the game starts
    bool  destroyAfter = false;   ///< remove the object after it goes off
    int   layerMask = -1;
    std::function<void(GameObject*, float)> onDamage;   ///< code hook (not saved): who was hit and how much damage
    int   fired = 0;              ///< how many times it has gone off (runtime)
    /// Go off now at this object's position. Returns how many bodies were shoved.
    int Trigger() {
        if (!gameObject || !gameObject->scene() || !gameObject->transform) return 0;
        ++fired;
        const int n = At(*gameObject->scene(), gameObject->transform->Position(), force, radius, upwardBias, layerMask, damage, onDamage, gameObject);
        if (destroyAfter) gameObject->scene()->Destroy(gameObject);
        return n;
    }
    /// A one-off blast anywhere (what the visual-script node "Explosion At" calls).
    static int At(Scene& scene, const Vec3& at, float force, float radius, float upwardBias = 0.4f, int layerMask = -1, float damage = 0.0f,
                  const std::function<void(GameObject*, float)>& onDamage = {}, GameObject* self = nullptr) {
        if (!(radius > 0.0f)) return 0;
        int n = 0;
        for (Rigidbody3D* rb : toys::BodiesInSphere(scene, at, radius, layerMask, self)) {
            if (!rb->transform) continue;
            const Vec3 d = rb->transform->Position() - at;
            const float dist = d.Magnitude();
            const float falloff = std::max(0.0f, 1.0f - dist / radius);
            Vec3 dir = dist > 1e-4f ? d * (1.0f / dist) : Vec3{0, 1, 0};
            dir.y += upwardBias;
            dir = toys::SafeNorm(dir, Vec3{0, 1, 0});
            rb->AddImpulse(dir * (force * falloff));
            if (onDamage && damage > 0.0f) onDamage(rb->gameObject, damage * falloff);
            ++n;
        }
        return n;
    }
    void Update(float dt) override {
        if (!onStart || m_done || !(dt > 0.0f)) return;
        m_clock += dt;
        if (m_clock >= delay) { m_done = true; Trigger(); }
    }
private:
    float m_clock = 0.0f; bool m_done = false;
};

/// A crate that breaks into `pieces` x `pieces` x `pieces` smaller crates when something hits it harder than `breakSpeed`. The
/// pieces are real bodies, inherit the crate's speed and material and tidy themselves away after `pieceLife` seconds.
class Breakable : public Behaviour {
public:
    float breakSpeed = 6.0f;      ///< how hard it must be hit (closing speed, metres per second)
    int   pieces = 2;             ///< pieces per side (2 = 8 pieces, 3 = 27)
    float pieceLife = 6.0f;       ///< seconds before the pieces disappear
    float burstSpeed = 2.5f;      ///< how fast the pieces fly apart
    bool  broken = false;         ///< runtime
    /// Break it now (also used by OnCollisionEnter3D). Returns the number of pieces made.
    int Break(const Vec3& hitVelocity = Vec3::Zero) {
        if (broken || !gameObject || !gameObject->scene() || !gameObject->transform) return 0;
        broken = true;
        Scene& scene = *gameObject->scene();
        Transform* t = gameObject->transform;
        const Vec3 s = t->LossyScale();
        const int n = std::max(1, std::min(4, pieces));
        auto* box = gameObject->GetComponent<BoxCollider3D>();
        const Vec3 full = box ? Vec3{box->size.x * s.x, box->size.y * s.y, box->size.z * s.z} : s;
        const Vec3 piece{full.x / n, full.y / n, full.z / n};
        auto* mr = gameObject->GetComponent<MeshRenderer>();
        auto* rb0 = gameObject->GetComponent<Rigidbody3D>();
        const float mass = rb0 ? std::max(0.01f, rb0->mass) / (float)(n * n * n) : 0.2f;
        const Vec3 v0 = rb0 ? rb0->velocity : Vec3::Zero;
        const PhysicsMaterialData* mat = SurfaceOf(gameObject);
        int made = 0;
        for (int x = 0; x < n; ++x) for (int y = 0; y < n; ++y) for (int z = 0; z < n; ++z) {
            const Vec3 local{((float)x + 0.5f) / n - 0.5f, ((float)y + 0.5f) / n - 0.5f, ((float)z + 0.5f) / n - 0.5f};
            const Vec3 off = t->Rotation() * Vec3{local.x * full.x, local.y * full.y, local.z * full.z};
            GameObject* p = scene.CreateGameObject(gameObject->name + " piece");
            p->transform->localPosition = t->Position() + off;
            p->transform->localRotation = t->Rotation();
            p->transform->localScale = piece;
            auto* m = p->AddComponent<MeshRenderer>();
            m->mesh = Mesh::Cube(1.0f);
            if (mr) m->color = mr->color;
            auto* c = p->AddComponent<BoxCollider3D>(); c->size = Vec3::One;
            auto* r = p->AddComponent<Rigidbody3D>(); r->mass = mass; r->freezeRotation = false;
            r->velocity = v0 + toys::SafeNorm(off, Vec3{0, 1, 0}) * burstSpeed + hitVelocity * 0.1f;
            if (mat) AssignPhysicsMaterial(*p, mat->name);
            auto* life = p->AddComponent<PieceLife>(); life->life = pieceLife;
            ++made;
        }
        scene.Destroy(gameObject);
        return made;
    }
    void OnCollisionEnter3D(const Collision3D& c) override { if (c.impactSpeed >= breakSpeed) Break(c.relativeVelocity); }

    /// Removes a piece when its time is up.
    class PieceLife : public Behaviour {
    public:
        float life = 6.0f;
        void Update(float dt) override { life -= dt; if (life <= 0.0f && gameObject && gameObject->scene()) gameObject->scene()->Destroy(gameObject); }
    };
};

} // namespace okay
