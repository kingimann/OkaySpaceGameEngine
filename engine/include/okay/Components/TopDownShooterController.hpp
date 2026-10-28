#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"   // spawn projectile prefabs
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/PlayerCollision.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Projectile3D.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/DeathLock.hpp"
#include "okay/Components/UIAnchor.hpp"     // UICanvas::Width/Height (viewport)
#include "okay/Input/Input.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <string>

namespace okay {

/// 3D TOP-DOWN SHOOTER controller (twin-stick: Enter the Gungeon, Nuclear Throne,
/// Helldivers). WASD / left stick moves; the body turns to face the mouse cursor
/// (or the stick direction on a gamepad) so you can run one way and shoot another.
/// Left mouse / RB fires: built-in bullets (no setup), a projectile prefab, or an
/// instant hitscan ray. Magazine + reload, spread / shotgun pellets, a dodge dash
/// with i-frames, and a high follow camera that leans toward where you aim and
/// kicks when you fire.
///
/// Script events on the player's script: on_fire, on_reload, on_reloaded, on_empty,
/// on_dash. Projectiles call on_hit on their own script.
class TopDownShooterController : public Behaviour {
public:
    /// Runs before the animation components (Character, ModelAnimator,
    /// AnimStateMachine at 0, IK at 100), so they pose this frame's movement and
    /// state changes, not last frame's.
    int ExecutionOrder() const override { return -10; }
    enum class AimMode { Mouse, MoveDirection, Auto };   ///< Auto: mouse, or the stick when a pad is used
    enum class FireMode { Bullets, Prefab, Hitscan };

    // ---- Movement ----
    float walkSpeed = 6.0f;
    float runSpeed = 8.5f;
    char  sprintKey = 0;                 ///< hold to run (0 = off: shooters usually don't)
    float acceleration = 70.0f, deceleration = 60.0f;
    float backpedalMultiplier = 0.8f;    ///< speed when moving away from the aim
    bool  cameraRelative = false;        ///< WASD follows the camera yaw instead of world axes
    bool  driveAnimation = true;

    // ---- Aim ----
    int   aimMode = (int)AimMode::Auto;
    float turnSpeed = 25.0f;             ///< how fast the body turns to the aim (0 = snap)

    // ---- Weapon ----
    int   fireMode = (int)FireMode::Bullets;
    int   fireButton = 0;                ///< mouse button (0 = left)
    char  fireKey = 0;                   ///< optional keyboard fire
    int   fireGamepadButton = 7;         ///< RB
    bool  autoFire = true;               ///< hold to keep firing
    float fireRate = 8.0f;               ///< shots per second
    float damage = 10.0f;
    float projectileSpeed = 24.0f;
    float projectileLifetime = 1.5f;
    float range = 30.0f;                 ///< hitscan reach
    int   pellets = 1;                   ///< bullets per shot (shotgun > 1)
    float spread = 3.0f;                 ///< random cone (degrees)
    float impactForce = 4.0f;
    float bulletSize = 0.12f;
    Color bulletColor = Color::FromBytes(255, 214, 90);
    std::string projectile;              ///< prefab for FireMode::Prefab
    float muzzleHeight = 1.0f;           ///< bullets start this high above the player origin
    float muzzleForward = 0.6f;          ///< ... and this far in front

    // ---- Ammo ----
    int   magazineSize = 0;              ///< 0 = never reload
    int   ammo = 0;                      ///< rounds left in the magazine (filled on Start)
    char  reloadKey = 'r';
    float reloadTime = 1.2f;
    bool  autoReload = true;             ///< reload when you fire an empty magazine

    // ---- Dash (dodge) ----
    // A short burst: it starts at dashSpeed and eases down to your movement speed
    // over dashTime, then hands straight back to normal movement (no skid). The
    // body ducks and faces the dash direction while it lasts.
    char  dashKey = ' ';                 ///< Space
    int   dashGamepadButton = 0;         ///< pad A (-1 = none)
    float dashSpeed = 16.0f;             ///< peak speed at the start of the dash
    float dashTime = 0.25f;
    float dashCooldown = 0.6f;           ///< after a dash ends
    bool  dashInvulnerable = true;       ///< no damage while dashing (sibling HealthStat)

    // ---- Camera ----
    float cameraDistance = 14.0f;
    float cameraPitch = 62.0f;           ///< downward tilt (deg)
    float cameraYaw = 0.0f;
    float cameraDamping = 10.0f;
    float aimLookAhead = 0.3f;           ///< share of the way to the cursor the camera leans
    float maxLookAhead = 4.0f;
    bool  wheelZoom = true;
    float minDistance = 7.0f, maxDistance = 24.0f;
    float fireShake = 0.06f;             ///< camera kick per shot

    // ---- Runtime (read by scripts / HUD) ----
    Vec3  AimPoint() const { return m_aimPoint; }
    Vec3  AimDirection() const { return m_aimDir; }
    bool  IsReloading() const { return m_reload > 0.0f; }
    float ReloadProgress() const { return reloadTime > 0.0f && m_reload > 0.0f ? 1.0f - m_reload / reloadTime : 1.0f; }
    bool  IsDashing() const { return m_dash > 0.0f; }
    int   ShotsFired() const { return m_shots; }

    void Start() override {
        if (magazineSize > 0 && ammo <= 0) ammo = magazineSize;
        if (Character* ch = FindCharacter()) m_baseAnimSpeed = ch->animSpeed;
        Cursor::Capture(false);   // twin-stick: the cursor is the crosshair
        m_aimDir = Vec3{0, 0, -1};
    }

    /// Fire one shot now (ignores the fire rate; still uses ammo).
    void Fire() {
        if (!gameObject || !transform) return;
        if (magazineSize > 0) {
            if (m_reload > 0.0f) return;
            if (ammo <= 0) { CallScript("on_empty"); if (autoReload) Reload(); return; }
            --ammo;
        }
        ++m_shots;
        Scene* sc = gameObject->scene();
        Vec3 muzzle = transform->Position() + Vec3{0, muzzleHeight, 0} + m_aimDir * muzzleForward;
        const int n = Mathf::Max(1, pellets);
        for (int i = 0; i < n; ++i) {
            // Pellets fan evenly across the spread; a single bullet gets a random wobble.
            float t = n > 1 ? ((float)i / (float)(n - 1) - 0.5f) : (Rand01() - 0.5f);
            float ang = t * spread * Mathf::Deg2Rad;
            float c = std::cos(ang), s = std::sin(ang);
            Vec3 d{m_aimDir.x * c - m_aimDir.z * s, 0.0f, m_aimDir.x * s + m_aimDir.z * c};
            if (sc) SpawnShot(*sc, muzzle, d);
        }
        m_shake = Mathf::Min(0.5f, m_shake + fireShake);
        CallScript("on_fire");
        if (magazineSize > 0 && ammo == 0 && autoReload) Reload();
    }

    void Reload() {
        if (magazineSize <= 0 || m_reload > 0.0f || ammo >= magazineSize) return;
        m_reload = Mathf::Max(0.001f, reloadTime);
        CallScript("on_reload");
    }

    void Update(float dt) override {
        if (!transform || !gameObject) return;
        if (Game::Paused()) return;
        if (!IsLocallyControlled(gameObject)) return;
        if (DeathLocksInput(gameObject)) { StopDeadSlide(gameObject); return; }
        const bool uiBlocked = Input::UICaptured();

        // ---- Move input ----
        Vec2 axis = uiBlocked ? Vec2{0, 0} : Input::AxisWASD();
        Vec2 pad = uiBlocked ? Vec2{0, 0} : Input::GamepadAxis();
        if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) { axis = pad; m_padActive = true; }
        Vec2 mp = Input::MousePosition();
        if (Mathf::Abs(mp.x - m_lastMouse.x) + Mathf::Abs(mp.y - m_lastMouse.y) > 2.0f) m_padActive = false;
        m_lastMouse = mp;

        Quat basis = cameraRelative ? Quat::Euler(0, cameraYaw, 0) : Quat::Identity;
        Vec3 fwd = basis * Vec3{0, 0, -1}, right = basis * Vec3::Right;
        Vec3 dir = fwd * axis.y + right * axis.x;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        bool moving = len > 0.01f;
        if (moving) { dir.x /= len; dir.z /= len; dir.y = 0.0f; }

        // ---- Aim ----
        const bool useMouse = aimMode == (int)AimMode::Mouse || (aimMode == (int)AimMode::Auto && !m_padActive);
        Vec3 pos = transform->Position();
        bool aimed = false;
        if (useMouse) {
            Vec3 hit;
            if (Scene* sc = gameObject->scene(); sc && CursorOnPlane(*sc, pos.y + muzzleHeight, hit)) {
                Vec3 d{hit.x - pos.x, 0.0f, hit.z - pos.z};
                float dl = std::sqrt(d.x * d.x + d.z * d.z);
                m_aimPoint = hit;
                if (dl > 0.05f) { m_aimDir = d * (1.0f / dl); aimed = true; }
            }
        } else {
            if (moving) { m_aimDir = dir; aimed = true; }
            m_aimPoint = pos + m_aimDir * 5.0f;
        }
        m_haveAim = m_haveAim || aimed;
        // ---- Dash ----
        bool running = sprintKey && Input::GetKey(sprintKey) && moving;
        float speed = running ? runSpeed : walkSpeed;
        const bool backpedal = moving && Vec3::Dot(dir, m_aimDir) < -0.3f;
        if (backpedal) speed *= backpedalMultiplier;
        m_dashCd = Mathf::Max(0.0f, m_dashCd - dt);
        bool dashEnded = false;
        if (m_dash > 0.0f) {
            m_dash -= dt;
            if (m_dash <= 0.0f) { EndDash(); dashEnded = true; }
        } else if (!uiBlocked && m_dashCd <= 0.0f && dashTime > 0.0f &&
                   ((dashKey && Input::GetKeyDown(dashKey)) ||
                    (dashGamepadButton >= 0 && Input::GetGamepadButtonDown(dashGamepadButton)))) {
            m_dashDir = moving ? dir : m_aimDir;
            m_dash = dashTime; m_dashCd = dashTime + dashCooldown;   // the cooldown starts when the dash ends
            if (dashInvulnerable)
                if (auto* hp = gameObject->GetComponent<HealthStat>()) { m_hpGod = hp->godMode; hp->godMode = true; m_godSet = true; }
            CallScript("on_dash");
        }
        const bool dashing = m_dash > 0.0f;

        // ---- Facing: the aim, or the dash direction while dashing ----
        {
            Vec3 face = dashing ? m_dashDir : m_aimDir;
            Quat want = Quat::LookRotation({-face.x, 0.0f, -face.z});   // mesh faces -Z
            float rate = dashing ? Mathf::Max(turnSpeed, 30.0f) : turnSpeed;
            float t = rate > 0.0f ? 1.0f - std::exp(-rate * dt) : 1.0f;
            transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
        }

        // ---- Velocity ----
        // Dash speed eases from dashSpeed down to the speed you'd be moving at, so
        // the burst blends into normal movement instead of ending in a skid.
        const float exitSpeed = Mathf::Min(dashSpeed, moving ? speed : walkSpeed * 0.5f);
        float dashV = 0.0f;
        if (dashing) {
            float p = 1.0f - Mathf::Clamp(m_dash / dashTime, 0.0f, 1.0f);   // 0 at the start .. 1 at the end
            dashV = exitSpeed + (dashSpeed - exitSpeed) * (1.0f - p) * (1.0f - p);
        }
        Vec3 want = dashing ? m_dashDir * dashV : (moving ? dir * speed : Vec3{0, 0, 0});
        if (auto* rb = gameObject->GetComponent<Rigidbody3D>()) {
            if (dashing) { rb->velocity.x = want.x; rb->velocity.z = want.z; }
            else {
                if (dashEnded) {   // leave the dash at (at most) the exit speed
                    Vec3 v{rb->velocity.x, 0.0f, rb->velocity.z};
                    float vl = std::sqrt(v.x * v.x + v.z * v.z);
                    if (vl > exitSpeed && vl > 1e-4f) { rb->velocity.x *= exitSpeed / vl; rb->velocity.z *= exitSpeed / vl; }
                }
                Vec3 cur{rb->velocity.x, 0.0f, rb->velocity.z};
                Vec3 dv{want.x - cur.x, 0.0f, want.z - cur.z};
                float rate = moving ? acceleration : deceleration;
                float dl = std::sqrt(dv.x * dv.x + dv.z * dv.z), step = rate * dt;
                if (dl > 1e-5f && dl > step) { dv.x = dv.x / dl * step; dv.z = dv.z / dl * step; }
                rb->velocity.x = cur.x + dv.x; rb->velocity.z = cur.z + dv.z;
            }
            rb->WakeUp();
        } else if (want.x != 0.0f || want.z != 0.0f) {
            transform->Translate(want * dt);
            if (Scene* sc = gameObject->scene()) ResolvePlayerBody(*sc, gameObject);
        }

        // ---- Reload / fire ----
        if (m_reload > 0.0f) {
            m_reload -= dt;
            if (m_reload <= 0.0f) { m_reload = 0.0f; ammo = magazineSize; CallScript("on_reloaded"); }
        }
        if (!uiBlocked && reloadKey && Input::GetKeyDown(reloadKey)) Reload();
        m_fireCd = Mathf::Max(0.0f, m_fireCd - dt);
        bool held = !uiBlocked && ((fireButton >= 0 && Input::GetMouseButton(fireButton)) ||
                                   (fireKey && Input::GetKey(fireKey)) ||
                                   (fireGamepadButton >= 0 && Input::GetGamepadButton(fireGamepadButton)));
        bool pressed = !uiBlocked && ((fireButton >= 0 && Input::GetMouseButtonDown(fireButton)) ||
                                      (fireKey && Input::GetKeyDown(fireKey)) ||
                                      (fireGamepadButton >= 0 && Input::GetGamepadButtonDown(fireGamepadButton)));
        if ((autoFire ? held : pressed) && m_fireCd <= 0.0f && m_dash <= 0.0f) {
            m_fireCd = 1.0f / Mathf::Max(0.1f, fireRate);
            Fire();
        }

        if (moving) m_backpedal = backpedal;   // keep stepping the same way while slowing to a stop
        if (driveAnimation) DriveAnimation(dt, running, m_backpedal);
    }

    void LateUpdate(float dt) override {
        if (!transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc || !sc->mainCamera || !sc->mainCamera->transform) return;
        if (!IsLocallyControlled(gameObject)) return;
        Transform* cam = sc->mainCamera->transform;

        if (wheelZoom && !Input::UICaptured()) {
            float w = Input::MouseWheel();
            if (w != 0.0f) cameraDistance = Mathf::Clamp(cameraDistance - w * 1.2f, minDistance, maxDistance);
        }
        Vec3 target = transform->Position();
        // Lean toward the cursor so you see further where you're shooting.
        if (m_haveAim) {
            Vec3 lean{(m_aimPoint.x - target.x) * aimLookAhead, 0.0f, (m_aimPoint.z - target.z) * aimLookAhead};
            float ll = std::sqrt(lean.x * lean.x + lean.z * lean.z);
            if (ll > maxLookAhead && ll > 1e-4f) lean = lean * (maxLookAhead / ll);
            target = target + lean;
        }

        float pr = cameraPitch * Mathf::Deg2Rad;
        Vec3 behind = Quat::Euler(0, cameraYaw, 0) * Vec3{0, 0, 1};
        Vec3 desired = target + Vec3{behind.x * std::cos(pr), std::sin(pr), behind.z * std::cos(pr)} * cameraDistance;
        if (cameraDamping > 0.0f && m_haveCam) {
            float t = 1.0f - std::exp(-cameraDamping * dt);
            m_camPos = m_camPos + (desired - m_camPos) * t;
        } else m_camPos = desired;
        m_haveCam = true;

        m_time += dt;
        m_shake = Mathf::Max(0.0f, m_shake - dt * 2.5f);
        Vec3 kick{std::sin(m_time * 61.0f) * m_shake, 0.0f, std::cos(m_time * 47.0f) * m_shake};
        cam->SetPosition(m_camPos + kick);
        cam->localRotation = Quat::Euler(-cameraPitch, cameraYaw, 0.0f);
    }

    void OnDestroy() override { if (m_dash > 0.0f) EndDash(); }

private:
    Vec3  m_aimDir{0, 0, -1}, m_aimPoint{0, 0, 0}, m_dashDir{0, 0, 0}, m_camPos{0, 0, 0};
    Vec2  m_lastMouse{0, 0};
    bool  m_padActive = false, m_haveCam = false, m_haveAim = false, m_godSet = false, m_hpGod = false;
    float m_fireCd = 0.0f, m_reload = 0.0f, m_dash = 0.0f, m_dashCd = 0.0f, m_shake = 0.0f, m_time = 0.0f;
    float m_baseAnimSpeed = 1.0f, m_moveSpeed = 0.0f;
    Vec3  m_lastPos{0, 0, 0};
    bool  m_havePos = false, m_backpedal = false;
    int   m_shots = 0;
    unsigned m_rng = 0x9E3779B9u;

    float Rand01() { m_rng = m_rng * 1664525u + 1013904223u; return (float)((m_rng >> 8) & 0xFFFF) / 65535.0f; }

    // Animation from what the body is ACTUALLY doing (its measured ground velocity),
    // not from the keys: no idle pose while still sliding, no legs running in
    // place against a wall. The Character lays its stride out from the reported
    // speed and direction, so the feet stay planted, strafing side-steps and
    // backpedalling walks backwards while the body keeps facing the aim.
    void DriveAnimation(float dt, bool running, bool backpedal) {
        (void)backpedal;
        Character* ch = FindCharacter();
        if (!ch) return;
        Vec3 pos = transform->Position();
        Vec3 vel{0.0f, 0.0f, 0.0f};
        if (auto* rb = gameObject->GetComponent<Rigidbody3D>()) vel = {rb->velocity.x, 0.0f, rb->velocity.z};
        else if (m_havePos && dt > 1e-5f) vel = {(pos.x - m_lastPos.x) / dt, 0.0f, (pos.z - m_lastPos.z) / dt};
        m_lastPos = pos; m_havePos = true;
        float v = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        // Smooth out physics jitter; settle faster when slowing so the body goes
        // idle the moment it actually stops.
        float k = 1.0f - std::exp((v < m_moveSpeed ? -30.0f : -12.0f) * dt);
        m_moveSpeed += (v - m_moveSpeed) * k;
        ch->animSpeed = m_baseAnimSpeed;
        ch->moveSpeed = m_moveSpeed;
        if (v > 0.05f) {
            Vec3 f = transform->localRotation * Vec3{0, 0, -1};
            float fl = std::sqrt(f.x * f.x + f.z * f.z);
            if (fl > 1e-4f) {
                f = f * (1.0f / fl);
                ch->moveAngle = std::atan2(vel.x * -f.z + vel.z * f.x, vel.x * f.x + vel.z * f.z) * Mathf::Rad2Deg;
            }
        }
        if (m_dash > 0.0f) { ch->anim = 6; return; }   // duck through the dodge
        if (m_moveSpeed < 0.35f) { ch->anim = 1; return; }
        const bool run = running || m_moveSpeed > walkSpeed * 1.15f;
        ch->anim = run ? 3 : 2;
    }

    void EndDash() {
        m_dash = 0.0f;
        if (m_godSet && gameObject)
            if (auto* hp = gameObject->GetComponent<HealthStat>()) hp->godMode = m_hpGod;
        m_godSet = false;
    }

    void CallScript(const char* ev) {
        if (gameObject)
            if (auto* s = gameObject->GetComponent<ScriptComponent>())
                if (s->VM()) s->VM()->CallEvent(ev);
    }

    void SpawnShot(Scene& sc, const Vec3& muzzle, const Vec3& d) {
        if (fireMode == (int)FireMode::Hitscan) {
            RaycastHit3D hit = sc.physics3D().Raycast(sc, muzzle, d, range, gameObject);
            if (hit.hit) {
                DamageGameObject(hit.gameObject, damage);
                if (hit.gameObject)
                    if (auto* rb = hit.gameObject->GetComponent<Rigidbody3D>()) rb->AddImpulse(d * impactForce);
            }
            return;
        }
        GameObject* b = nullptr;
        if (fireMode == (int)FireMode::Prefab && !projectile.empty())
            b = SceneSerializer::InstantiateFromFile(sc, projectile, nullptr);
        if (!b) {
            b = sc.CreateGameObject("Bullet");
            auto* mr = b->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Sphere(Mathf::Max(0.02f, bulletSize), 6, 8);
            mr->color = bulletColor;
            mr->shader = MeshRenderer::Shader::Unlit;
        }
        if (!b->transform) return;
        b->transform->SetPosition(muzzle);
        b->transform->localRotation = Quat::LookRotation({-d.x, -d.y, -d.z});
        if (auto* pr = b->GetComponent<Projectile3D>()) {
            pr->velocity = d * projectileSpeed; pr->owner = gameObject;
        } else if (auto* rb = b->GetComponent<Rigidbody3D>()) {
            rb->velocity = d * projectileSpeed;
        } else {
            auto* p = b->AddComponent<Projectile3D>();
            p->velocity = d * projectileSpeed; p->owner = gameObject;
            p->damage = damage; p->lifetime = projectileLifetime; p->impactForce = impactForce;
        }
    }

    // Unproject the cursor through the main camera onto the horizontal plane y = planeY.
    bool CursorOnPlane(Scene& scene, float planeY, Vec3& out) const {
        Camera* cam = scene.mainCamera;
        if (!cam || !cam->gameObject || !cam->gameObject->transform) return false;
        float w = UICanvas::Width(), h = UICanvas::Height();
        if (w < 1.0f || h < 1.0f) return false;
        Vec2 mp = Input::MousePosition();
        Vec3 ro, rd;
        cam->ScreenPointToRay(mp.x, mp.y, w, h, ro, rd);
        if (Mathf::Abs(rd.y) < 1e-5f) return false;
        float t = (planeY - ro.y) / rd.y;
        if (t <= 0.0f) return false;
        out = {ro.x + rd.x * t, planeY, ro.z + rd.z * t};
        return true;
    }

    Character* FindCharacter() const {
        if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        for (Transform* c : transform->Children())
            if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay
