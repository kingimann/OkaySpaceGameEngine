#pragma once
// Saved guide lines for the UI designer: positions in design pixels. A scene has this only when the designer chose
// "Keep guides with the scene", so scenes without it save nothing new. Guides never draw in the game.
#include "okay/Scene/Component.hpp"
#include <vector>

namespace okay {
class UIGuides final : public Behaviour {
public:
    std::vector<float> vertical;     ///< x positions
    std::vector<float> horizontal;   ///< y positions
};
} // namespace okay
