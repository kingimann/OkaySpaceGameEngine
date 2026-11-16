#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {

/// Arcade vehicle controller. Throttle drives the car forward along its facing,
/// steering yaws it (scaled by speed, and reversed when backing up), braking and a
/// handbrake slow it, and lateral "grip" bleeds off sideways slide so it corners
/// instead of skating (drop grip / hold handbrake to drift). A short downward
/// raycast keeps it from being considered airborne on gentle slopes.
///
/// Setup: put this on the car body. With a sibling Rigidbody3D it drives the
/// body's velocity (so it collides and gravity applies); without one it moves the
/// Transform directly. Default keys: W/S or Up/Down = throttle/brake-reverse,
/// A/D or Left/Right = steer, Space = handbrake. Turn on `followCamera` to make the
/// scene's main camera chase it.
///
/// This is the arcade model. Turn on `suspension` for per-wheel raycast suspension:
/// four down-rays (at the wheelBase × trackWidth corners) drive a critically-damped
/// vertical spring so the body rides at `rideHeight`, soaks up bumps, and the chassis
/// rolls/pitches from per-wheel compression plus accel/cornering lean.
///
/// Timing: with a Rigidbody3D the driving (throttle, brakes, steering, grip and the
/// suspension spring) runs once per PHYSICS step in FixedUpdate, sized by the fixed step,
/// so a car drives the same at 30 or 144 frames per second. Without a Rigidbody3D it moves
/// the Transform once per frame in Update. Wheel visuals and the chase camera run per frame.
///
/// Grip comes from the wheels: drive, braking, steering and sideways grip only act through
/// wheels that touch the ground. Arcade mode casts one ray from the centre along the car's own
/// down direction (groundCheckDistance long), so a car on its roof or in the air has no grip;
/// suspension mode uses its four wheel rays and scales by how many hit. `airControl` keeps a
/// share of control with no wheel down (0 by default). A body with gravity off always grips.
class VehicleController : public Behaviour {
public:
    VehicleController() { m_uid = ++NextUid(); Live().push_back({this, m_uid}); }
    ~VehicleController() override {
        auto& l = Live();
        for (size_t i = 0; i < l.size(); ++i) if (l[i].first == this) { l.erase(l.begin() + (long)i); break; }
    }
    VehicleController(const VehicleController&) = delete;
    VehicleController& operator=(const VehicleController&) = delete;
    /// True while `p` is a live VehicleController with this `uid` (Uid()). A seat keeps (pointer, uid) pairs and
    /// checks them here, so a controller that was removed (even with another one later made at the same address)
    /// is never touched.
    static bool IsLive(const VehicleController* p, unsigned uid) {
        if (!p) return false;
        for (const auto& e : Live()) if (e.first == p) return e.second == uid;
        return false;
    }
    unsigned Uid() const { return m_uid; }

    float acceleration = 18.0f;          // how quickly it reaches top speed (units/s^2)
    float maxSpeed     = 22.0f;          // forward top speed (units/s)
    float reverseSpeed = 8.0f;           // top reverse speed
    float brakeForce   = 36.0f;          // deceleration when braking (units/s^2)
    float drag         = 4.0f;           // rolling resistance while coasting (no throttle)
    float turnSpeed    = 110.0f;         // steering rate at speed (degrees/s)
    float grip         = 7.0f;           // how fast sideways slide is killed (higher = sticky)
    float handbrakeGrip = 0.6f;          // grip while the handbrake is held (low = drifty)
    float groundCheckDistance = 1.2f;    // down-ray length for "on the ground"
    /// Share (0..1) of drive, braking, steering and grip the car keeps with no wheel on the
    /// ground (in the air, or on its roof or side). 0, the default, is none: the wheels only
    /// push while they touch the ground. 1 drives the same everywhere (the old behaviour).
    float airControl = 0.0f;

    // ---- Per-wheel raycast suspension (opt-in) ----
    bool  suspension       = false;      // enable spring suspension + body tilt
    float rideHeight       = 0.7f;       // chassis height the spring rests at (units)
    float springStrength   = 60.0f;      // vertical spring stiffness
    float springDamping    = 9.0f;       // vertical spring damper
    float suspensionTravel = 0.6f;       // droop below rideHeight before airborne
    float wheelBase        = 2.4f;       // front-to-rear wheel spacing
    float trackWidth       = 1.6f;       // left-to-right wheel spacing
    float bodyLean         = 0.5f;       // chassis lean from accel/cornering (0 = off)
    float maxTilt          = 16.0f;      // clamp body roll/pitch (degrees)
    float tiltSmooth       = 9.0f;       // how fast tilt eases toward target

    bool  followCamera = false;          // chase the scene's main camera behind the car
    float camDistance  = 8.0f;
    float camHeight    = 3.5f;
    float camLerp      = 6.0f;

    // Throttle (W/S or Up/Down) and steering (A/D or Left/Right) come from the
    // shared WASD/arrow axis. The handbrake key is remappable.
    char handbrakeKey = ' ';

    /// ---- Driver seat (see VehicleSeat.hpp) ----
    /// A vehicle with a driver VehicleSeat only reads the keyboard while someone sits in it.
    /// With nobody at the wheel it brakes to a stop (brakeForce) and does not chase a camera.
    /// A vehicle with no driver seat (or only passenger seats) keeps the old behaviour: it always reads the keys.
    GameObject* Driver() const { return m_driver; }
    bool  HasDriverSeat() const { return m_driverSeats > 0; }
    /// Called by VehicleSeat only.
    void  SetDriver(GameObject* who) { m_driver = who; }
    void  AddDriverSeat() { ++m_driverSeats; }
    void  RemoveDriverSeat() { if (m_driverSeats > 0) --m_driverSeats; }

    float Speed() const { return m_speed; }              // signed forward speed (units/s)
    bool  IsGrounded() const { return m_grounded; }
    /// Share (0..1) of the wheels on the ground at the last physics step: arcade mode is 0 or 1,
    /// suspension mode is wheels hit / 4.
    float GroundedShare() const { return m_groundShare; }

    /// ---- Wheel visuals (looks only; nothing here is saved) ----
    /// Every descendant named "Wheel..." that has a child named "Tire" is animated: the wheel object
    /// is turned about Y for steering (only if its name contains "Front"; D turns it right), and the Tire is spun about
    /// the car's X axis by distance / radius. The radius is half the Tire's local X scale (a Cylinder
    /// mesh has radius 0.5). So the Tire is the cylinder (scaled to the tyre size, laid on its side:
    /// local rotation Euler(0, 0, 90) when not spinning), and the "Wheel" object is an empty at the axle.
    static constexpr float kMaxSteerAngle = 30.0f;       ///< degrees the front wheels turn at full lock
    float WheelSpin() const { return m_wheelSpin; }      ///< degrees the wheels have rolled (0..360), + = forward
    float SteerAngle() const { return m_steerAngle; }    ///< degrees the front wheels are turned, + = right
    int   WheelCount() const { return (int)m_wheels.size(); }
    /// The Seat that sits in this car moves the main camera (a seated rider's camera). While it does, the
    /// chase camera of `followCamera` stays out of its way. Called by VehicleSeat only.
    void  SetCameraDrivenBySeat(bool on) { m_seatCamera = on; }
    bool  CameraDrivenBySeat() const { return m_seatCamera; }

    void Update(float dt) override {
        if (!transform || dt <= 0.0f) return;
        if (!IsLocallyControlled(gameObject)) { ProxyWheels(dt); return; }   // remote proxy: NetworkSync drives it

        // ---- Read input: WASD/arrow axis (y = throttle, x = steer) ----
        // With a driver seat, only a seated driver's keys count; empty = no input.
        // The keys are read once per frame here and used by every physics step of this frame.
        m_unmanned = m_driverSeats > 0 && !m_driver;
        m_throttleIn = 0.0f; m_steerIn = 0.0f; m_handbrakeIn = false;
        if (!m_unmanned) {
            Vec2 axis = Input::AxisWASD();
            m_throttleIn = axis.y; m_steerIn = axis.x;
            m_handbrakeIn = handbrakeKey && Input::GetKey(handbrakeKey);
        }

        // No body: move the Transform directly, once per frame.
        if (!Body()) Drive(dt, nullptr);

        AnimateWheels(dt, m_speed, m_steerIn);
        if (followCamera && !m_unmanned && !m_seatCamera) UpdateCamera(dt);
    }

    /// With a Rigidbody3D the car is driven here, once per physics step (`step` is the fixed
    /// step, or the frame time when the scene steps physics once per frame). A force added
    /// here (the suspension spring) is used by exactly the step that follows.
    void FixedUpdate(float step) override {
        if (!transform || step <= 0.0f) return;
        if (!IsLocallyControlled(gameObject)) return;
        if (Rigidbody3D* rb = Body()) Drive(step, rb);
    }

private:
    Rigidbody3D* Body() const { return gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr; }

    // One drive step of `dt` seconds: per physics step with a body, per frame without one.
    void Drive(float dt, Rigidbody3D* rb) {
        const bool unmanned = m_unmanned;
        const float throttle = m_throttleIn, steer = m_steerIn;
        const bool handbrake = m_handbrakeIn;

        // Planar facing axes (flatten Y so slopes don't change the drive plane).
        Vec3 F = transform->Forward(); Vec3 R = transform->Right();
        Vec3 fp = Planar(F), rp = Planar(R);

        // ---- Which wheels touch the ground: everything below acts only through them ----
        // A body-less car has no physics, and a body with gravity off (gravityScale 0) has no
        // "down" to fall to (a hover car, or a headless test without a floor): both always drive.
        const float share = GroundShare(fp, rp);
        const float ground = (rb && rb->gravityScale != 0.0f) ? share : 1.0f;
        const float grip01 = ground + (1.0f - ground) * Mathf::Clamp(airControl, 0.0f, 1.0f);

        Vec3 vel = rb ? rb->velocity : m_kinematicVel;
        float speedF = vel.x * fp.x + vel.z * fp.z;       // velocity along facing
        float lat    = vel.x * rp.x + vel.z * rp.z;       // sideways velocity

        // ---- Steering: scales in as you gain speed; reverses when backing up ----
        float steerScale = Mathf::Clamp(std::fabs(speedF) / 4.0f, 0.0f, 1.0f);
        float dirSign = (speedF >= -0.05f) ? 1.0f : -1.0f;
        // A positive yaw is counter-clockwise seen from above, which is a LEFT turn for any heading. D (steer > 0)
        // must turn right, so the yaw is the negative of the steering.
        float yawDelta = (std::fabs(steer) > 0.001f && steerScale > 0.001f)
                       ? -steer * turnSpeed * steerScale * dirSign * grip01 * dt : 0.0f;
        if (suspension) {
            // Suspension owns the full orientation (yaw + pitch/roll), so track yaw
            // as a scalar and rebuild the rotation rather than accumulating quats.
            if (!m_yawInit) { m_yaw = std::atan2(F.x, F.z) * 57.2957795f; m_yawInit = true; }
            m_yaw += yawDelta;
            transform->localRotation = Quat::Euler(m_pitch, m_yaw, m_roll);
        } else if (yawDelta != 0.0f) {
            transform->Rotate({0.0f, yawDelta, 0.0f});
        }

        // Recompute facing after the turn so momentum follows the new heading.
        F = transform->Forward(); R = transform->Right();
        fp = Planar(F); rp = Planar(R);

        // ---- Longitudinal: ease forward speed toward the throttle target ----
        float target = throttle > 0.0f ? maxSpeed * throttle
                     : throttle < 0.0f ? -reverseSpeed * (-throttle) : 0.0f;
        float rate = unmanned ? brakeForce                           // driver left: brake to a stop
                   : (throttle == 0.0f) ? drag
                   : (throttle < 0.0f && speedF > 0.1f) ? brakeForce      // pressing back while rolling fwd = brake
                   : acceleration;
        speedF = MoveToward(speedF, target, rate * grip01 * dt);

        // ---- Lateral grip: shed sideways slide (less while handbraking = drift) ----
        float g = handbrake ? handbrakeGrip : grip;
        lat *= Mathf::Clamp(1.0f - g * grip01 * dt, 0.0f, 1.0f);

        // ---- Reassemble velocity (keep vertical for gravity) ----
        Vec3 planar = fp * speedF + rp * lat;
        m_speed = speedF;
        if (suspension) {
            ApplySuspension(dt, rb, planar, speedF, lat, grip01 > 0.0f);
        } else if (rb) {
            // No wheel down and no air control: the wheels do nothing, physics keeps the velocity.
            if (grip01 > 0.0f) {
                rb->velocity.x = planar.x;
                rb->velocity.z = planar.z;
            }
            // leave rb->velocity.y to gravity/collision
        } else {
            m_kinematicVel = {planar.x, 0.0f, planar.z};
            transform->Translate(Vec3{planar.x, 0.0f, planar.z} * dt);
        }
    }

public:

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (m_driver && dying.count(m_driver)) m_driver = nullptr;   // never keep a freed driver
        m_wheelScanT = 0.0f;                                          // re-scan the wheels soon
        for (size_t i = m_wheels.size(); i-- > 0;)
            if (dying.count(m_wheels[i].root) || dying.count(m_wheels[i].tire))
                m_wheels.erase(m_wheels.begin() + (long)i);
    }

private:
    static Vec3 Planar(const Vec3& v) {
        Vec3 p{v.x, 0.0f, v.z};
        float m = std::sqrt(p.x * p.x + p.z * p.z);
        return m > 1e-5f ? Vec3{p.x / m, 0.0f, p.z / m} : Vec3{0.0f, 0.0f, 1.0f};
    }
    static float MoveToward(float a, float b, float maxDelta) {
        float d = b - a;
        if (std::fabs(d) <= maxDelta) return b;
        return a + (d > 0.0f ? maxDelta : -maxDelta);
    }
    struct Wheel { GameObject* root = nullptr; GameObject* tire = nullptr; bool front = false; float radius = 0.35f; float spin = 0.0f; };
    static bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
    void FindWheels(Transform* t) {
        for (Transform* c : t->Children()) {
            if (!c || !c->gameObject) continue;
            const std::string& n = c->gameObject->name;
            if (StartsWith(n, "Wheel")) {
                for (Transform* k : c->Children()) {
                    if (k && k->gameObject && k->gameObject->name == "Tire") {
                        Wheel w;
                        w.root = c->gameObject; w.tire = k->gameObject;
                        w.front = n.find("Front") != std::string::npos;
                        const float r = std::fabs(k->localScale.x) * 0.5f;
                        if (r > 0.01f) w.radius = r;
                        m_wheels.push_back(w);
                        break;
                    }
                }
            }
            FindWheels(c);
        }
    }
    // Roll and steer the wheel objects. `steer` is the -1..1 steering input (0 with nobody at the wheel).
    void AnimateWheels(float dt, float speed, float steer) {
        // Look for wheels on the first call and then twice a second, so wheels added later are found.
        m_wheelScanT -= dt;
        if (transform && (!m_wheelsFound || m_wheelScanT <= 0.0f)) {
            std::vector<Wheel> old;
            old.swap(m_wheels);
            FindWheels(transform);
            for (Wheel& w : m_wheels)
                for (const Wheel& o : old) if (o.root == w.root) { w.spin = o.spin; break; }
            m_wheelsFound = true;
            m_wheelScanT = 0.5f;
        }
        const float k = 1.0f - std::exp(-12.0f * dt);
        m_steerAngle += (steer * kMaxSteerAngle - m_steerAngle) * k;
        if (m_wheels.empty()) {   // still track the roll angle so WheelSpin() works with no wheel objects
            m_wheelSpin = std::fmod(m_wheelSpin + speed / 0.35f * dt * 57.2957795f, 360.0f);
            return;
        }
        for (Wheel& w : m_wheels) {
            w.spin = std::fmod(w.spin + speed / w.radius * dt * 57.2957795f, 360.0f);
            if (w.root->transform) w.root->transform->localRotation = Quat::Euler(0.0f, w.front ? -m_steerAngle : 0.0f, 0.0f);   // + steer = right = clockwise from above = negative yaw
            if (w.tire->transform)
                w.tire->transform->localRotation = (Quat::AngleAxis(w.spin, Vec3{1, 0, 0}) * Quat::Euler(0.0f, 0.0f, 90.0f)).Normalized();
        }
        m_wheelSpin = m_wheels[0].spin;
    }
    // A car driven by someone else (a network proxy): roll the wheels from how far it moved along its heading.
    // Steering is not known here, so the front wheels stay straight.
    void ProxyWheels(float dt) {
        if (!transform) return;
        const Vec3 pos = transform->Position();
        if (m_proxyInit) {
            const Vec3 fp = Planar(transform->Forward());
            const Vec3 d = pos - m_proxyPos;
            AnimateWheels(dt, (d.x * fp.x + d.z * fp.z) / dt, 0.0f);
        }
        m_proxyPos = pos; m_proxyInit = true;
    }
    // Share of the wheels on the ground (0..1), and m_grounded. Arcade mode: one ray from the
    // centre along the car's OWN down direction, so a car on its roof (the ray points up) or on
    // its side counts as off the ground. Suspension mode: the four wheel rays (the spring keeps
    // that body upright), share = wheels that hit / 4. The car's own colliders, children
    // included, are never hit.
    float GroundShare(const Vec3& fp, const Vec3& rp) {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (suspension) {
            CastWheels(sc, fp, rp);
            m_groundShare = (float)m_wheelHits / 4.0f;
        } else if (sc && transform) {
            Physics3D::IgnoreHierarchy own(gameObject);
            const Vec3 down = transform->Up() * -1.0f;
            m_groundShare = sc->physics3D().Raycast(*sc, transform->Position(), down,
                                                    groundCheckDistance, gameObject).hit ? 1.0f : 0.0f;
        } else {
            m_groundShare = 1.0f;
        }
        m_grounded = m_groundShare > 0.0f;
        return m_groundShare;
    }
    // The suspension's four wheel rays, straight down from the wheelBase x trackWidth corners
    // (0 front-right, 1 front-left, 2 rear-right, 3 rear-left). Each starts a little above the
    // chassis and reaches rideHeight + travel below it, so a wheel finds the ground whether the
    // spring is compressed or drooping. A wheel that misses rests at full droop.
    void CastWheels(Scene* sc, const Vec3& fp, const Vec3& rp) {
        const Vec3 pos = transform->Position();
        const float hb = wheelBase * 0.5f, ht = trackWidth * 0.5f;
        const Vec3 corner[4] = { pos + fp * hb + rp * ht, pos + fp * hb - rp * ht,
                                 pos - fp * hb + rp * ht, pos - fp * hb - rp * ht };
        const float topMargin = 0.3f, maxRay = topMargin + rideHeight + suspensionTravel;
        m_wheelHits = 0; m_wheelSumY = 0.0f;
        Physics3D::IgnoreHierarchy own(gameObject);   // never stand on our own wheels
        for (int i = 0; i < 4; ++i) {
            RaycastHit3D h{};
            if (sc) h = sc->physics3D().Raycast(*sc, corner[i] + Vec3{0, topMargin, 0},
                                                Vec3{0, -1, 0}, maxRay, gameObject);
            if (h.hit) { m_wheelY[i] = h.point.y; m_wheelSumY += h.point.y; ++m_wheelHits; }
            else       { m_wheelY[i] = corner[i].y - suspensionTravel; }
        }
    }
    // Per-wheel raycast suspension: from the four wheel rays (CastWheels), run a
    // critically-damped vertical spring toward rideHeight (via AddForce so gravity
    // composes through the integrator), and tilt the chassis from per-wheel
    // compression plus dynamic accel/cornering lean.
    void ApplySuspension(float dt, Rigidbody3D* rb, const Vec3& planar, float speedF, float lat,
                         bool wheelsPush) {
        // The wheel rays were cast by GroundShare at the start of this drive step.
        const Vec3 pos = transform->Position();
        const float* groundY = m_wheelY;
        const int nHit = m_wheelHits;
        const float sumGround = m_wheelSumY;

        // ---- Vertical spring toward (average ground + rideHeight) ----
        float velY = rb ? rb->velocity.y : m_kinematicVel.y;
        if (m_grounded) {
            float targetY = sumGround / (float)nHit + rideHeight;
            float gUp = 9.81f * (rb ? rb->gravityScale : 1.0f);   // feed-forward gravity
            float a = springStrength * (targetY - pos.y) - springDamping * velY + gUp;
            if (rb) rb->AddForce(Vec3{0, a * rb->mass, 0});       // composes with gravity in Step
            else    velY += (a - 9.81f) * dt;                     // kinematic: integrate net accel
        } else if (!rb) {
            velY -= 9.81f * dt;                                   // kinematic free-fall
        }

        // ---- Chassis tilt: terrain slope + dynamic accel/cornering lean ----
        float frontY = (groundY[0] + groundY[1]) * 0.5f, rearY = (groundY[2] + groundY[3]) * 0.5f;
        float rightY = (groundY[0] + groundY[2]) * 0.5f, leftY = (groundY[1] + groundY[3]) * 0.5f;
        float pitchT = m_grounded ? std::atan2(frontY - rearY, wheelBase)  * 57.2957795f : 0.0f;
        float rollT  = m_grounded ? std::atan2(rightY - leftY, trackWidth) * 57.2957795f : 0.0f;
        float accel = (speedF - m_prevSpeedF) / (dt > 1e-4f ? dt : 1e-4f);
        m_prevSpeedF = speedF;
        pitchT += -accel * bodyLean;          // accelerate => nose up, brake => dive
        rollT  += lat * bodyLean * 2.0f;      // lean out of the corner
        pitchT = Mathf::Clamp(pitchT, -maxTilt, maxTilt);
        rollT  = Mathf::Clamp(rollT,  -maxTilt, maxTilt);
        float t = tiltSmooth > 0.0f ? (1.0f - std::exp(-tiltSmooth * dt)) : 1.0f;
        m_pitch += (pitchT - m_pitch) * t;
        m_roll  += (rollT  - m_roll)  * t;
        transform->localRotation = Quat::Euler(m_pitch, m_yaw, m_roll);

        // ---- Commit velocity (with no wheel down and no air control, physics keeps it) ----
        if (rb) { if (wheelsPush) { rb->velocity.x = planar.x; rb->velocity.z = planar.z; } }
        else {
            m_kinematicVel = {planar.x, velY, planar.z};
            transform->Translate(Vec3{planar.x, velY, planar.z} * dt);
        }
    }
    void UpdateCamera(float dt) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s || !s->mainCamera || !s->mainCamera->gameObject) return;
        Transform* ct = s->mainCamera->gameObject->transform;
        if (!ct || ct == transform) return;
        Vec3 fp = Planar(transform->Forward());           // flat heading (stable when stopped)
        Vec3 carPos = transform->Position();
        // Sit behind and above the car.
        Vec3 want = carPos - fp * camDistance + Vec3{0, camHeight, 0};
        float t = camLerp > 0.0f ? (1.0f - std::exp(-camLerp * dt)) : 1.0f;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        Vec3 np = ct->Position();
        np = np + (want - np) * t;
        ct->SetPosition(np);
        // Aim at a point a little ABOVE the car and slightly AHEAD of it, not at the
        // car's base — otherwise a camera mounted up high just stares down at the roof.
        // This frames the road ahead like a real arcade racer. The look target follows
        // the heading so cornering reveals where you're going.
        Vec3 lookAt = carPos + Vec3{0, camHeight * 0.45f, 0} + fp * (camDistance * 0.35f);
        Vec3 look = lookAt - np;
        if (look.x * look.x + look.y * look.y + look.z * look.z > 1e-6f)
            ct->SetRotation(Quat::LookRotation(look * -1.0f));   // a camera looks down its -Z
    }

    GameObject* m_driver = nullptr;      // runtime only, set by VehicleSeat
    int   m_driverSeats = 0;             // runtime only: live driver seats (VehicleSeat)
    float m_speed = 0.0f;
    bool  m_grounded = true;
    float m_groundShare = 1.0f;          // wheels on the ground at the last drive step (0..1)
    float m_wheelY[4] = {0, 0, 0, 0};    // suspension: ground height under each wheel
    float m_wheelSumY = 0.0f;
    int   m_wheelHits = 0;
    // Input read in Update, used by the drive steps (runtime only).
    float m_throttleIn = 0.0f, m_steerIn = 0.0f;
    bool  m_handbrakeIn = false, m_unmanned = false;
    Vec3  m_kinematicVel{0, 0, 0};
    // Suspension state: scalar yaw (so pitch/roll compose), smoothed tilt.
    float m_yaw = 0.0f, m_pitch = 0.0f, m_roll = 0.0f, m_prevSpeedF = 0.0f;
    bool  m_yawInit = false;
    // Wheel visuals (runtime only).
    std::vector<Wheel> m_wheels;
    bool  m_wheelsFound = false;
    float m_wheelSpin = 0.0f, m_steerAngle = 0.0f;
    bool  m_seatCamera = false;
    float m_wheelScanT = 0.0f;
    Vec3  m_proxyPos{0, 0, 0};
    bool  m_proxyInit = false;
    unsigned m_uid = 0;
    static unsigned& NextUid() { static unsigned n = 0; return n; }
    static std::vector<std::pair<const VehicleController*, unsigned>>& Live() {
        static std::vector<std::pair<const VehicleController*, unsigned>> v;
        return v;
    }
};

} // namespace okay
