#pragma once
// Keyboard and gamepad focus for in-game UI: when the ring is shown, and how it looks.
//
// The ring is drawn only after keyboard, gamepad or script focus. A mouse click hides it
// until the next one of those. The look comes from the widget's UI theme (colour, thickness,
// padding, optional pulse). Accessibility can turn on a high-visibility ring and scale the
// thickness. See docs/accessibility.md.
#include "okay/Render/Color.hpp"

namespace okay {

class GameObject;

/// Which device last moved focus. The ring is drawn for keyboard, gamepad and script focus.
class UIFocus {
public:
    enum class Source { None, Keyboard, Pointer, Script };

    static Source Current();
    /// True when the focused widget should draw its ring.
    static bool RingVisible();
    static void NoteKeyboard();   ///< a menu key or a D-pad / face button
    static void NotePointer();    ///< a mouse click hides the ring
    static void NoteScript();     ///< ui_set_focus / UIButton.Focus shows the ring
    static void Reset();          ///< tests and a new game: ring hidden, pulse clock automatic

    /// Hold the pulse clock (tests, screenshots). ClearManualPulse lets it follow real time again.
    static void SetPulseTime(float seconds);
    static void ClearManualPulse();
    static float PulseTime();
};

/// The ring to draw around one widget, after the theme and the accessibility settings.
struct UIFocusStyle {
    Color color = Color::FromBytes(120, 182, 255, 230);
    float thickness = 2.0f;     ///< stroke width in pixels
    float padding = 3.0f;       ///< gap between the widget edge and the stroke centre
    bool pulse = false;         ///< brightness breathes; held still by reduce motion or reduce flashing
    bool highVisibility = false;///< a dark halo plus a thick bright stroke
};

/// Theme on the widget or an ancestor, else a global theme, else the first theme in the scene.
UIFocusStyle ResolveUIFocusStyle(GameObject* widget);
/// 1 when the ring is steady, else about 0.55 to 1 over a one-second breath.
float UIFocusPulseAlpha(const UIFocusStyle& style);

} // namespace okay
