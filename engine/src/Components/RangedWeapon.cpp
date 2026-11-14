#include "okay/Components/RangedWeapon.hpp"
#include "okay/Combat/WeaponDefinition.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/Projectile3D.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

RangedWeapon::Tune RangedWeapon::Current() const {
    Tune t;
    t.damage = damage;
    t.fireRate = fireRate;
    t.range = range;
    t.reload = reloadTime;
    t.knockback = knockback;
    t.spread = spread;
    t.projectileSpeed = projectileSpeed;
    t.magazine = magazine;
    t.reserve = reserve;
    t.hitscan = hitscan;
    t.projectile = projectile;
    const WeaponDefinition* w = FindWeapon(weapon, weaponFile);
    if (!w && weapon.empty() && weaponFile.empty()) w = WeaponForHolder(gameObject);
    if (w && w->kind == WeaponKind::Gun) {
        t.damage = w->damage;
        t.fireRate = w->fireRate;
        t.range = w->range;
        t.reload = w->reload;
        t.knockback = w->knockback;
        t.spread = w->spread;
        t.projectileSpeed = w->projectileSpeed;
        t.magazine = w->magazine;
        t.reserve = w->ammo;
        t.hitscan = w->hitscan;
        if (!w->projectile.empty()) t.projectile = w->projectile;
    }
    return t;
}

void RangedWeapon::Arm() {
    if (m_ready) return;
    m_ready = true;
    const Tune t = Current();
    m_mag = std::max(0, t.magazine);
    m_infinite = t.reserve < 0;
    m_reserve = m_infinite ? 0 : std::max(0, t.reserve);
}

int RangedWeapon::Rounds() const {
    if (m_ready) return m_mag;
    const Tune t = Current();
    return std::max(0, t.magazine);
}

int RangedWeapon::Spare() const {
    if (!m_ready) return Current().reserve;
    return m_infinite ? -1 : m_reserve;
}

void RangedWeapon::Start() { Arm(); }

void RangedWeapon::FinishReload() {
    const Tune t = Current();
    m_reloading = false;
    m_reloadLeft = 0.0f;
    if (t.magazine <= 0) return;
    const int need = t.magazine - m_mag;
    if (need <= 0) return;
    if (m_infinite) { m_mag = t.magazine; return; }
    const int take = std::min(need, m_reserve);
    m_mag += take;
    m_reserve -= take;
}

void RangedWeapon::Reload() {
    Arm();
    if (m_reloading) return;
    const Tune t = Current();
    if (t.magazine <= 0 || m_mag >= t.magazine) return;
    if (!m_infinite && m_reserve <= 0) return;
    if (!(t.reload > 0.0f)) { FinishReload(); return; }
    m_reloading = true;
    m_reloadLeft = t.reload;
    if (auto* sc = gameObject ? gameObject->GetComponent<ScriptComponent>() : nullptr)
        if (sc->VM()) sc->VM()->CallEvent("on_reload");
}

bool RangedWeapon::Shoot(const Tune& t) {
    if (!gameObject || !transform) return false;
    Scene* scene = gameObject->scene();
    if (!scene) return false;
    Vec3 dir = transform->Forward();
    const float len = dir.Magnitude();
    if (len < 1e-5f) dir = Vec3{0.0f, 0.0f, 1.0f};
    else dir = dir * (1.0f / len);
    const Vec3 origin = transform->Position() + Vec3{0.0f, muzzleHeight, 0.0f};
    if (!t.hitscan) {
        GameObject* bullet = nullptr;
        if (!t.projectile.empty()) {
            bullet = SceneSerializer::InstantiatePrefab(*scene, t.projectile, {});
            if (bullet && bullet->transform) bullet->transform->SetPosition(origin);
        }
        if (!bullet) bullet = scene->CreateGameObject("Bullet");
        if (bullet && bullet->transform) {
            if (t.projectile.empty()) bullet->transform->SetPosition(origin);
            Projectile3D* shot = bullet->GetComponent<Projectile3D>();
            if (!shot) shot = bullet->AddComponent<Projectile3D>();
            if (shot) {
                shot->velocity = dir * std::max(0.1f, t.projectileSpeed);
                shot->damage = t.damage;
                shot->impactForce = t.knockback;
                shot->owner = gameObject;
                shot->lifetime = t.range / std::max(0.1f, t.projectileSpeed);
            }
        }
        return bullet != nullptr;
    }
    const RaycastHit3D hit = scene->physics3D().Raycast(*scene, origin, dir, std::max(0.0f, t.range), gameObject);
    if (!hit.hit || !hit.gameObject) return true;   // the shot still left the gun
    ApplyCombatDamage(hit.gameObject, t.damage, origin, dir * t.knockback);
    return true;
}

bool RangedWeapon::Fire() {
    if (!gameObject || !transform) return false;
    Arm();
    if (m_reloading || m_cooldown > 0.0f) return false;
    const Tune t = Current();
    if (!(t.fireRate > 0.0f)) return false;
    if (t.magazine > 0 && m_mag <= 0) {
        if (autoReload) Reload();
        if (auto* sc = gameObject->GetComponent<ScriptComponent>())
            if (sc->VM()) sc->VM()->CallEvent("on_empty");
        return false;
    }
    if (!Shoot(t)) return false;
    m_cooldown = 1.0f / t.fireRate;
    if (t.magazine > 0) --m_mag;
    ++m_shots;
    if (Scene* scene = gameObject->scene())
        for (ActionList* al : scene->FindObjectsOfType<ActionList>()) al->ReceiveMessage("weapon_fire");
    if (auto* sc = gameObject->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEvent("on_fire");
    if (t.magazine > 0 && m_mag <= 0 && autoReload) Reload();
    return true;
}

void RangedWeapon::Update(float dt) {
    if (dt > 0.0f) {
        if (m_cooldown > 0.0f) m_cooldown = std::max(0.0f, m_cooldown - dt);
        if (m_reloading) {
            m_reloadLeft -= dt;
            if (m_reloadLeft <= 0.0f) {
                FinishReload();
                if (auto* sc = gameObject ? gameObject->GetComponent<ScriptComponent>() : nullptr)
                    if (sc->VM()) sc->VM()->CallEvent("on_reloaded");
            }
        }
    }
    if (!IsLocallyControlled(gameObject)) return;
    if (GameInputBlocked(gameObject ? gameObject->scene() : nullptr)) return;
    if (Input::UICaptured()) return;
    if (reloadKey && Input::GetKeyDown(reloadKey)) Reload();
    const bool mouse = fireButton >= 0 && Input::GetMouseButton(fireButton);
    const bool key = fireKey && Input::GetKey(fireKey);
    if (mouse || key) Fire();
}

} // namespace okay
