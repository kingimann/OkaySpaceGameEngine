#pragma once
#include "okay/Scene/Component.hpp"
#include <string>

namespace okay {

/// Data binding for UI text: a `format` string with `{key}` placeholders that
/// are replaced each frame with the matching Prefs values, and the result is
/// written into the sibling TextRenderer. So a label authored as
/// `text bind="Score: {score}"` shows the live value of the `score` pref —
/// update it from script with prefs_set("score", v) and the HUD follows.
/// Unknown keys resolve to an empty string; `{{` / `}}` emit literal braces.
class UITextBind : public Behaviour {
public:
    std::string format = "{value}";

    /// Resolve `format` against the current Prefs values (also usable standalone).
    static std::string Resolve(const std::string& format);

    void Start() override { Apply(); }
    void Update(float) override { Apply(); }

private:
    void Apply();   // implemented in UITextBind.cpp
};

} // namespace okay
