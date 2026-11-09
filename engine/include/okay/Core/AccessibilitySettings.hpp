#pragma once
// Accessibility options a player sets once and every part of the game honours
// (docs/accessibility.md): text size, high contrast, reduced motion and flashing, colour
// vision correction, subtitle and caption settings, hold versus toggle, aim assist and
// screen-reader hints.
//
// AccessibilitySettings is the plain data and saves to Prefs ("a11y.*"). The Accessibility
// service holds the settings in use, tells listeners which parts changed, and has the small
// helpers games call (ScaleText, MotionScale, FilterColor, ToggleFor...).
#include "okay/Core/Subtitles.hpp"
#include "okay/Graphics/ColorVision.hpp"

#include <cstdint>
#include <functional>
#include <set>
#include <string>

namespace okay {

enum class AimAssist { Off, Low, Medium, High };
const char* AimAssistName(AimAssist level);                       ///< "off", "low", "medium", "high"
bool AimAssistFromName(const std::string& name, AimAssist& level);
/// How hard aim assist helps, 0 .. 1 (Off 0, Low 0.35, Medium 0.65, High 1): a multiplier for the game's own assist.
float AimAssistStrength(AimAssist level);

/// A part of the settings, for change notifications (combine with |).
enum AccessibilityField : std::uint32_t {
    kA11yTextScale       = 1u << 0,
    kA11yHighContrast    = 1u << 1,
    kA11yReduceMotion    = 1u << 2,
    kA11yReduceFlashing  = 1u << 3,
    kA11yColorblind      = 1u << 4,
    kA11ySubtitles       = 1u << 5,
    kA11yHoldToggle      = 1u << 6,
    kA11yAimAssist       = 1u << 7,
    kA11yScreenReader    = 1u << 8,
    kA11yAll             = (1u << 9) - 1,
};

struct AccessibilitySettings {
    float textScale = 1.0f;               ///< UI text multiplier, 0.75 .. 2
    bool highContrast = false;            ///< stronger contrast between text and its background
    bool reduceMotion = false;            ///< no camera shake, parallax or screen sway; shorter transitions
    bool reduceFlashing = false;          ///< no strobing or full-screen flashes
    ColorblindMode colorblind = ColorblindMode::None;   ///< the player's colour vision: the game shifts colours to suit
    float colorblindStrength = 1.0f;      ///< 0 .. 1, how far
    SubtitleSettings subtitles;
    bool toggleAllHolds = false;          ///< every "hold to ..." action toggles on press instead
    std::set<std::string> toggleActions;  ///< actions that toggle ("Sprint", "Aim", "Crouch") while others hold
    AimAssist aimAssist = AimAssist::Medium;
    bool screenReaderHints = false;       ///< announce menus, focus and events to a screen reader

    /// Bring every value into range.
    void Clamp();
    /// Whether `action` toggles instead of holding.
    bool ToggleFor(const std::string& action) const { return toggleAllHolds || toggleActions.count(action) > 0; }
    /// The parts that differ from `other` (AccessibilityField bits).
    std::uint32_t Diff(const AccessibilitySettings& other) const;
    bool operator==(const AccessibilitySettings& o) const { return Diff(o) == 0; }
    bool operator!=(const AccessibilitySettings& o) const { return Diff(o) != 0; }

    /// Read from / write to Prefs under "a11y.*" (Prefs::Save writes them to disk). Load leaves a
    /// value alone when Prefs has none for it.
    void Load();
    void Save() const;
};

/// The settings in use.
class Accessibility {
public:
    static const AccessibilitySettings& Get();
    /// Replace the settings (clamped). Listeners hear which parts changed; nothing happens when
    /// nothing did. The shared CaptionManager (CaptionManager::Main) takes the subtitle settings.
    static void Set(const AccessibilitySettings& settings);
    /// Read the player's saved choices from Prefs (call after Prefs::Load) and apply them.
    static void Load();
    /// Remember the settings in Prefs (Prefs::Save writes them to disk).
    static void Save();
    /// Back to the defaults, no listeners, no announcer (tests, new game).
    static void Reset();

    using Listener = std::function<void(const AccessibilitySettings& now, std::uint32_t changed)>;
    /// Hear about changes (called inside Set). Returns an id.
    static int AddListener(Listener fn);
    static void RemoveListener(int id);

    // ---- One setting at a time (each notifies like Set) ----
    static void SetTextScale(float scale);
    static void SetHighContrast(bool on);
    static void SetReduceMotion(bool on);
    static void SetReduceFlashing(bool on);
    static void SetColorblind(ColorblindMode mode, float strength = 1.0f);
    static void SetSubtitles(const SubtitleSettings& s);
    static void SetToggleAllHolds(bool on);
    static void SetToggleAction(const std::string& action, bool toggle);
    static void SetAimAssist(AimAssist level);
    static void SetScreenReaderHints(bool on);

    // ---- What games ask ----
    /// A text size in pixels scaled by the player's text size.
    static float ScaleText(float pixels);
    /// 0 with reduced motion, else 1: multiply camera shake, sway and parallax by it.
    static float MotionScale();
    /// A transition's length shortened (to a quarter) with reduced motion.
    static float TransitionSeconds(float seconds);
    /// 0 with reduced flashing, else 1: multiply flash and strobe intensity by it.
    static float FlashScale();
    /// `c` shifted for the player's colour vision (unchanged with no deficiency set).
    static Color FilterColor(const Color& c);
    /// `foreground` as it should be drawn over `background`: with high contrast on, moved until
    /// the contrast is at least 7:1 (WCAG AAA), else unchanged.
    static Color TextColor(const Color& foreground, const Color& background);
    /// Whether this action toggles instead of holding.
    static bool ToggleFor(const std::string& action);
    /// 0 .. 1 for the player's aim assist level.
    static float AimAssistStrength();

    // ---- Screen reader ----
    using Announcer = std::function<void(const std::string& text)>;
    /// Where announcements go (a platform's screen reader or text-to-speech). Null removes it.
    static void SetAnnouncer(Announcer announcer);
    /// Say `text` to the screen reader. False (nothing said) when hints are off or there is no announcer.
    static bool Announce(const std::string& text);
};

/// Turns a held input into the action's state for a player who chose hold or toggle. In hold
/// mode the action is active while the input is down; in toggle mode each press flips it.
class HoldToggle {
public:
    /// `held`: the input is down this frame. `toggleMode`: the player's choice (Accessibility::ToggleFor).
    /// Returns whether the action is active.
    bool Update(bool held, bool toggleMode);
    bool Active() const { return m_active; }
    /// Turn it off (a toggled sprint when stamina runs out) without waiting for another press.
    void Cancel() { m_active = false; }
    void Reset() { m_active = false; m_held = false; }

private:
    bool m_active = false;
    bool m_held = false;
};

} // namespace okay
