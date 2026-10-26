#pragma once
#include "okay/Scene/Component.hpp"

namespace okay {

/// Clips this object's UI descendants to the rect of the widget on the same
/// GameObject (a panel, an image, ...): anything that would draw outside it is cut
/// off, and the hidden parts can't be clicked either. Masks nest (a child is
/// clipped by every mask and scroll view above it) and the mask's own widget still
/// draws normally unless `showGraphic` is off.
class UIMask final : public Behaviour {
public:
    bool showGraphic = true;   // draw the masking widget itself (off = invisible clip area)
};

} // namespace okay
