#pragma once
#include "okay/UI/UIPainter.hpp"

namespace okay {
class GameObject;
namespace ui2d {

/// Draw one of the extra widgets (search, date, files, accordion, breadcrumbs,
/// pages, skeleton, sheet). False when `kind` is not one of those layers.
bool DrawMoreUIWidget(int kind, GameObject* go, Painter& p, float logicalW, float logicalH, float opacity);

} // namespace ui2d
} // namespace okay
