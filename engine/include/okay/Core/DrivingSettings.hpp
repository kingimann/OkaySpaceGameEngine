#pragma once
// Player-facing driving options, saved in the user's preferences (Prefs keys "driving.*").
// A vehicle reads them when it first updates (VehicleController), so a player's choice follows them into every
// vehicle without changing the scene. Written from scratch for this engine.
#include "okay/Core/Prefs.hpp"
#include <algorithm>

namespace okay {

struct DrivingSettings {
    bool  mph = false;                 ///< speedometer in mph (false = km/h)
    float steerSensitivity = 1.0f;     ///< multiplies the vehicle's own steering sensitivity (0.5 .. 1.5)
    int   cameraView = -1;             ///< -1 = what the vehicle says, 0 Chase, 1 Far, 2 Hood, 3 Bonnet
    bool  assists = true;              ///< false: ABS, traction control, stability control and steering assist off (hard mode)
    bool  invertSteer = false;
    bool  hasUserChoice = false;       ///< true once the player saved something

    static DrivingSettings Load() {
        DrivingSettings s;
        s.hasUserChoice = Prefs::Has("driving.steer") || Prefs::Has("driving.assists") || Prefs::Has("driving.camera")
                       || Prefs::Has("driving.mph") || Prefs::Has("driving.invert");
        s.mph = Prefs::GetBool("driving.mph", false);
        s.steerSensitivity = std::min(1.5f, std::max(0.5f, Prefs::GetFloat("driving.steer", 1.0f)));
        s.cameraView = std::min(3, std::max(-1, Prefs::GetInt("driving.camera", -1)));
        s.assists = Prefs::GetBool("driving.assists", true);
        s.invertSteer = Prefs::GetBool("driving.invert", false);
        return s;
    }
    void Save() const {
        Prefs::SetBool("driving.mph", mph);
        Prefs::SetFloat("driving.steer", steerSensitivity);
        Prefs::SetInt("driving.camera", cameraView);
        Prefs::SetBool("driving.assists", assists);
        Prefs::SetBool("driving.invert", invertSteer);
    }
    /// Speed in the unit the player chose: metres per second in, km/h or mph out.
    float DisplaySpeed(float metresPerSecond) const { return std::fabs(metresPerSecond) * (mph ? 2.2369363f : 3.6f); }
    const char* UnitName() const { return mph ? "mph" : "km/h"; }
};

} // namespace okay
