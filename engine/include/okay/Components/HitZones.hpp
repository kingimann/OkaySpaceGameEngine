#pragma once
#include "okay/Modules/Damage.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <string>
#include <vector>

namespace okay {

/// Where on a body a hit landed changes how much it hurts. Put it on the same object as the health
/// (HealthStat, SurvivalStats, Stats or an NPC). A hit that names a zone uses it; a hit with only a point
/// is sorted by height above the object's feet into head / body / legs. It also scales by damage type, so
/// a fire-proof guard takes half from fire. Written for this engine from the usual idea of "headshot
/// multipliers"; the numbers are plain defaults, change them freely.
class HitZones : public Behaviour {
public:
    struct Zone { std::string name; float scale = 1.0f; };
    /// Named zones and their multipliers. A hit whose zone is not listed counts as 1.
    std::vector<Zone> zones{{"head", 2.0f}, {"body", 1.0f}, {"legs", 0.75f}};
    /// Height of the head and leg bands above the object's position, in metres (the object stands on its
    /// origin). Used only when a hit has a point and no zone name.
    float headFromHeight = 1.5f;
    float legsToHeight = 0.5f;
    /// Multiplier per DamageType (index = DamageType), 1 = unchanged.
    float typeScale[kDamageTypeCount] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

    float ZoneScale(const std::string& name) const {
        for (auto& z : zones) if (z.name == name) return z.scale;
        return 1.0f;
    }
    /// Zone name for a world point: "head", "body" or "legs".
    std::string ZoneForPoint(const Vec3& p) const {
        const float base = (gameObject && gameObject->transform) ? gameObject->transform->Position().y : 0.0f;
        const float h = p.y - base;
        if (h >= headFromHeight) return "head";
        if (h <= legsToHeight) return "legs";
        return "body";
    }
    /// Fill in the zone name (when the hit has a point and no name) and scale the amount.
    void Scale(DamageEvent& e) const {
        if (e.zone.empty() && e.hasPoint) e.zone = ZoneForPoint(e.point);
        e.amount *= ZoneScale(e.zone);
        int t = (int)e.type;
        if (t >= 0 && t < kDamageTypeCount) e.amount *= typeScale[t];
    }
};

} // namespace okay
