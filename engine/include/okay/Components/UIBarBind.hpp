#pragma once
#include "okay/Scene/Component.hpp"
#include <string>

namespace okay {

/// Data binding for a progress bar: drives the sibling UIProgressBar (or
/// UIRadialProgress) fill from a live visual-scripting variable (an ActionList
/// var) or a Prefs value each frame. Set `var` to the variable name and `max`
/// to the value that means "full" (`min` to the value that means "empty"); the
/// bar fills to (value - min) / (max - min), clamped to 0..1. So a health bar can
/// track the `hp` variable with no scripting — Set Variable "hp" in a visual
/// script (or prefs_set) and the bar follows.
class UIBarBind : public Behaviour {
public:
    std::string var = "value";
    float min = 0.0f;
    float max = 100.0f;

    void Start() override { Apply(); }
    void Update(float) override { Apply(); }

private:
    void Apply();   // implemented in UIBarBind.cpp
};

} // namespace okay
