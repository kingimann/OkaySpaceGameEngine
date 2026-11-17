#pragma once
#include <functional>
#include <memory>
#include <string>

namespace okay {

class GameObject;

/// A weapon on an object, whichever component runs it (RangedWeapon, TopDownShooterController). The components keep
/// their settings and saved records; each module registers an adapter (engine/modules/<name>/register.cpp).
/// Ammo is part of the contract: what is in the magazine and what is spare.
class IWeapon {
public:
    virtual ~IWeapon() = default;
    /// Fire one shot now if the rate, the reload and the rounds allow it. True when a shot left the weapon.
    virtual bool Fire() = 0;
    /// Start refilling the magazine. No effect when it is full, already reloading, or the weapon never reloads.
    virtual void Reload() = 0;
    virtual bool IsReloading() const = 0;
    /// Rounds in the magazine (-1 when the weapon has no magazine: it never runs dry).
    virtual int Rounds() const = 0;
    virtual int MagazineSize() const = 0;
    /// Spare rounds (-1: they never run out).
    virtual int Spare() const = 0;
    virtual bool IsEmpty() const { return MagazineSize() > 0 && Rounds() <= 0; }
    virtual const char* Model() const = 0;
};

namespace weapon {
using Finder = std::function<std::unique_ptr<IWeapon>(GameObject*)>;
void AddFinder(const std::string& name, Finder f);
void ClearFinders();   // tests
/// The weapon on `go` (not its parents), or null. The first finder that has one wins.
std::unique_ptr<IWeapon> Find(GameObject* go);
} // namespace weapon

} // namespace okay
