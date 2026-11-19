#pragma once
// UI Constraints: limits and a portrait look for one widget. Min and max size, an aspect-ratio lock, and a second
// position and size used when the screen is taller than wide. Only widgets that carry this component are touched, and
// it saves nothing for a scene that has none. ApplyUIConstraints runs in the player (and in editor Play), never while
// editing, so the saved scene always holds the landscape values.
#include "okay/Scene/Scene.hpp"
#include "okay/Components/UIElement.hpp"
#include <algorithm>

namespace okay {

class UIConstraints final : public Behaviour {
public:
    Vec2  minSize{0.0f, 0.0f};          ///< 0 = no minimum on that axis
    Vec2  maxSize{0.0f, 0.0f};          ///< 0 = no maximum
    bool  lockAspect = false;
    float aspect = 0.0f;                ///< width / height to keep; 0 = the widget's size when first seen
    bool  portraitOverride = false;
    Vec2  portraitPosition{0.0f, 0.0f}; ///< used instead of the widget's position when the screen is taller than wide
    Vec2  portraitSize{100.0f, 100.0f};
    // not saved: what the landscape values were while the portrait ones are shown
    bool  inPortrait = false;
    Vec2  landscapePosition{0.0f, 0.0f}, landscapeSize{0.0f, 0.0f};
};

/// The size after the limits (pure). With an aspect lock the height follows the width, and a limit on either axis keeps the shape.
inline Vec2 ConstrainSize(Vec2 s, const UIConstraints& c, float currentAspect = 0.0f) {
    const float a = c.aspect > 0.0f ? c.aspect : currentAspect;
    auto clampAxis = [](float v, float lo, float hi) { if (lo > 0.0f) v = std::max(v, lo); if (hi > 0.0f) v = std::min(v, hi); return v; };
    s.x = clampAxis(s.x, c.minSize.x, c.maxSize.x);
    if (c.lockAspect && a > 0.0f) {
        s.y = s.x / a;
        const float y2 = clampAxis(s.y, c.minSize.y, c.maxSize.y);
        if (y2 != s.y) { s.y = y2; s.x = s.y * a; }
    } else s.y = clampAxis(s.y, c.minSize.y, c.maxSize.y);
    return s;
}

/// Apply every UIConstraints in the scene for a screen of w x h. Returns how many widgets changed.
inline int ApplyUIConstraints(Scene& scene, float w, float h) {
    int n = 0;
    const bool portrait = h > w;
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        auto* c = up->GetComponent<UIConstraints>();
        if (!c || !c->enabled) continue;
        UIRect r = GetUIRect(up.get());
        if (!r.valid || !r.position) continue;
        if (c->portraitOverride && portrait != c->inPortrait) {
            if (portrait) { c->landscapePosition = *r.position; if (r.sizePtr) c->landscapeSize = *r.sizePtr; *r.position = c->portraitPosition; if (r.sizePtr) *r.sizePtr = c->portraitSize; }
            else { *r.position = c->landscapePosition; if (r.sizePtr) *r.sizePtr = c->landscapeSize; }
            c->inPortrait = portrait; ++n;
        }
        if (r.sizePtr && !r.Stretches()) {
            const float cur = r.sizePtr->y > 0.0f ? r.sizePtr->x / r.sizePtr->y : 0.0f;
            const Vec2 s = ConstrainSize(*r.sizePtr, *c, cur);
            if (s.x != r.sizePtr->x || s.y != r.sizePtr->y) { *r.sizePtr = s; ++n; }
        }
    }
    return n;
}

} // namespace okay
