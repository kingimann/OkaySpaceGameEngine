#pragma once
#include "okay/UI/UIPainter.hpp"

namespace okay {
class GameObject;
namespace ui2d {

/// Draw the game HUD parts on a GameObject (radial menu, kill feed, ammo counter, interaction prompt, objective tracker,
/// compass, item tooltip card, ability icon). Always true: the layer is theirs.
bool DrawGameHud(GameObject* go, Painter& p, float logicalW, float logicalH, float opacity);

} // namespace ui2d
} // namespace okay
