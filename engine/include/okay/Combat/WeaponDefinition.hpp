#pragma once
// A weapon is data, not a pile of numbers typed onto every fighter.
// One `.okayweapon` file holds the weapons for a project (the same idea as
// items.okayitems). A Melee Attacker, a Ranged Weapon, or the top-down shooter
// names one of them. A weapon can also name the item it belongs to, so equipping
// that item selects the weapon without a second setting.
//
//   okayweapon 1
//   weapon "sword"
//     name "Sword"
//     kind melee
//     item "sword"
//     damage 18
//     swing 0.6
//     range 2.2
//     arc 110
//     hit 0.15 0.35
//     knockback 6
//     combo 3 0.5 0.25
//     hitbox 0 1.2 1.6 1.4
//   end
//   weapon "pistol"
//     name "Pistol"
//     kind gun
//     item "pistol"
//     damage 12
//     fire 5
//     range 40
//     magazine 12
//     ammo 36
//     reload 1.2
//     knockback 3
//     spread 1
//     hitscan 1
//   end
//
// `ammo` is the spare rounds (-1 means the spare pile never runs out). `magazine`
// is how many the gun holds. `hit` is the window, in seconds from the start of
// the swing, during which a melee attack can connect. `combo` is the number of
// hits in a chain, how long the next hit may wait, and the extra damage fraction
// added for each hit after the first.
#include "okay/Math/Vec3.hpp"
#include <deque>
#include <string>
#include <vector>

namespace okay {

class GameObject;

enum class WeaponKind { Melee = 0, Gun = 1 };

struct WeaponDefinition {
    std::string id;
    std::string name;
    WeaponKind kind = WeaponKind::Melee;
    std::string item;                 ///< item id this weapon is tied to (empty = none)

    float damage = 10.0f;
    float range = 2.5f;
    float knockback = 0.0f;

    // Melee.
    float swingTime = 0.45f;          ///< length of one swing, seconds (0 = the hit lands at once)
    float arc = 120.0f;               ///< full cone in front, degrees
    float hitStart = 0.12f;           ///< seconds into the swing when the hit can land
    float hitEnd = 0.32f;             ///< seconds into the swing when the hit closes
    int   combo = 1;                  ///< hits in one chain (1 = no chain)
    float comboWindow = 0.6f;         ///< seconds after a swing to continue the chain
    float comboScale = 0.25f;         ///< extra damage fraction for each hit after the first
    bool  useHitbox = false;          ///< a box in front, instead of the cone
    float hitboxWidth = 1.2f;
    float hitboxHeight = 1.6f;
    float hitboxDepth = 1.4f;

    // Gun.
    float fireRate = 5.0f;            ///< shots per second
    int   magazine = 12;              ///< rounds the gun holds (0 = no magazine, it never reloads)
    int   ammo = -1;                  ///< spare rounds (-1 = the spare pile never runs out)
    float reload = 1.2f;              ///< seconds to refill the gun from the spare rounds
    float spread = 0.0f;              ///< aim wobble, degrees (the shooter uses this)
    bool  hitscan = true;             ///< an instant shot; off spawns a projectile
    std::string projectile;           ///< prefab path when the shot is not instant
    float projectileSpeed = 30.0f;

    const std::string& DisplayName() const { return name.empty() ? id : name; }
};

/// The weapons loaded from one `.okayweapon` file. `Get()` is the project's list.
/// A component can also name a file of its own; `Cached` reads that file once.
class WeaponBook {
public:
    static WeaponBook& Get();
    /// The book at `path`, loaded the first time. An empty path is `Get()`.
    static const WeaponBook& Cached(const std::string& path);
    /// Drop a cached file so the next read sees what is on disk now.
    static void Forget(const std::string& path);

    bool Load(const std::string& path, std::string* err = nullptr);
    bool Save(const std::string& path) const;
    bool LoadText(const std::string& text, std::string* err = nullptr);
    std::string Serialize() const;
    const std::string& Path() const { return m_path; }
    void Clear();

    const WeaponDefinition* Find(const std::string& id) const;
    WeaponDefinition* FindMutable(const std::string& id);
    /// The first weapon whose item id is `itemId`.
    const WeaponDefinition* ForItem(const std::string& itemId) const;
    WeaponDefinition& Put(const WeaponDefinition& def);
    bool Remove(const std::string& id);
    const std::deque<WeaponDefinition>& Weapons() const { return m_weapons; }
    std::deque<WeaponDefinition>& Weapons() { return m_weapons; }

    /// Plain-English problems a designer should fix (duplicate ids, a hit window
    /// that closes before it opens, a gun that cannot fire, an item nobody defined).
    std::vector<std::string> Issues() const;

private:
    std::deque<WeaponDefinition> m_weapons;
    std::string m_path;
};

/// The weapon `id` in `file` (empty file = the project book). A file with one
/// weapon and an empty id means that weapon.
const WeaponDefinition* FindWeapon(const std::string& id, const std::string& file);

/// The weapon tied to whatever this character is holding in the main hand:
/// the item's custom "weapon" field, or the weapon whose item id matches.
const WeaponDefinition* WeaponForHolder(GameObject* go);

/// Hurt `target` once: its Health, or its survival health, or its RPG stats, or
/// its NPC health, whichever it has. `impulse` is added to a dynamic rigid body
/// on that object (a knockback). Returns true when something took the damage.
bool ApplyCombatDamage(GameObject* target, float amount, const Vec3& from, const Vec3& impulse);

} // namespace okay
