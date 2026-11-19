#pragma once
#include "okay/Scene/Component.hpp"
#include <string>

namespace okay {

/// Gives one widget a role in the UI theme: Primary, Secondary, Danger, Ghost, Outline or Pill.
/// When a UITheme is applied, a widget with this component is styled for its role from the theme's
/// colours (so a Danger button is red in every theme). `variant` uses the UITheme::Variant numbers
/// (0 Default, 1 Primary, 2 Secondary, 3 Danger, 4 Ghost, 5 Outline, 6 Pill).
/// Scripts: `ui_set_variant("Quit", "danger")`.
class UIStyleVariant final : public Behaviour {
public:
    int variant = 0;
    /// Which theme to take colours from (a UITheme's name or its object's name). Empty = the
    /// scene's first theme.
    std::string themeName;
    /// A named look from the style library (okay/Scene/UIStyleLib.hpp): "Glass", "Neon Pill", "Pixel"... Empty = none. Applied when the
    /// theme is applied and when the theme is switched at run time. Script: `ui_set_style("Play", "Neon Pill")`.
    std::string style;
    /// Size (xs, s, m, l, xl) and density (compact, cosy, comfortable, spacious) used with `style`. Empty = m and cosy.
    std::string styleSize;
    std::string styleDensity;
    /// A shared style by name (okay/Components/UIUserStyle.hpp). Empty = none; saved only when set.
    std::string userStyle;
};

} // namespace okay
