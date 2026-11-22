#pragma once
#include <string>
#include <vector>

namespace okay {

/// Rumble patterns: a short list of steps (heavy motor, light motor, seconds) played one after the
/// other on one controller, so a game can say "heartbeat" instead of timing Input::Rumble calls.
/// Each step becomes one Input::Rumble request when it starts; the runtime plays it as usual.
/// Advanced by InputActions::Tick(dt). Patterns: "tap", "buzz", "pulse", "heartbeat", "ramp", "ramp down".
/// Written for this project from the Input::Rumble interface; the shapes are ordinary ones.
class InputHaptics {
public:
    struct Step { float low = 0, high = 0, seconds = 0; };
    /// Start a built-in pattern on a controller (0..3). False for an unknown name. Replaces one already playing there.
    static bool Play(const std::string& pattern, int player = 0);
    /// Start your own steps.
    static bool Play(const std::vector<Step>& steps, int player = 0);
    static void Stop(int player = 0);
    static bool Playing(int player = 0);
    /// The steps of a built-in pattern (empty for an unknown name).
    static std::vector<Step> Pattern(const std::string& name);
    /// Advance every pattern by dt seconds (called from InputActions::Tick).
    static void Update(float dt);
    /// Scales every pattern: 0 = off (a player's vibration setting), 1 = full. Saved in Prefs as "input.haptics".
    static void SetStrength(float strength);
    static float Strength();
};

} // namespace okay
