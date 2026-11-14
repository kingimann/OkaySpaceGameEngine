#pragma once
#include "okay/Math/Vec2.hpp"

namespace okay {

class GameObject;

/// Maps a point in a widget's layout space (the coordinates `GetUIScreenRect`
/// reports, before rotation and scale) onto screen pixels.
struct UIFrameAffine {
    float a = 1.0f, b = 0.0f, tx = 0.0f;
    float c = 0.0f, d = 1.0f, ty = 0.0f;
    Vec2 Apply(Vec2 p) const { return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty}; }
};

/// True when `go` or a UI ancestor frame is turned or scaled away from 1, 1.
bool UIFrameTransformed(GameObject* go);

/// Layout space to screen. The identity when nothing on the way up is transformed,
/// because layout space is then already screen space. False when `go` has no UI rect.
bool UIFrameLayoutToScreen(GameObject* go, float screenW, float screenH, UIFrameAffine& out);

/// Screen point into the widget's own rect: (0, 0) is the top-left of the
/// unscaled layout rect and (width, height) the bottom-right, after undoing
/// this frame's turn and scale and every ancestor's. False when it has no rect
/// or the scale is zero.
bool UIFramePointToLocal(GameObject* go, const Vec2& screen, float screenW, float screenH, Vec2& local);

/// Screen pixels of the four corners after rotation and scale, in order:
/// top-left, top-right, bottom-right, bottom-left.
bool UIFrameCorners(GameObject* go, float screenW, float screenH, Vec2 corners[4]);

} // namespace okay
