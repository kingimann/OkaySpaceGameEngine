#pragma once
#include "okay/Components/DamageGuard.hpp"
#include "okay/Scene/Component.hpp"
#include <string>
#include <algorithm>

namespace okay {

/// Per-piece data carried by every structure the StructureBuilder places: its
/// upgrade tier (wood/stone/metal), health, and the resource + amount it cost (so
/// it can be repaired, upgraded, or refunded against an Inventory). Lives on the
/// placed GameObject so it saves with the scene and other systems (combat, raids)
/// can damage it.
class BuildPiece : public Behaviour {
public:
    int         tier      = 0;          ///< 0 = wood, 1 = stone, 2 = metal
    float       health    = 200.0f;
    float       maxHealth = 200.0f;
    std::string material  = "Wood";     ///< resource spent to build / upgrade / repair it
    int         cost      = 10;         ///< units of `material` it cost (basis for refund/repair)

    /// Apply damage; returns true if it just got destroyed (health hit 0).
    bool Damage(float amount) {
        if (!AcceptFiniteAmount(amount, m_warnedNonFinite, gameObject, "BuildPiece.Damage")) return false;
        if (amount <= 0.0f) return false;
        health -= amount;
        if (health <= 0.0f) { health = 0.0f; return true; }
        return false;
    }
    void Heal(float amount) {
        if (!AcceptFiniteAmount(amount, m_warnedNonFinite, gameObject, "BuildPiece.Heal")) return;
        health = std::min(maxHealth, health + std::max(0.0f, amount));
    }
    bool FullHealth() const { return health >= maxHealth - 1e-3f; }

private:
    bool m_warnedNonFinite = false;   // one warning for a NaN / infinite amount (DamageGuard.hpp)
};

} // namespace okay
