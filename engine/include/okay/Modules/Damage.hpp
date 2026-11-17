#pragma once
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <string>

namespace okay {

class GameObject;

/// What kind of harm a hit is. A target can take less or more of one kind (HitZones.typeScale) and
/// gameplay can react to it (a fire hit sets things alight). Generic is what every old caller sends.
enum class DamageType : uint8_t {
    Generic, Bullet, Explosion, Melee, Fall, Fire, Cold, Poison, Starvation, Drowning, Collision, Environment
};
constexpr int kDamageTypeCount = 12;

/// Lowercase script name ("bullet"), and the way back (false for an unknown name).
const char* DamageTypeName(DamageType t);
bool DamageTypeFromName(const std::string& name, DamageType& out);

/// One hit, as every damage source describes it. A bare number is not enough: the target needs to know where
/// it landed (zone), what kind it is, and who did it.
struct DamageEvent {
    float amount = 0.0f;
    DamageType type = DamageType::Generic;
    GameObject* source = nullptr;      ///< the thing that hurt (a bullet, a trap)
    GameObject* instigator = nullptr;  ///< who is responsible (the shooter)
    std::string weapon;                ///< weapon name, "" when none
    std::string zone;                  ///< hit zone name ("head"), "" = let HitZones work it out from `point`
    Vec3 point{};                      ///< world point of the hit
    bool hasPoint = false;
    Vec3 origin{};                     ///< where the hit came from (a shot's muzzle); death falls away from it
    bool hasOrigin = false;

    static DamageEvent Of(float amount, DamageType t = DamageType::Generic) {
        DamageEvent e; e.amount = amount; e.type = t; return e;
    }
};

/// What a hit did.
struct DamageResult {
    bool handled = false;   ///< something with health took the event (false: nothing there can be hurt)
    bool blocked = false;   ///< it was there but took nothing (god mode, i-frames, already dead, amount <= 0)
    bool killed = false;    ///< this hit killed it
    float applied = 0.0f;   ///< health actually lost
};

/// Anything that can be hurt.
class IDamageable {
public:
    virtual ~IDamageable() = default;
    virtual DamageResult ApplyDamage(const DamageEvent& e) = 0;
};

} // namespace okay
