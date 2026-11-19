#pragma once
// BoatController: drive a floating hull with W/S (throttle, reverse) and A/D (steer).
//
// Written by us from how a small motor boat behaves (a propeller pushes along the hull, a rudder turns the boat
// only while water flows past it), not from any other engine. It sits on the same object as a Rigidbody3D and a
// Buoyancy (it adds a Buoyancy itself if it is missing). Buoyancy makes the hull float and brings the drag; this
// component adds:
//   * engine thrust along the boat's nose, flat to the water (a tipped-up bow does not make it fly). The push
//     fades to zero as the boat reaches `maxSpeed`, so the top speed is simply the number you type;
//   * a rudder: turning needs water flowing past it, from speed or from the propeller wash, so a boat at rest turns
//     slowly and a fast one turns well; in reverse the steering flips like a car's;
//   * less control out of the water: thrust and steering both scale with how much of the hull is under water;
//   * hooks for sound (EngineRevs, an AudioSource on the same object is tuned for you) and looks (a foam wake
//     behind the stern and a bow splash, built from the TrailRenderer and the particle system);
//   * an optional chase camera.
// Input comes from the Input Actions: "Move" (W/S and A/D, arrows, or the left stick) and "Jump" (Space = brake).
// With Is Player off the boat only obeys the throttle / steer values that code sets (an AI boat, a test).
#include "okay/Components/Buoyancy.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace okay {

class BoatController : public Behaviour {
public:
    // ---- Engine ----
    bool  isPlayer = true;          ///< read the keyboard / stick (off: only throttleInput and steerInput move it)
    float maxSpeed = 12.0f;         ///< top speed forward, metres per second
    float reverseSpeed = 4.0f;      ///< top speed backwards
    float enginePower = 6.0f;       ///< how hard the engine pushes at a standstill, m/s^2
    float brakePower = 5.0f;        ///< extra slowing when braking (Space) or when you pull back while going forward
    // ---- Steering ----
    float turnRate = 55.0f;         ///< how fast the boat can turn at full rudder, degrees per second
    float steerResponse = 3.5f;     ///< how quickly it swings into a turn (per second)
    // ---- Looks and sound ----
    bool  wake = true;              ///< foam trail behind the stern
    bool  bowSplash = true;         ///< spray at the bow when going fast
    bool  engineSound = true;       ///< tune a sibling AudioSource's pitch and volume to the engine
    // ---- Camera ----
    bool  followCamera = false;     ///< chase the scene's main camera behind the boat
    float camDistance = 10.0f;
    float camHeight = 4.0f;
    float camLerp = 4.0f;

    // ---- Controls (set these from code for an AI boat; written by the keys when Is Player is on) ----
    float throttleInput = 0.0f;     ///< -1 reverse .. +1 full ahead
    float steerInput = 0.0f;        ///< -1 left .. +1 right
    bool  brakeInput = false;

    // ---- Read-outs ----
    float Speed() const { return m_speed; }            ///< forward speed, m/s (negative backwards)
    float EngineRevs() const { return m_revs; }        ///< 0 idle .. 1 flat out (drive a sound with it)
    float Control() const { return m_control; }        ///< 0 out of the water .. 1 fully afloat
    float Rudder() const { return m_rudder; }          ///< smoothed steering, -1 .. 1
    bool  InWater() const { return m_control > 0.05f; }
    /// Called every physics step with (revs 0..1, speed m/s): an engine-sound hook for code or a script.
    std::function<void(float, float)> onEngine;

    void Start() override {
        Ensure();
        if (auto* rb = Body()) rb->freezeRotation = false;
    }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        for (int i = 0; i < 2; ++i) if (m_trail[i] && dying.count(m_trail[i]->gameObject)) m_trail[i] = nullptr;
        if (m_spray && dying.count(m_spray->gameObject)) m_spray = nullptr;
    }
    void OnDestroy() override {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return;
        for (int i = 0; i < 2; ++i) if (m_trail[i] && m_trail[i]->gameObject) sc->Destroy(m_trail[i]->gameObject);
        if (m_spray && m_spray->gameObject) sc->Destroy(m_spray->gameObject);
        m_trail[0] = m_trail[1] = nullptr; m_spray = nullptr;
    }

    void FixedUpdate(float dt) override {
        Rigidbody3D* rb = Body();
        if (!rb || !transform || dt <= 0.0f) return;
        if (!IsLocallyControlled(gameObject)) return;   // a remote copy: the network moves it
        if (rb->bodyType != Rigidbody3D::BodyType::Dynamic || rb->mass <= 1e-4f) return;
        Buoyancy* bu = Ensure();

        ReadInput();
        const float throttle = Mathf::Clamp(throttleInput, -1.0f, 1.0f);
        const float steer = Mathf::Clamp(steerInput, -1.0f, 1.0f);

        // How much of the boat is afloat. A little in the water gives a little grip; half in gives full control.
        const float sub = bu ? bu->Submerged() : 1.0f;
        m_control = Mathf::Clamp(sub * 2.0f, 0.0f, 1.0f);

        // Nose direction flattened onto the water.
        Vec3 fw = transform->Forward();
        fw.y = 0.0f;
        const float fl = std::sqrt(fw.x * fw.x + fw.z * fw.z);
        fw = fl > 1e-4f ? Vec3{fw.x / fl, 0.0f, fw.z / fl} : Vec3{0.0f, 0.0f, 1.0f};
        const Vec3 flow = bu ? bu->SurfaceUnder().velocity : Vec3{0, 0, 0};
        const float vf = Vec3::Dot(rb->velocity - Vec3{flow.x, 0.0f, flow.z}, fw);   // speed through the water
        m_speed = vf;

        // ---- Engine thrust: a "cruise" push. The throttle sets the speed it aims for (full throttle = maxSpeed,
        // half = half of it); it pushes hard while far from that speed and eases off as it gets there. It also pays
        // for the hull drag at that speed, so the boat really reaches the number you typed. Letting go of the
        // throttle just coasts (the water slows it). Pulling back while going forward brakes first. ----
        float accel = 0.0f;
        if (throttle != 0.0f) {
            const float top = throttle > 0.0f ? maxSpeed : reverseSpeed;
            const float vT = throttle * std::max(0.1f, top);
            const float err = vT - vf;
            const float k = Mathf::Clamp(err / (0.3f * std::max(0.1f, std::fabs(vT))), -1.0f, 1.0f);
            const float lim = err >= 0.0f ? (throttle > 0.0f ? enginePower : enginePower * 0.6f) : brakePower;
            accel = lim * k;
            if (vf * throttle > 0.0f && bu)   // moving the way it is told to: pay for the hull drag
                accel += (vf > 0.0f ? 1.0f : -1.0f) * (bu->dragForward + bu->dragForward2 * std::fabs(vf)) * std::fabs(vf) * bu->Submerged();
        }
        accel *= m_control;
        if (accel != 0.0f) rb->AddForce(fw * (accel * rb->mass));
        if (brakeInput && m_control > 0.0f) {   // Space: bite the water
            const float k = 1.0f + brakePower * 0.5f * m_control * dt;
            rb->velocity = Vec3{rb->velocity.x / k, rb->velocity.y, rb->velocity.z / k};
        }

        // ---- Rudder: needs flowing water, from speed or from the propeller wash ----
        const float wash = std::fabs(throttle) * 0.5f;
        const float flowPast = Mathf::Clamp(std::fabs(vf) / 3.0f + wash, 0.0f, 1.0f);
        const float dirSign = vf >= -0.1f ? 1.0f : -1.0f;
        const float target = -steer * turnRate * 0.0174532925f * flowPast * dirSign * m_control;   // + yaw is a left turn
        const float k = 1.0f - std::exp(-std::max(0.0f, steerResponse) * dt);
        rb->angularVelocity.y += (target - rb->angularVelocity.y) * k * (m_control > 0.0f ? 1.0f : 0.0f);
        m_rudder += (steer - m_rudder) * (1.0f - std::exp(-8.0f * dt));
        rb->WakeUp();

        // ---- Engine revs (for sound) ----
        const float want = Mathf::Clamp(std::fabs(throttle) * 0.7f + std::fabs(vf) / std::max(0.1f, maxSpeed) * 0.5f, 0.0f, 1.0f);
        m_revs += (want - m_revs) * (1.0f - std::exp(-5.0f * dt));
        if (onEngine) onEngine(m_revs, vf);
    }

    void Update(float dt) override {
        if (!transform || dt <= 0.0f) return;
        Buoyancy* bu = gameObject ? gameObject->GetComponent<Buoyancy>() : nullptr;
        TuneSound();
        UpdateWake(dt, bu);
        if (followCamera && isPlayer) UpdateCamera(dt);
    }

private:
    Rigidbody3D* Body() const { return gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr; }
    Buoyancy* Ensure() {
        if (!gameObject) return nullptr;
        auto* bu = gameObject->GetComponent<Buoyancy>();
        if (!bu) {
            if (!gameObject->GetComponent<Rigidbody3D>()) return nullptr;
            bu = gameObject->AddComponent<Buoyancy>();
            // A boat: slips forward easily, grips sideways (the keel).
            bu->dragForward = 0.15f; bu->dragForward2 = 0.02f;
            bu->dragSide = 1.5f; bu->dragSide2 = 0.5f;
        }
        return bu;
    }

    void ReadInput() {
        if (!isPlayer) return;
        Vec2 v = InputActions::Vector("Move");
        if (v.x == 0.0f && v.y == 0.0f) v = Input::AxisWASD();
        throttleInput = v.y;
        steerInput = v.x;
        brakeInput = InputActions::Get("Jump");
    }

    void TuneSound() {
        if (!engineSound || !gameObject) return;
        if (auto* a = gameObject->GetComponent<AudioSource>()) {
            a->pitch = 0.7f + m_revs * 1.1f;
            a->volume = 0.25f + 0.75f * m_revs;
        }
    }

    static Vec3 Flat(const Vec3& v) { return Vec3{v.x, 0.0f, v.z}; }

    void UpdateWake(float dt, Buoyancy* bu) {
        Scene* sc = gameObject->scene();
        if (!sc || !bu) return;
        Vec3 c, h; Quat q;
        bu->HullBounds(c, h, q);
        const Water::Surface& sur = bu->SurfaceUnder();
        const float surfY = bu->InWater() ? sur.height : c.y - h.y;
        const Vec3 fw = q * Vec3{0, 0, 1}, rt = q * Vec3{1, 0, 0};
        const float speed = std::fabs(m_speed);
        const bool active = wake && bu->InWater();
        // Tell a Water with the realistic look where the boat is going, so it lays rings along the path (docs/water_visuals.md).
        if (bu->InWater()) {
            if (Rigidbody3D* wrb = Body()) {
                for (Water* w : sc->FindObjectsOfType<Water>()) {
                    Water::Surface ws;
                    if (!w || !w->enabled || !w->realisticLook || !w->SurfaceAt({c.x, surfY, c.z}, ws, 0.0f)) continue;
                    w->AddWake({c.x, surfY, c.z}, wrb->velocity, dt, (int)(std::intptr_t)this);
                    break;
                }
            }
        }

        if (active && !m_trail[0]) {
            for (int i = 0; i < 2; ++i) {
                GameObject* go = sc->CreateGameObject(i == 0 ? "Boat Wake Port" : "Boat Wake Starboard");
                m_trail[i] = go->AddComponent<TrailRenderer>();
                m_trail[i]->time = 2.5f;
                m_trail[i]->minVertexDistance = 0.4f;
                m_trail[i]->alignment = LineAlignment::Flat;
                m_trail[i]->color = Color{0.95f, 0.98f, 1.0f, 0.7f};
                m_trail[i]->teleportDistance = 6.0f;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (!m_trail[i] || !m_trail[i]->transform) continue;
            const float side = i == 0 ? -1.0f : 1.0f;
            Vec3 p = c - fw * h.z * 0.95f + rt * (side * h.x * 0.8f);
            p.y = surfY + 0.04f;
            m_trail[i]->transform->SetPosition(p);
            m_trail[i]->emitting = active && speed > 1.0f;
            m_trail[i]->width = Mathf::Clamp(0.3f + speed * 0.08f, 0.3f, 1.4f);
        }

        if (bowSplash && bu->InWater()) {
            if (!m_spray) m_spray = waterfx::MakeSpray(sc, "Boat Bow Splash", 0.6f);
            if (m_spray && m_spray->transform) {
                Vec3 p = c + fw * h.z * 0.95f;
                p.y = surfY;
                m_spray->transform->SetPosition(p);
                m_spray->startVelocity = {fw.x * speed * 0.2f, 1.6f + speed * 0.12f, fw.z * speed * 0.2f};
                if (speed > 3.0f) {
                    m_bowAcc += (speed - 3.0f) * 4.0f * dt;
                    const int n = (int)m_bowAcc;
                    if (n > 0) { m_spray->Emit(std::min(n, 8)); m_bowAcc -= (float)n; }
                } else m_bowAcc = 0.0f;
            }
        }
    }

    void UpdateCamera(float dt) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s || !s->mainCamera || !s->mainCamera->gameObject) return;
        Transform* ct = s->mainCamera->gameObject->transform;
        if (!ct || ct == transform) return;
        Vec3 fp = Flat(transform->Forward());
        const float fl = std::sqrt(fp.x * fp.x + fp.z * fp.z);
        fp = fl > 1e-4f ? Vec3{fp.x / fl, 0.0f, fp.z / fl} : Vec3{0, 0, 1};
        const Vec3 pos = transform->Position();
        const Vec3 want = pos - fp * camDistance + Vec3{0, camHeight, 0};
        float t = camLerp > 0.0f ? (1.0f - std::exp(-camLerp * dt)) : 1.0f;
        t = Mathf::Clamp(t, 0.0f, 1.0f);
        const Vec3 np = ct->Position() + (want - ct->Position()) * t;
        ct->SetPosition(np);
        const Vec3 look = (pos + Vec3{0, camHeight * 0.3f, 0} + fp * (camDistance * 0.3f)) - np;
        if (look.SqrMagnitude() > 1e-6f) ct->SetRotation(Quat::LookRotation(look * -1.0f));   // a camera looks down its -Z
    }

    float m_speed = 0.0f, m_revs = 0.0f, m_control = 0.0f, m_rudder = 0.0f, m_bowAcc = 0.0f;
    TrailRenderer* m_trail[2] = {nullptr, nullptr};
    ParticleSystem* m_spray = nullptr;
};

} // namespace okay
