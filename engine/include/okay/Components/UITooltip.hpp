#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Core/Localization.hpp"
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
    /// Localized: a string-table key whose text replaces `text` in the current language.
    LocalizedText loc;
    float delay = 0.5f;                              // hover seconds before showing
    Color background = Color::FromBytes(38, 42, 51, 250);
    Color textColor  = Color::White;
    Color borderColor = Color::FromBytes(255, 255, 255, 28);
    // grok/ui-controls — defaults match the renderer’s old cursor offset and plain text.
    /// Draw `<b>` and `#rrggbb` in `text`. Off = the text is drawn as-is.
    bool richText = false;
    /// Pixels from the cursor to the box. {14, 14} is the historical position.
    Vec2 offset{14.0f, 14.0f};
    void SetRichText(bool on) { richText = on; }
    void SetOffset(float x, float y) { offset = {x, y}; }
    void SetDelay(float seconds) { delay = seconds < 0.0f ? 0.0f : seconds; }
    bool RichText() const { return richText; }
    float Delay() const { return delay; }

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
