#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/SpriteAnimator.hpp"   // directional sprite-sheet rows
#include "okay/Components/UIAnchor.hpp"     // UICanvas::Width/Height for mouse->world
#include "okay/Scene/SceneSerializer.hpp"   // spawn projectiles
#include <string>
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// A full-featured 2D TOP-DOWN player controller (Zelda / twin-stick / ARPG feel) for
/// a sprite on the XY plane — the no-code replacement for a "move by WASD" script.
/// Moves with WASD / arrows / left-stick, with optional 8-direction normalization,
/// sprint, momentum (accel/decel), a dash, and facing that turns the sprite toward
/// travel or the mouse cursor (twin-stick aim). Drives a sibling Rigidbody2D's
/// velocity when present (so it collides), otherwise moves the Transform. Optional
/// world bounds clamp or screen-wrap, and it drives a Character's idle/walk/run anim.
class TopDownController2D : public Behaviour {
public:
    // ---- Movement ----
    float speed      = 5.0f;      // walk speed
    float runSpeed   = 8.0f;      // speed while the sprint key is held
    char  sprintKey  = 0;         // hold to run (0 = disabled)
    bool  normalizeDiagonal = true;   // W+D isn't faster than W alone
    bool  useGamepad = true;      // left stick overrides the keys when pushed
    // Momentum: units/s^2 toward the target speed (0 = instant, arcade-snappy).
    float acceleration = 0.0f;
    float deceleration = 0.0f;

    // ---- Dash ----
    char  dashKey       = 0;      // tap to dash (0 = disabled)
    float dashSpeed     = 16.0f;  // speed during a dash
    float dashDuration  = 0.15f;  // seconds
    float dashCooldown  = 0.6f;   // seconds between dashes

    // ---- Facing ----
    enum class FaceMode { None, Move, Mouse };   // turn toward travel, the cursor, or don't
    int   faceMode = (int)FaceMode::Move;
    // Which way the sprite art points at 0 rotation, so it lines up: Up/Right/Down/Left.
    enum class Forward { Up, Right, Down, Left };
    int   spriteForward = (int)Forward::Up;
    float turnSpeed = 0.0f;       // deg/s of turning (0 = snap instantly)

    // ---- Animation ----
    bool  driveAnimation = true;  // set a Character walk/run/idle from movement
    int   facing = 0;             // 0=Down 1=Up 2=Left 3=Right — read this for directional sprites

    // ---- World limits ----
    bool  clampBounds = false;    // keep the player inside [boundsMin, boundsMax]
    Vec2  boundsMin{-100.0f, -100.0f};
    Vec2  boundsMax{ 100.0f,  100.0f};
    bool  screenWrap = false;     // wrap around the camera view edges instead

    // ---- Combat ----
    char  fireKey = 0;            // hold to shoot (0 = disabled)
    int   fireButton = -1;        // mouse button to shoot (-1 = off; 0 = left)
    std::string projectile;       // prefab spawned as a bullet (give it a Rigidbody2D + Lifetime)
    float projectileSpeed = 12.0f;
    float fireRate = 5.0f;        // shots per second
    float knockbackTime = 0.22f;  // control lockout after a Knockback()

    /// Shove the player: bullets, explosions, and enemy hits call this. Locks input
    /// briefly while the impulse decays. Pass any direction (auto-normalized).
    void Knockback(Vec2 dir, float force) {
        float l = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        Vec2 d = l > 1e-4f ? Vec2{dir.x / l, dir.y / l} : Vec2{-m_lastDir.x, -m_lastDir.y};
        m_knockVel = {d.x * force, d.y * force};
        m_knockTimer = knockbackTime;
    }

    void Update(float dt) override {
        if (!transform) return;
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it

        // ---- Knockback: ride out the impulse, ignoring input ----
        if (m_knockTimer > 0.0f) {
            m_knockTimer -= dt;
            float k = knockbackTime > 0.0f ? Mathf::Max(0.0f, m_knockTimer / knockbackTime) : 0.0f;
            Vec2 v{m_knockVel.x * k, m_knockVel.y * k};
            if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr) rb->velocity = v;
            else transform->Translate({v.x * dt, v.y * dt, 0.0f});
            return;
        }

        // ---- Input ----
        Vec2 axis = Input::AxisWASD();
        if (useGamepad) { Vec2 pad = Input::GamepadAxis();
            if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) axis = pad; }
        if (normalizeDiagonal) { float l = std::sqrt(axis.x * axis.x + axis.y * axis.y);
            if (l > 1.0f) { axis.x /= l; axis.y /= l; } }
        bool moving = (Mathf::Abs(axis.x) + Mathf::Abs(axis.y)) > 0.001f;
        if (moving) m_lastDir = { axis.x, axis.y };
        bool running = sprintKey && Input::GetKey(sprintKey) && moving;

        // ---- Dash ----
        if (m_dashCd > 0.0f) m_dashCd -= dt;
        if (dashKey && Input::GetKeyDown(dashKey) && m_dashTimer <= 0.0f && m_dashCd <= 0.0f
            && (m_lastDir.x != 0.0f || m_lastDir.y != 0.0f)) {
            m_dashTimer = dashDuration; m_dashCd = dashCooldown;
            m_dashDir = m_lastDir;
        }
        bool dashing = m_dashTimer > 0.0f;
        if (dashing) m_dashTimer -= dt;

        // ---- Target velocity ----
        Vec2 dir = dashing ? m_dashDir : axis;
        float spd = dashing ? dashSpeed : (running ? runSpeed : speed);
        Vec2 target{ dir.x * spd, dir.y * spd };

        if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr) {
            if (dashing || acceleration <= 0.0f) {          // instant during a dash / arcade mode
                rb->velocity = target;
            } else {
                float rate = (moving ? acceleration : deceleration) * dt;
                rb->velocity.x = Mathf::MoveTowards(rb->velocity.x, target.x, rate);
                rb->velocity.y = Mathf::MoveTowards(rb->velocity.y, target.y, rate);
            }
        } else {
            transform->Translate({ target.x * dt, target.y * dt, 0.0f });
        }

        // ---- Facing (rotate the sprite + expose a 4-way direction) ----
        Vec2 look{0, 0};
        if ((FaceMode)faceMode == FaceMode::Move) look = dashing ? m_dashDir : (moving ? Vec2{axis.x, axis.y} : Vec2{0, 0});
        else if ((FaceMode)faceMode == FaceMode::Mouse) look = MouseDir();
        if (look.x != 0.0f || look.y != 0.0f) {
            facing = (Mathf::Abs(look.x) > Mathf::Abs(look.y)) ? (look.x < 0 ? 2 : 3) : (look.y < 0 ? 0 : 1);
            if ((FaceMode)faceMode != FaceMode::None) {
                float want = std::atan2(look.y, look.x) * Mathf::Rad2Deg + ForwardOffset();
                float cur = transform->localRotation.ToEuler().z;
                float z = (turnSpeed > 0.0f) ? Mathf::MoveTowardsAngle(cur, want, turnSpeed * dt) : want;
                transform->localRotation = Quat::Euler(0, 0, z);
            }
        }

        // ---- Animation ----
        if (driveAnimation) if (Character* ch = FindCharacter())
            ch->anim = !moving ? 1 : (running ? 3 : 2);

        // ---- World limits ----
        if (clampBounds) {
            Vec3 p = transform->localPosition;
            p.x = Mathf::Clamp(p.x, boundsMin.x, boundsMax.x);
            p.y = Mathf::Clamp(p.y, boundsMin.y, boundsMax.y);
            transform->localPosition = p;
        } else if (screenWrap) {
            WrapToView();
        }

        // ---- Directional sprite-sheet row from the 4-way facing ----
        if (driveAnimation) if (auto* sa = gameObject ? gameObject->GetComponent<SpriteAnimator>() : nullptr)
            if (sa->atlasColumns > 0 && sa->atlasRows > 1) sa->rowOverride = facing;

        // ---- Shooting ----
        if (m_fireCd > 0.0f) m_fireCd -= dt;
        bool wantFire = (fireKey && Input::GetKey(fireKey)) || (fireButton >= 0 && Input::GetMouseButton(fireButton));
        if (wantFire && m_fireCd <= 0.0f && !projectile.empty()) {
            Vec2 aim = ((FaceMode)faceMode == FaceMode::Mouse) ? MouseDir()
                     : (moving ? Vec2{axis.x, axis.y} : m_lastDir);
            Fire(aim);
            m_fireCd = fireRate > 0.0f ? 1.0f / fireRate : 0.2f;
        }
    }

private:
    Vec2 m_lastDir{0, -1}, m_dashDir{0, 0}, m_knockVel{0, 0};
    float m_dashTimer = 0.0f, m_dashCd = 0.0f, m_fireCd = 0.0f, m_knockTimer = 0.0f;

    void Fire(Vec2 aim) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s || projectile.empty()) return;
        GameObject* b = SceneSerializer::InstantiateFromFile(*s, projectile, nullptr);
        if (!b || !b->transform) return;
        b->transform->localPosition = transform->Position();
        float l = std::sqrt(aim.x * aim.x + aim.y * aim.y);
        Vec2 d = l > 1e-4f ? Vec2{aim.x / l, aim.y / l} : m_lastDir;
        if (auto* rb = b->GetComponent<Rigidbody2D>()) rb->velocity = {d.x * projectileSpeed, d.y * projectileSpeed};
    }

    float ForwardOffset() const {
        switch ((Forward)spriteForward) {
            case Forward::Right: return 0.0f;
            case Forward::Up:    return -90.0f;
            case Forward::Left:  return 180.0f;
            case Forward::Down:  return 90.0f;
        }
        return -90.0f;
    }

    // Cursor position in world XY, using the main (orthographic) camera — for twin-stick aim.
    Vec2 MouseDir() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        Camera* cam = s ? s->mainCamera : nullptr;
        if (!cam || !cam->transform) return {0, 0};
        float ortho = cam->orthographicSize > 1e-3f ? cam->orthographicSize : 5.0f;
        float w = UICanvas::Width(), h = UICanvas::Height();
        float scale = h / (2.0f * ortho); if (scale < 1e-6f) scale = 1.0f;
        Vec3 cp = cam->transform->Position();
        Vec2 mp = Input::MousePosition();
        Vec2 world{ cp.x + (mp.x - w * 0.5f) / scale, cp.y + (h * 0.5f - mp.y) / scale };
        Vec3 me = transform->Position();
        Vec2 d{ world.x - me.x, world.y - me.y };
        float l = std::sqrt(d.x * d.x + d.y * d.y);
        return l > 1e-4f ? Vec2{ d.x / l, d.y / l } : Vec2{0, 0};
    }

    void WrapToView() {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        Camera* cam = s ? s->mainCamera : nullptr;
        if (!cam || !cam->transform) return;
        float ortho = cam->orthographicSize > 1e-3f ? cam->orthographicSize : 5.0f;
        float aspect = UICanvas::Width() / Mathf::Max(1.0f, UICanvas::Height());
        Vec3 cc = cam->transform->Position();
        float halfY = ortho, halfX = ortho * aspect;
        Vec3 p = transform->localPosition;
        if (p.x < cc.x - halfX) p.x = cc.x + halfX; else if (p.x > cc.x + halfX) p.x = cc.x - halfX;
        if (p.y < cc.y - halfY) p.y = cc.y + halfY; else if (p.y > cc.y + halfY) p.y = cc.y - halfY;
        transform->localPosition = p;
    }

    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform) for (Transform* c : transform->Children())
            if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay
