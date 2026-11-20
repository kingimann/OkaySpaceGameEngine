#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/GroundGrip.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Components/VehicleTuning.hpp"
#include "okay/Core/DrivingSettings.hpp"
#include "okay/Components/ActionList.hpp"
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
class VehicleController : public Behaviour, public VehicleTuning {
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

    // The tunable numbers (acceleration, maxSpeed, reverseSpeed, brakeForce, drag, turnSpeed, grip, handbrakeGrip and the
    // open-world settings) live in VehicleTuning.hpp, one table for the Inspector, scene files, presets and scripts.
    float groundCheckDistance = 1.2f;    // down-ray length for "on the ground"

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

    bool  usePlayerSettings = true;      ///< open-world model: apply the player's Driving options (Prefs) when it starts
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
        const bool unmanned = m_driverSeats > 0 && !m_driver;
        if (openWorld) { UpdateOpenWorld(dt, unmanned); return; }
        float throttle = 0.0f, steer = 0.0f;
        bool handbrake = false;
        if (!unmanned) {
            Vec2 axis = Input::AxisWASD();
            throttle = axis.y; steer = axis.x;
            handbrake = handbrakeKey && Input::GetKey(handbrakeKey);
        }

        // Planar facing axes (flatten Y so slopes don't change the drive plane).
        Vec3 F = transform->Forward(); Vec3 R = transform->Right();
        Vec3 fp = Planar(F), rp = Planar(R);

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        Vec3 vel = rb ? rb->velocity : m_kinematicVel;
        float speedF = vel.x * fp.x + vel.z * fp.z;       // velocity along facing
        float lat    = vel.x * rp.x + vel.z * rp.z;       // sideways velocity

        // ---- Steering: scales in as you gain speed; reverses when backing up ----
        float steerScale = Mathf::Clamp(std::fabs(speedF) / 4.0f, 0.0f, 1.0f);
        float dirSign = (speedF >= -0.05f) ? 1.0f : -1.0f;
        // A positive yaw is counter-clockwise seen from above, which is a LEFT turn for any heading. D (steer > 0)
        // must turn right, so the yaw is the negative of the steering.
        float yawDelta = (std::fabs(steer) > 0.001f && steerScale > 0.001f)
                       ? -steer * turnSpeed * steerScale * dirSign * dt : 0.0f;
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
        const float groundGrip = GroundGripScale(gameObject);   // ice: the wheels barely bite; rubber: they bite more
        speedF = MoveToward(speedF, target, rate * groundGrip * dt);

        // ---- Lateral grip: shed sideways slide (less while handbraking = drift) ----
        float g = handbrake ? handbrakeGrip : grip;
        lat *= Mathf::Clamp(1.0f - g * groundGrip * dt, 0.0f, 1.0f);

        // ---- Reassemble velocity (keep vertical for gravity) ----
        Vec3 planar = fp * speedF + rp * lat;
        m_speed = speedF;
        AnimateWheels(dt, speedF, steer);
        if (suspension) {
            ApplySuspension(dt, rb, fp, rp, planar, speedF, lat);
        } else {
            UpdateGrounded();
            if (rb) {
                rb->velocity.x = planar.x;
                rb->velocity.z = planar.z;
                // leave rb->velocity.y to gravity/collision
            } else {
                m_kinematicVel = {planar.x, 0.0f, planar.z};
                transform->Translate(Vec3{planar.x, 0.0f, planar.z} * dt);
            }
        }

        if (followCamera && !unmanned && !m_seatCamera) UpdateCamera(dt);
    }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (m_driver && dying.count(m_driver)) m_driver = nullptr;   // never keep a freed driver
        m_wheelScanT = 0.0f;                                          // re-scan the wheels soon
        for (size_t i = m_wheels.size(); i-- > 0;)
            if (dying.count(m_wheels[i].root) || dying.count(m_wheels[i].tire))
                m_wheels.erase(m_wheels.begin() + (long)i);
    }

    // ================= Open-world driving model (openWorld = true) =================
    // Written from general vehicle ideas: a bicycle model for turning, a friction limit for grip, a torque curve and gear
    // ratios for the engine. See docs/gameplay/vehicle_controls.md.

    /// One frame of driver input. A script, a test or an AI sets it with SetInput; otherwise the keyboard / pad is read.
    struct DriveInput {
        float throttle = 0, brake = 0, steer = 0;   ///< 0..1, 0..1, -1..1 (+ = right)
        bool handbrake = false, boost = false, reverse = false;
    };
    /// Whole-input setter for tests and AI: holds until ClearInput(). The one-value setters below (what scripts call) expire
    /// half a second after the last call, so a script that stops calling hands the keys back.
    void SetInput(const DriveInput& in) { m_in = in; m_scripted = true; m_holdT = -1.0f; }
    void ClearInput() { m_scripted = false; m_in = DriveInput{}; m_holdT = -1.0f; }
    void SetThrottle(float v) { Hold(); m_in.throttle = Mathf::Clamp(v, 0.0f, 1.0f); }
    void SetBrake(float v)    { Hold(); m_in.brake = Mathf::Clamp(v, 0.0f, 1.0f); }
    void SetSteer(float v)    { Hold(); m_in.steer = Mathf::Clamp(v, -1.0f, 1.0f); }
    void SetHandbrake(bool on) { Hold(); m_in.handbrake = on; }
    /// Boost on / off (needs boostEnabled and some boost left).
    void Boost(bool on) { Hold(); m_in.boost = on; }
    void Horn(bool on) { m_horn = on; }
    bool HornOn() const { return m_horn; }
    /// Headlights: also turns every child object whose name starts with "Headlight" on or off.
    void SetLights(bool on) {
        m_lights = on;
        if (transform) SetNamedChildren(transform, "Headlight", on);
    }
    bool LightsOn() const { return m_lights; }
    /// (To throw the driver out beside the car, use VehicleSeat::EjectDriver(car).)
    /// Apply a built-in preset ("Sports Car", ...) or one loaded from a file. Runtime state is reset.
    bool ApplyPreset(const std::string& name) {
        if (!VehicleTuning::ApplyPreset(name)) return false;
        m_presetName = name; ResetRuntime(); return true;
    }
    const std::string& PresetName() const { return m_presetName; }
    /// Load / save a .okayvehicle text (see VehicleTuning::ToFile). The text is made here; reading and writing the file is up to the caller.
    std::string SavePresetText(const std::string& name) const { return ToFile(name); }
    bool LoadPresetText(const std::string& text) {
        std::string n; if (!FromFile(text, &n)) return false;
        m_presetName = n; ResetRuntime(); return true;
    }
    /// Grip of the ground right now (1 = tarmac). Set by a script or a test; -1 = read the surface under the car.
    void SetSurfaceGrip(float g) { m_surfaceOverride = g; }
    float Rpm() const { return m_rpm; }
    int   Gear() const { return m_reversing ? -1 : m_gear; }          ///< -1 = reverse, 0 = neutral at a standstill, 1.. forward
    float SlipAngle() const { return m_slip; }                         ///< degrees between where the car points and where it goes
    bool  IsDrifting() const { return m_drifting; }
    float BoostLeft() const { return m_boostLeft; }                    ///< 0..1
    bool  IsFlipped() const { return m_flipped; }
    float Handling01() const { return handling; }
    int   CameraView() const { return m_view; }
    float SteerWheelAngle() const { return m_wheelAngle; }
    float LongAccel() const { return m_aLong; }
    /// Put the car upright again right now (the Recover button does this).
    void Recover() {
        if (!transform) return;
        if (!m_init) { const Vec3 F = transform->Forward(); m_yaw = std::atan2(F.x, F.z) * kDeg; }
        const Vec3 p = transform->Position();
        m_pitch = m_roll = 0.0f;
        transform->localRotation = Quat::Euler(0.0f, m_yaw, 0.0f);
        transform->SetPosition(p + Vec3{0, 0.8f, 0});
        if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr) rb->velocity = Vec3{0, 0, 0};
        m_vx = m_vz = 0.0f; m_speed = 0.0f; m_flipped = false; m_flipT = 0.0f;
    }

private:
    // ---------------- open-world model internals ----------------
    static constexpr float kDeg = 57.2957795f;
    static float Smooth01(float x) { x = Mathf::Clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
    static void SetNamedChildren(Transform* t, const char* prefix, bool on) {
        for (Transform* c : t->Children()) {
            if (!c || !c->gameObject) continue;
            if (StartsWith(c->gameObject->name, prefix)) c->gameObject->active = on;
            SetNamedChildren(c, prefix, on);
        }
    }
    void ResetRuntime() {
        m_vx = m_vz = 0; m_rpm = idleRpm; m_gear = 1; m_shiftT = 0; m_boostLeft = 1; m_wheelAngle = 0;
        m_slip = 0; m_drifting = false; m_flipped = false; m_flipT = 0; m_reversing = false; m_thrS = m_brkS = m_steerS = 0;
    }
    float GearTop(int g) const {                       // top speed of gear g (1-based)
        const int n = std::max(1, gearCount);
        return maxSpeed * std::pow((float)g / (float)n, 0.8f);
    }
    float TorqueCurve(float x) const {                 // 0..1 pull at rpm fraction x
        const float w = std::max(torquePeak, 1.0f - torquePeak);
        float c = 1.0f - ((x - torquePeak) / w) * ((x - torquePeak) / w);
        c = std::max(0.35f, c);
        return c * Mathf::Clamp((1.03f - x) / 0.05f, 0.0f, 1.0f);   // limiter
    }
    float SurfaceGrip() {
        if (m_surfaceOverride >= 0.0f) return m_surfaceOverride;
        if (m_surfaceCheckT > 0.0f) return m_surfaceCache;
        m_surfaceCheckT = 0.1f; m_surfaceCache = 1.0f;
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (sc && transform) {
            RaycastHit3D h = sc->physics3D().Raycast(*sc, transform->Position(), Vec3{0, -1, 0}, groundCheckDistance + 0.6f, gameObject);
            m_grounded = h.hit;
            if (h.hit && h.gameObject) {
                const std::string& t = h.gameObject->tag;
                if (t == "Ice") m_surfaceCache = iceGrip;
                else if (t == "Mud") m_surfaceCache = mudGrip;
                else if (t == "Grass") m_surfaceCache = grassGrip;
            }
        }
        return m_surfaceCache;
    }
    float Dead(float v) const {
        const float a = std::fabs(v);
        if (a <= deadZone) return 0.0f;
        return (v < 0 ? -1.0f : 1.0f) * (a - deadZone) / std::max(0.01f, 1.0f - deadZone);
    }
    bool KeyHeld(char k, char arrow = 0) const { return Input::GetKey(k) || (arrow && Input::GetKey(arrow)); }
    DriveInput ReadInput(float dt) {
        DriveInput in;
        if (m_scripted) return m_in;
        in.throttle = KeyHeld(keyThrottle, keyThrottle == 'w' ? Input::KeyUp : 0) ? 1.0f : 0.0f;
        in.brake = KeyHeld(keyBrake, keyBrake == 's' ? Input::KeyDown : 0) ? 1.0f : 0.0f;
        float st = 0.0f;
        if (KeyHeld(keySteerRight, keySteerRight == 'd' ? Input::KeyRight : 0)) st += 1.0f;
        if (KeyHeld(keySteerLeft, keySteerLeft == 'a' ? Input::KeyLeft : 0)) st -= 1.0f;
        in.steer = st;
        in.handbrake = handbrakeKey && Input::GetKey(handbrakeKey);
        in.boost = Input::GetKey(keyBoost);
        in.reverse = Input::GetKey(keyReverse);
        if (Input::GamepadConnected(0)) {   // gamepad: right trigger gas, left trigger brake / reverse, left stick steer
            const float rt = Dead(Input::GamepadTrigger(1)), lt = Dead(Input::GamepadTrigger(0));
            const float sx = Dead(Input::GamepadAxis(0).x);
            in.throttle = std::max(in.throttle, rt);
            in.brake = std::max(in.brake, lt);
            if (std::fabs(sx) > 0.0f) in.steer = sx;
            in.handbrake = in.handbrake || Input::GetGamepadButton(padHandbrake);
            in.boost = in.boost || Input::GetGamepadButton(padBoost);
        }
        (void)dt;
        return in;
    }

    void UpdateOpenWorld(float dt, bool unmanned) {
        if (!m_init) {
            Vec3 F = transform->Forward();
            m_yaw = std::atan2(F.x, F.z) * kDeg; m_init = true; m_boostLeft = 1.0f; m_rpm = idleRpm; m_view = camView;
            ResetRuntimeKeepView();
            m_drv = DrivingSettings::Load();   // read once, not every frame
            if (usePlayerSettings) ApplyPlayerSettings();
        }
        m_surfaceCheckT -= dt;
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        Vec3 vel = rb ? rb->velocity : m_kinematicVel;
        // Ground friction in the physics step nibbles the speed we set last frame. Keep our own speed unless something
        // really changed it (a wall, a hit, a script or a push bigger than that).
        if (m_haveCmd && rb) {
            const float dx = vel.x - m_cmdX, dz = vel.z - m_cmdZ, thr = 0.05f + 12.0f * dt;
            if (dx * dx + dz * dz < thr * thr) { vel.x = m_cmdX; vel.z = m_cmdZ; }
        }
        m_vx = vel.x; m_vz = vel.z;

        m_unmanned = unmanned;
        if (m_scripted && m_holdT >= 0.0f) { m_holdT -= dt; if (m_holdT <= 0.0f) ClearInput(); }
        DriveInput in = unmanned ? DriveInput{} : ReadInput(dt);
        if (unmanned) in.brake = 1.0f;                        // nobody at the wheel: brakes on
        // Shape the raw inputs (smoothing, sensitivity, dead zone, invert).
        const float sm = inputSmoothing > 0.0f ? 1.0f - std::exp(-inputSmoothing * dt) : 1.0f;
        m_thrS += (in.throttle - m_thrS) * sm;
        m_brkS += (in.brake - m_brkS) * sm;
        float stRaw = Mathf::Clamp(in.steer * steerSensitivity * m_steerMul, -1.0f, 1.0f) * ((invertSteer != m_invert) ? -1.0f : 1.0f);
        m_steerS += (stRaw - m_steerS) * sm;
        in.throttle = m_thrS; in.brake = m_brkS;

        // Buttons pressed once (not scripted: scripts call the functions).
        if (!m_scripted && !unmanned) {
            if (Input::GetKeyDown(keyRecover) || Input::GetGamepadButtonDown(padRecover)) Recover();
            if (Input::GetKeyDown(keyLights) || Input::GetGamepadButtonDown(padLights)) SetLights(!m_lights);
            m_horn = Input::GetKey(keyHorn) || Input::GetGamepadButton(padHorn);
            if (Input::GetKeyDown(keyCamera) || Input::GetGamepadButtonDown(padCamera)) m_view = (m_view + 1) % 4;
            m_lookBack = Input::GetKey(keyLookBack);
            if (gearbox != 0) {
                if (Input::GetKeyDown(keyGearUp) || Input::GetGamepadButtonDown(padGearUp)) ShiftGear(+1);
                if (Input::GetKeyDown(keyGearDown) || Input::GetGamepadButtonDown(padGearDown)) ShiftGear(-1);
            }
        }

        // Flipped (something outside tipped the car over): no driving until it is back on its wheels.
        const float upY = transform->Up().y;
        if (upY < 0.3f || (m_flipped && upY < 0.45f)) {   // hysteresis; our own air tilt is clamped well above this
            m_flipped = true; m_flipT += dt; m_speed = 0.0f;
            if ((autoFlip && m_flipT > 2.0f) || (!unmanned && !m_scripted && Input::GetKeyDown(keyRecover))) Recover();
            return;
        }
        m_flipped = false; m_flipT = 0.0f;

        // Fixed small steps keep the result the same at 30, 60 or 144 frames per second.
        const float maxStep = 1.0f / 120.0f;
        int n = (int)std::ceil(dt / maxStep); if (n < 1) n = 1; if (n > 64) n = 64;
        const float h = dt / (float)n;
        const float yawBefore = m_yaw;
        const float vfBefore = m_speed;
        const Vec3 velBefore{m_vx, 0, m_vz};
        for (int i = 0; i < n; ++i) Step(h, in, std::fabs(m_steerS) > 0.0f ? m_steerS : 0.0f);
        (void)yawBefore; (void)velBefore;

        // Body lean (weight transfer) and air control.
        const float lerpK = 1.0f - std::exp(-9.0f * dt);
        const float aLatNow = m_speed * m_yawRate;                      // sideways acceleration felt (right turn > 0)
        const float aLongRaw = (m_speed - vfBefore) / dt;
        m_aLong += (aLongRaw - m_aLong) * (1.0f - std::exp(-6.0f * dt));
        const float stiff = 1.0f - 0.7f * antiRoll;
        float pT = Mathf::Clamp(-m_aLong * 0.35f * leanAmount, -6.0f, 6.0f);
        float rT = Mathf::Clamp(-aLatNow * 0.9f * leanAmount * stiff, -8.0f, 8.0f);
        UpdateGroundedOpen();
        if (!m_grounded) {
            const float vh = std::sqrt(m_vx * m_vx + m_vz * m_vz);
            const float vy = rb ? rb->velocity.y : 0.0f;
            pT = Mathf::Clamp(-std::atan2(vy, std::max(vh, 1.0f)) * kDeg * 0.8f, -35.0f, 35.0f);
            m_airPitch = Mathf::Clamp(m_airPitch + (in.brake - in.throttle) * -airControl * 45.0f * dt, -30.0f, 30.0f);
            m_airRoll = Mathf::Clamp(m_airRoll + in.steer * airControl * 60.0f * dt, -35.0f, 35.0f);
            pT = Mathf::Clamp(pT + m_airPitch, -45.0f, 45.0f); rT = Mathf::Clamp(m_airRoll, -30.0f, 30.0f);
        } else { m_airPitch *= std::exp(-4.0f * dt); m_airRoll *= std::exp(-4.0f * dt); }
        m_pitch += (pT - m_pitch) * lerpK;
        m_roll  += (rT - m_roll)  * lerpK;
        transform->localRotation = Quat::Euler(m_pitch, m_yaw, m_roll);

        // Hit something: a big sudden loss of speed.
        const float loss = vfBefore - m_speed;
        if (std::fabs(vfBefore) > 6.0f && loss > 7.0f * dt * 60.0f * 0.5f) { m_shake = std::min(1.0f, loss * 0.08f); Input::Rumble(0.7f, 0.4f, 0.25f); }
        m_shake *= std::exp(-6.0f * dt);
        if (m_drifting && !unmanned) Input::Rumble(0.0f, 0.25f, 0.1f);

        // Commit.
        AnimateWheels(dt, m_speed, Mathf::Clamp(m_wheelAngle / std::max(1.0f, maxSteerAngle), -1.0f, 1.0f));
        if (rb) { rb->velocity.x = m_vx; rb->velocity.z = m_vz; m_cmdX = m_vx; m_cmdZ = m_vz; m_haveCmd = true; }
        else {
            m_kinematicVel = {m_vx, 0.0f, m_vz};
            transform->Translate(Vec3{m_vx, 0.0f, m_vz} * dt);
        }
        if (!unmanned) PublishHud();
        if (followCamera && !unmanned && !m_seatCamera) UpdateOpenCamera(dt);
    }
    // Speedometer and gear for a HUD text: the UI kit's Text Bind with the format
    // "{vehicle_speed|0} {vehicle_unit|km/h}  Gear {vehicle_gear|N}" shows them (no new component).
    void PublishHud() {
        const DrivingSettings& d = m_drv;
        ActionList::Vars()["vehicle_speed"] = std::floor(d.DisplaySpeed(m_speed) + 0.5f);
        ActionList::Vars()["vehicle_rpm"] = std::floor(m_rpm);
        ActionList::Vars()["vehicle_boost"] = std::floor(m_boostLeft * 100.0f);
        const int g = Gear();
        ActionList::StrVars()["vehicle_gear"] = g < 0 ? "R" : (std::fabs(m_speed) < 0.3f && g == 1 ? "N" : std::to_string(g));
        ActionList::StrVars()["vehicle_unit"] = d.UnitName();
    }
    // The player's own driving options (Prefs "driving.*", see DrivingSettings.hpp). Only when the player saved something.
    void ApplyPlayerSettings() {
        const DrivingSettings d = m_drv;
        if (!d.hasUserChoice) return;
        // Runtime-only copies: the saved settings are never edited, so saving during play changes nothing.
        m_steerMul = d.steerSensitivity;
        m_invert = d.invertSteer;
        if (d.cameraView >= 0) m_view = d.cameraView;
        m_assistsOff = !d.assists;
    }
    void ResetRuntimeKeepView() { m_gear = 1; m_shiftT = 0; }
    void UpdateGroundedOpen() {
        if (m_surfaceOverride >= 0.0f || m_surfaceCheckT <= 0.0f) {   // refresh the ground flag
            if (m_surfaceOverride >= 0.0f) UpdateGrounded();
            else SurfaceGrip();
        }
    }
    void ShiftGear(int d) {
        const int g = m_gear + d;
        if (g < 1 || g > std::max(1, gearCount)) return;
        m_gear = g; m_shiftT = shiftTime;
    }

    // One fixed step of the driving model.
    void Step(float h, const DriveInput& in, float /*steerShape*/) {
        const float a = handling;                                     // 0 realistic .. 1 arcade
        const float yawRad = m_yaw / kDeg;
        float fx = std::sin(yawRad), fz = std::cos(yawRad);           // facing
        float rx = -fz, rz = fx;                                      // car's right
        float vf = m_vx * fx + m_vz * fz;
        const float vAbs = std::fabs(vf);
        const float surface = SurfaceGrip();
        const float mu = surface * 0.5f * (frontGrip + rearGrip);
        const float g = 9.81f;

        // ---- Gas, brake and reverse (hold the brake at a standstill to reverse, release to roll) ----
        float push = 0.0f, braking = 0.0f; bool rev = false;
        if (reverseMode == 0) {
            if (in.brake > 0.05f && in.throttle <= 0.05f) { if (vf > 0.5f) braking = in.brake; else { push = in.brake; rev = true; } }
            else if (in.throttle > 0.05f && in.brake <= 0.05f) { if (vf < -0.5f) braking = in.throttle; else push = in.throttle; }
            else if (in.brake > 0.05f) braking = in.brake;                          // both held: brake
        } else {
            if (in.reverse) { if (vf > 0.5f) braking = 1.0f; else { push = std::max(in.throttle, 0.6f); rev = true; } }
            else push = in.throttle;
            braking = std::max(braking, in.brake);
        }
        if (m_unmanned) { push = 0.0f; braking = 1.0f; rev = false; }   // nobody at the wheel: brake to a stop, never reverse away
        m_reversing = rev;
        if (m_shiftT > 0.0f) m_shiftT -= h;

        // ---- Engine, gears and rpm ----
        const bool boosting = boostEnabled && in.boost && m_boostLeft > 0.0f && !rev;
        const float boostMul = boosting ? 1.12f : 1.0f;
        const int N = std::max(1, gearCount);
        if (gearbox == 0 && !rev && m_shiftT <= 0.0f) {                              // automatic
            const float x = vAbs / (GearTop(m_gear) * boostMul);
            if (x > 0.93f && m_gear < N && push > 0.05f) { m_gear++; m_shiftT = shiftTime; }
            else if (m_gear > 1 && x < 0.40f) { m_gear--; m_shiftT = shiftTime * 0.5f; }
        }
        if (gearbox != 0 && m_gear > N) m_gear = N;
        const float gTop = rev ? reverseSpeed : GearTop(m_gear) * boostMul;
        const float x = Mathf::Clamp(vAbs / std::max(0.1f, gTop), 0.0f, 1.2f);
        const float rpmTarget = idleRpm + (maxRpm - idleRpm) * std::max(Mathf::Clamp(x, 0.0f, 1.0f), push * 0.2f);
        m_rpm += (rpmTarget - m_rpm) * (1.0f - std::exp(-14.0f * h));

        float aEng = 0.0f;
        if (push > 0.0f && m_shiftT <= 0.0f) {
            if (rev) aEng = acceleration * 0.6f * push * Mathf::Clamp(1.0f - vAbs / std::max(0.1f, reverseSpeed), 0.0f, 1.0f);
            else {
                const float ratio = std::pow((float)GearTop(N) / std::max(0.1f, GearTop(m_gear)), 0.7f);
                aEng = acceleration * ratio * TorqueCurve(x) * push;
            }
            if (boosting) aEng += boostPower * push;
        }
        // Tyre limit on the driven wheels: front/rear weight share, all-wheel uses both axles (weakest one counts).
        const float wf = 0.42f, wr = 0.58f;
        float frac = drivetrain == 0 ? wr : drivetrain == 1 ? wf
                   : std::min(wf / std::max(0.05f, torqueSplit), wr / std::max(0.05f, 1.0f - torqueSplit));
        frac = std::min(1.0f, frac * (1.0f + 0.15f * diffLock));
        const float aTrac = mu * g * frac * (1.0f + downforce * vAbs * 0.4f);
        bool spinning = false;
        if (aEng > aTrac) {
            if ((tractionControl && !m_assistsOff) || a > 0.85f) aEng = aTrac;
            else { aEng = aTrac * 0.85f; spinning = true; }
        }
        // Boost tank.
        if (boosting) m_boostLeft = std::max(0.0f, m_boostLeft - h / std::max(0.1f, boostDuration));
        else m_boostLeft = std::min(1.0f, m_boostLeft + boostRecharge * h);

        // ---- Brakes (ABS keeps the tyres at their grip limit; without it, locked tyres slide and stop slower) ----
        float aBrake = 0.0f;
        const float biasEff = Mathf::Clamp(1.0f - 1.2f * std::fabs(brakeBias - 0.6f), 0.6f, 1.0f);
        float demand = brakeForce * braking * biasEff + (in.handbrake ? handbrakeStrength * 0.5f * brakeForce : 0.0f);
        if (demand > 0.0f) {
            const float limit = mu * g * (1.0f + downforce * vAbs * 0.4f);
            aBrake = demand <= limit * 0.97f ? demand : (antiLock && !m_assistsOff ? limit * 0.97f : limit * 0.72f);
        }
        // ---- Longitudinal speed ----
        const float sgn = vf >= 0.0f ? 1.0f : -1.0f;
        const float dir = rev ? -1.0f : 1.0f;
        float dv = dir * aEng * h;
        if (push <= 0.0f && braking <= 0.0f && !in.handbrake) {
            const float coast = drag + engineBraking * Mathf::Clamp(m_rpm / maxRpm, 0.0f, 1.0f);
            const float s = std::min(vAbs, coast * h); dv -= sgn * s;
        }
        dv -= sgn * std::min(vAbs, aeroDrag * vAbs * vAbs * h);
        if (aBrake > 0.0f) dv -= sgn * std::min(vAbs, aBrake * h);
        // push towards the opposite direction of travel while pressing the other pedal is a brake, handled above
        vf += dv;
        m_vx += fx * dv; m_vz += fz * dv;                              // the push acts along the heading

        // ---- Steering (bicycle model, less lock at speed, counter-steer help in a slide) ----
        const float sf = 1.0f - steerSpeedSensitivity * 0.85f * Smooth01(vAbs / std::max(1.0f, maxSpeed));
        float target = m_steerS * maxSteerAngle * sf;
        const float vl0 = m_vx * rx + m_vz * rz;
        const float beta = vAbs > 3.0f ? std::atan2(vl0, std::fabs(vf)) * kDeg * (vf >= 0 ? 1.0f : -1.0f) : 0.0f;
        if (vAbs > 3.0f && counterSteerAssist > 0.0f && !m_assistsOff) {
            const float assist = counterSteerAssist * (0.3f + 0.7f * a);
            target += Mathf::Clamp(beta * assist * 1.1f, -maxSteerAngle, maxSteerAngle) * (1.0f - 0.5f * std::fabs(m_steerS));
            target = Mathf::Clamp(target, -maxSteerAngle, maxSteerAngle);
        }
        const bool wantSteer = std::fabs(m_steerS) > 0.02f;
        const float rate = wantSteer ? steerResponse : std::max(autoCentre, steerResponse);
        m_wheelAngle += (target - m_wheelAngle) * (1.0f - std::exp(-rate * h));
        const float wb = std::max(0.5f, wheelBase);
        float omega = vf / wb * std::tan(m_wheelAngle / kDeg);       // rad/s, + = right
        const float omegaMax = mu * (0.85f + 0.3f * a) * g / std::max(vAbs, 2.0f) * ((steerAssist && !m_assistsOff) ? 1.0f : 1.25f);
        omega = Mathf::Clamp(omega, -omegaMax, omegaMax);
        if (in.handbrake) omega *= 1.0f + driftAmount * 0.6f;       // the back lets go and swings round
        if (spinning && drivetrain != 1) omega *= 1.15f;
        m_yawRate = omega;
        m_yaw -= omega * h * kDeg;
        if (m_yaw > 360.0f) m_yaw -= 360.0f; else if (m_yaw < -360.0f) m_yaw += 360.0f;

        // ---- Sideways grip (the world velocity stays; the heading turned, so a slide shows up) ----
        const float ny = m_yaw / kDeg;
        fx = std::sin(ny); fz = std::cos(ny); rx = -fz; rz = fx;
        float vfN = m_vx * fx + m_vz * fz, vl = m_vx * rx + m_vz * rz;
        const float slip = vAbs > 1.0f ? std::atan2(vl, std::max(0.1f, std::fabs(vfN))) * kDeg : 0.0f;
        m_slip = slip;
        float rateG = grip * mu * (0.8f + 0.45f * a) * (1.0f + downforce * vAbs * 0.4f);
        if (in.handbrake) {
            const float hb = handbrakeGrip * (1.2f - driftAmount) * surface;
            rateG = std::min(rateG, std::max(0.05f, hb));
        }
        if (spinning && !in.handbrake) rateG *= 0.6f;
        if (std::fabs(slip) > slipLimit) rateG *= 0.5f;                             // past the limit the tyres let go
        else if (!in.handbrake && !m_assistsOff && stabilityControl > 0.0f && std::fabs(slip) > 10.0f) rateG *= 1.0f + 0.6f * stabilityControl;
        m_drifting = std::fabs(slip) > 12.0f && vAbs > 4.0f;
        const float dl = vl * (1.0f - std::exp(-rateG * h));
        vl -= dl;
        if (vAbs > 1.0f) vfN += (vfN >= 0 ? 1.0f : -1.0f) * std::fabs(dl) * 0.35f;  // a corner keeps most of the speed
        m_vx = fx * vfN + rx * vl; m_vz = fz * vfN + rz * vl;
        m_speed = vfN;
    }

    // The camera of the open-world model: views, drift swing, look-ahead, speed FOV and distance, collision, shake.
    void UpdateOpenCamera(float dt) {
        Scene* s0 = gameObject ? gameObject->scene() : nullptr;
        if (s0 && s0->mainCamera) DriveCamera(s0->mainCamera, dt);
    }
public:
    /// Move `cam` like the open-world chase camera (VehicleSeat calls this for a seated player, so the camera
    /// glides from the on-foot view to the car view). Only while openWorld.
    void DriveCamera(Camera* cam, float dt) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s || !cam || !cam->gameObject) return;
        Transform* ct = cam->gameObject->transform;
        if (!ct || ct == transform) return;
        const float yaw = m_yaw / kDeg;
        Vec3 fwd{std::sin(yaw), 0.0f, std::cos(yaw)};
        Vec3 right{-fwd.z, 0.0f, fwd.x};
        const float sp = Smooth01(std::fabs(m_speed) / std::max(1.0f, maxSpeed));
        const Vec3 carPos = transform->Position();
        Vec3 want, lookAt;
        if (m_view >= 2) {            // hood / bonnet: sit on the car
            const float up = m_view == 2 ? 1.25f : 0.95f, ahead = m_view == 2 ? 0.2f : 1.3f;
            const Vec3 dirv = m_lookBack ? fwd * -1.0f : fwd;
            want = carPos + Vec3{0, up, 0} + dirv * ahead;
            lookAt = want + dirv * 20.0f + right * (m_wheelAngle / std::max(1.0f, maxSteerAngle)) * -3.0f * camLookAhead;
        } else {
            const float zoomK = m_view == 1 ? 1.8f : 1.0f;
            // swing out in a drift: blend the heading towards the way the car is really moving
            Vec3 dirv = fwd;
            if (std::fabs(m_slip) > 3.0f) {
                const float sw = Mathf::Clamp(camDriftSwing * std::fabs(m_slip) / 35.0f, 0.0f, 0.8f);
                Vec3 vv{m_vx, 0.0f, m_vz}; const float m = std::sqrt(vv.x * vv.x + vv.z * vv.z);
                if (m > 1.0f) { vv = vv * (1.0f / m); dirv = fwd * (1.0f - sw) + vv * sw; const float dm = std::sqrt(dirv.x*dirv.x + dirv.z*dirv.z); if (dm > 1e-4f) dirv = dirv * (1.0f / dm); }
            }
            if (m_lookBack) dirv = dirv * -1.0f;
            const float dist = (camDistance + camSpeedDistance * sp) * zoomK;
            want = carPos - dirv * dist + Vec3{0, camHeight * zoomK, 0};
            lookAt = carPos + Vec3{0, camHeight * 0.45f, 0} + dirv * (camDistance * 0.35f)
                   + right * (m_wheelAngle / std::max(1.0f, maxSteerAngle)) * -camLookAhead * 4.0f * (m_lookBack ? -1.0f : 1.0f);
            // collision: pull the camera in if something is between the car and it
            const Vec3 from = carPos + Vec3{0, 1.0f, 0}; Vec3 d = want - from; const float len = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
            if (len > 0.1f) {
                RaycastHit3D h = s->physics3D().Raycast(*s, from, d * (1.0f / len), len, gameObject);
                if (h.hit && h.distance > 0.5f) want = from + d * ((h.distance - 0.3f) / len);
            }
        }
        const float t = (m_view >= 2) ? 1.0f : (camLerp > 0.0f ? 1.0f - std::exp(-camLerp * dt) : 1.0f);
        Vec3 np = ct->Position(); np = np + (want - np) * t;
        if (m_shake > 0.01f) np = np + Vec3{std::sin(m_shakeT * 91.0f), std::sin(m_shakeT * 77.0f + 1.0f), 0.0f} * (0.25f * m_shake);
        m_shakeT += dt;
        ct->SetPosition(np);
        const Vec3 look = lookAt - np;
        if (look.x * look.x + look.y * look.y + look.z * look.z > 1e-6f) {
            const Quat want = Quat::LookRotation(look * -1.0f);
            ct->SetRotation(m_view >= 2 ? want : Quat::Slerp(ct->Rotation(), want, camLerp > 0.0f ? 1.0f - std::exp(-camLerp * 1.5f * dt) : 1.0f));
        }
        const float fov = camFov + camSpeedFov * sp;
        cam->fieldOfView += (fov - cam->fieldOfView) * (1.0f - std::exp(-5.0f * dt));
    }
private:

    void Hold() { if (!m_scripted || m_holdT >= 0.0f) m_holdT = 0.5f; m_scripted = true; }
    DriveInput m_in; float m_holdT = -1.0f; float m_steerMul = 1.0f; bool m_invert = false, m_assistsOff = false; DrivingSettings m_drv; bool m_scripted = false, m_unmanned = false, m_haveCmd = false; float m_cmdX = 0, m_cmdZ = 0;
    bool  m_init = false, m_horn = false, m_lights = false, m_lookBack = false, m_reversing = false, m_drifting = false, m_flipped = false;
    float m_vx = 0, m_vz = 0, m_rpm = 900, m_shiftT = 0, m_boostLeft = 1, m_wheelAngle = 0, m_slip = 0, m_yawRate = 0, m_aLong = 0;
    float m_thrS = 0, m_brkS = 0, m_steerS = 0, m_flipT = 0, m_airPitch = 0, m_airRoll = 0, m_shake = 0, m_shakeT = 0;
    float m_surfaceOverride = -1.0f, m_surfaceCache = 1.0f, m_surfaceCheckT = 0.0f;
    int   m_gear = 1, m_view = 0;
    std::string m_presetName;

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
    void UpdateGrounded() {
        m_grounded = true;
        if (gameObject && gameObject->scene() && transform) {
            RaycastHit3D h = gameObject->scene()->physics3D().Raycast(
                *gameObject->scene(), transform->Position(), Vec3{0, -1, 0},
                groundCheckDistance, gameObject);
            m_grounded = h.hit;
        }
    }
    // Per-wheel raycast suspension: cast down at the four wheel corners, run a
    // critically-damped vertical spring toward rideHeight (via AddForce so gravity
    // composes through the integrator), and tilt the chassis from per-wheel
    // compression plus dynamic accel/cornering lean.
    void ApplySuspension(float dt, Rigidbody3D* rb, const Vec3& fp, const Vec3& rp,
                         const Vec3& planar, float speedF, float lat) {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        Vec3 pos = transform->Position();
        float hb = wheelBase * 0.5f, ht = trackWidth * 0.5f;
        // Corners: 0 front-right, 1 front-left, 2 rear-right, 3 rear-left.
        Vec3 corner[4] = { pos + fp * hb + rp * ht, pos + fp * hb - rp * ht,
                           pos - fp * hb + rp * ht, pos - fp * hb - rp * ht };
        // Start each ray a little above the chassis and reach rideHeight+travel below
        // it, so the wheel finds ground whether the spring is compressed or drooping.
        float topMargin = 0.3f;
        float maxRay = topMargin + rideHeight + suspensionTravel;
        float groundY[4]; int nHit = 0; float sumGround = 0.0f;
        for (int i = 0; i < 4; ++i) {
            RaycastHit3D h{};
            if (sc) h = sc->physics3D().Raycast(*sc, corner[i] + Vec3{0, topMargin, 0},
                                                Vec3{0, -1, 0}, maxRay, gameObject);
            if (h.hit) { groundY[i] = h.point.y; sumGround += h.point.y; ++nHit; }
            else       { groundY[i] = corner[i].y - suspensionTravel; }
        }
        m_grounded = nHit > 0;

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

        // ---- Commit velocity ----
        if (rb) { rb->velocity.x = planar.x; rb->velocity.z = planar.z; }
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
