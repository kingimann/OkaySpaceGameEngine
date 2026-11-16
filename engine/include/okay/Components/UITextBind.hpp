#pragma once
#include "okay/Scene/Component.hpp"
#include <string>

namespace okay {

class GameObject;

/// Data binding for UI text: a `format` string with `{key}` placeholders that
/// are replaced each frame with the live value of a matching visual-scripting
/// variable (ActionList var) or Prefs value, and the result is written into the
/// sibling TextRenderer/Button/InputField. So a label authored as
/// `Score: {score}` shows the live `score` variable — set it from a visual script
/// (Set Variable "score") or with prefs_set("score", v) and the HUD follows.
/// Numbers are prettified (5.0 -> "5"). Unknown keys resolve to an empty string,
/// or to a default written after a bar: `{score|0}`. `{{` / `}}` emit literal braces.
/// `{owner.name}`, `{owner.level}`, `{owner.health}`, `{owner.maxHealth}`, `{owner.healthPct}` read the character this
/// text belongs to (the nearest parent with health, stats or a Character; okay/Components/UIOwnerData.hpp), so a
/// nameplate made a child of a character shows that character. With no owner they fall back to the global variable
/// of the same name without "owner.".
class UITextBind : public Behaviour {
public:
    std::string format = "{value}";

    /// Resolve `format` against visual-scripting vars + Prefs (also usable standalone).
    static std::string Resolve(const std::string& format);
    /// The same, with `owner.` keys read from the owner of `self` (null: no owner).
    static std::string Resolve(const std::string& format, const GameObject* self);
    /// Resolve a single `{key}`: a visual-scripting variable first, then a Prefs value.
    static std::string ResolveKey(const std::string& key);

    void Start() override { Apply(); }
    void Update(float) override { Apply(); }

private:
    void Apply();   // implemented in UITextBind.cpp
};

} // namespace okay
