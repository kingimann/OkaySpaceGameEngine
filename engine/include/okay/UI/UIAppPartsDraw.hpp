#pragma once
#include "okay/UI/UIPainter.hpp"

namespace okay {
class GameObject;
namespace ui2d {

/// Draw the app parts on a GameObject for layer `kind` (K_AppParts: menu bar, toolbar, tree view, data table, wizard, form
/// group labels, colour square; K_AppPopup: a menu bar's open dropdown). False when `kind` is not one of those two layers.
bool DrawAppParts(int kind, GameObject* go, Painter& p, float logicalW, float logicalH, float opacity);

} // namespace ui2d
} // namespace okay
