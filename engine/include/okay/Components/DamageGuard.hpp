#pragma once
// The check every damage, heal and set-health call makes first (issue #888, Finding 57). A NaN amount
// passes an `amount <= 0` guard (every comparison with NaN is false), and one NaN in `health` made the
// object immortal: `health <= 0` is never true again. An infinite amount does the same through
// infinity * 0 (a 100% resistance) or infinity - infinity. So a non-finite amount is refused: nothing
// changes, and the first refusal on each component logs one warning.
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cmath>

namespace okay {

/// True when `amount` is a finite number. Otherwise false, and the first time (per `warned` flag, one
/// per component) a warning names the object and the call: "HealthStat.Damage got nan on 'Enemy'".
inline bool AcceptFiniteAmount(float amount, bool& warned, const GameObject* go, const char* call) {
    if (std::isfinite(amount)) return true;
    if (!warned) {
        warned = true;
        Log::Warning(call, " got ", amount, " on '", go ? go->name : std::string("?"),
                     "': a NaN or infinite amount is ignored (health is unchanged). Warned once per component.");
    }
    return false;
}

} // namespace okay
