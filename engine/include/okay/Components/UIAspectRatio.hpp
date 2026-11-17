#pragma once
// UI Aspect Ratio: a layout container that keeps its own box at a fixed width : height
// inside its parent (or the screen), whatever shape that space is. Children placed
// under it are laid out inside this box, so a 16:9 card, a square minimap frame or a
// portrait phone area stays the right shape on a phone, a tablet and an ultrawide.
// Written from the plain geometry of "fit a rectangle of ratio r into a box".
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Math/Vec2.hpp"
#include <algorithm>

namespace okay {

class UIAspectRatio final : public Behaviour {
public:
    /// How the box is sized from the space it has.
    enum class Mode {
        Fit,          ///< the biggest box of this ratio that fits inside the space (bars left over)
        Fill,         ///< the smallest box of this ratio that covers the space (it may overflow)
        WidthControls,///< width = the space's width, height follows from the ratio
        HeightControls///< height = the space's height, width follows from the ratio
    };
    /// Width divided by height (16:9 is 1.7778, a square is 1).
    float ratio = 16.0f / 9.0f;
    Mode mode = Mode::Fit;
    /// Where the box sits in the left-over space: 0 = left/top, 0.5 = centre, 1 = right/bottom.
    float alignX = 0.5f;
    float alignY = 0.5f;
    /// Empty space kept around the box, in pixels.
    float padding = 0.0f;
    /// Not used for placement (the container always follows the rule above); kept so the
    /// usual widget tools find a rect to show.
    Vec2 position{0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    UIAnchor anchor = UIAnchor::StretchFull;

    /// Set the ratio from a width and a height (a 4:3 card is 4, 3). Ignores zero or negative sizes.
    void SetRatio(float w, float h) { if (w > 0.0f && h > 0.0f) ratio = w / h; }

    /// The box this container takes inside the space whose top-left is `co` and size `cs`
    /// (the parent's rect, or the screen). Never negative.
    void Resolve(Vec2 co, Vec2 cs, Vec2& origin, Vec2& outSize) const {
        const float r = ratio > 0.0001f ? ratio : 1.0f;
        const float pad = padding > 0.0f ? padding : 0.0f;
        const float aw = std::max(0.0f, cs.x - pad * 2.0f), ah = std::max(0.0f, cs.y - pad * 2.0f);
        float w = aw, h = ah;
        switch (mode) {
            case Mode::Fit:
                if (aw / r <= ah) h = aw / r; else w = ah * r;
                break;
            case Mode::Fill:
                if (aw / r >= ah) h = aw / r; else w = ah * r;
                break;
            case Mode::WidthControls: h = aw / r; break;
            case Mode::HeightControls: w = ah * r; break;
        }
        const float ax = std::min(1.0f, std::max(0.0f, alignX)), ay = std::min(1.0f, std::max(0.0f, alignY));
        origin = {co.x + pad + (aw - w) * ax, co.y + pad + (ah - h) * ay};
        outSize = {w, h};
    }
};

} // namespace okay
