#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <string>

namespace okay {

/// A gun. It fires on a rate, spends the rounds in the gun, and reloads from the
/// spare rounds. A weapon definition replaces the numbers below. An instant shot
/// hurts the first Health, survival health, RPG stats or NPC the shot reaches,
/// and pushes a dynamic body. A shot that is not instant spawns a projectile.
/// Broadcasts `weapon_fire` on each shot that leaves the gun.
class RangedWeapon : public Behaviour {
public:
    float damage = 10.0f;
    float fireRate = 4.0f;         ///< shots per second
    float range = 30.0f;
    int   magazine = 8;            ///< rounds the gun holds (0 = it never reloads)
    int   reserve = -1;            ///< spare rounds (-1 = they never run out)
    float reloadTime = 1.2f;
    float knockback = 3.0f;
    float spread = 0.0f;
    bool  hitscan = true;
    bool  autoReload = true;       ///< start a reload when the gun is empty
    char  fireKey = 0;             ///< 0 = no key
    int   fireButton = 0;          ///< mouse button, -1 = none
    char  reloadKey = 'r';
    float muzzleHeight = 0.0f;
    float projectileSpeed = 30.0f;
    std::string projectile;        ///< prefab path; empty spawns a plain projectile
    std::string weapon;
    std::string weaponFile;

    bool IsReloading() const { return m_reloading; }
    int ShotsFired() const { return m_shots; }
    /// Rounds left in the gun right now.
    int Rounds() const;
    /// Spare rounds right now (-1 when they never run out).
    int Spare() const;

    void Start() override;
    void Update(float dt) override;

    /// Fire one shot if the rate, the reload and the rounds allow it.
    bool Fire();
    /// Start refilling the gun from the spare rounds.
    void Reload();

private:
    struct Tune {
        float damage, fireRate, range, reload, knockback, spread, projectileSpeed;
        int magazine, reserve;
        bool hitscan;
        std::string projectile;
    };
    Tune Current() const;
    void Arm();
    void FinishReload();
    bool Shoot(const Tune& t);

    float m_cooldown = 0.0f;
    float m_reloadLeft = 0.0f;
    int m_mag = 0;
    int m_reserve = 0;
    int m_shots = 0;
    bool m_ready = false;
    bool m_reloading = false;
    bool m_infinite = true;
};

} // namespace okay
