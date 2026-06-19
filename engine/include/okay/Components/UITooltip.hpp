#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

/// Shows a small text box near the cursor while the pointer hovers over the
/// sibling UI widget (button, panel, image, slider, …) for `delay` seconds —
/// the classic "what does this do?" hint. Add it to any GameObject that already
/// carries a screen-space UI component; the tooltip uses that widget's rect for
/// the hover test. Rendered by the editor (Game view) and the built game.
class UITooltip : public Behaviour {
public:
    std::string text = "Tooltip";
    float delay = 0.5f;                              // hover seconds before showing
    Color background = Color::FromBytes(15, 17, 24, 235);
    Color textColor  = Color::White;
    Color borderColor = Color::FromBytes(255, 255, 255, 50);

    /// True once the pointer has hovered the sibling widget for `delay` seconds.
    bool Ready() const { return m_hover >= delay && m_inside; }
    /// Accumulated hover time (for renderers that want a fade-in).
    float HoverTime() const { return m_hover; }

    void Update(float dt) override;   // implemented in UITooltip.cpp

private:
    float m_hover = 0.0f;
    bool  m_inside = false;
};

} // namespace okay
