#pragma once
#include <string>
#include <vector>

namespace okay {

/// How the game window is shown. A host maps it to its windowing library (for SDL:
/// Windowed = 0, Borderless = SDL_WINDOW_FULLSCREEN_DESKTOP, Fullscreen = SDL_WINDOW_FULLSCREEN
/// with the display mode of `DisplaySettings::EffectiveResolution()`).
enum class WindowMode { Windowed = 0, Borderless = 1, Fullscreen = 2 };

/// A window or display size in pixels.
struct DisplayResolution {
    int width = 0;
    int height = 0;
    bool Valid() const { return width > 0 && height > 0; }
    float Aspect() const { return height > 0 ? (float)width / (float)height : 0.0f; }
    std::string Label() const { return std::to_string(width) + " x " + std::to_string(height); }
    bool operator==(const DisplayResolution& o) const { return width == o.width && height == o.height; }
    bool operator!=(const DisplayResolution& o) const { return !(*this == o); }
};

/// What a quality level sets in the renderer's shared switches (declared in
/// Render/SoftwareRenderer.hpp, read by the software path and by the OpenGL and Direct3D
/// backends).
struct QualityPreset {
    const char* name;
    bool  shadows;
    bool  perPixelLighting;
    bool  bloom;
    bool  ssao;
    bool  fxaa;
    int   shadowResolution;   ///< texels per cascade (ShadowMapResolution())
    float shadowDistance;     ///< world units the cascades reach (ShadowDistance())
    int   shadowCascades;     ///< ShadowCascades()
    float shadowSoftness;     ///< ShadowSoftness(), in texels
};

/// The values the Settings screen drives: window mode, resolution, vsync and the
/// Low/Medium/High quality preset. All of it is plain data (no window is needed), kept in
/// Prefs under `video.*` next to the audio mixer settings, and applied by the host when
/// Generation() moves. Use from the main thread.
///
/// Rules a host can rely on:
///  * a resolution is clamped into [kMinWidth x kMinHeight, kMaxWidth x kMaxHeight];
///  * `Resolutions()` lists what a settings screen offers (the display's modes when the host
///    gives them, else a standard list), sorted small to large, with no repeats;
///  * `EffectiveResolution()` is the size to apply for the current mode;
///  * the quality preset is applied only once the player chose one (SetQuality, or a saved
///    `video.quality`), so a game's own render settings (Build Settings) stay in force until then.
struct DisplaySettings {
    static constexpr int kMinWidth = 640, kMinHeight = 360;
    static constexpr int kMaxWidth = 16384, kMaxHeight = 16384;
    static constexpr int kQualityLevels = 3;

    // ---- Window mode ----
    static WindowMode Mode();
    /// True in Borderless and Fullscreen (what the older Fullscreen setting meant).
    static bool Fullscreen();
    static void SetMode(WindowMode mode);
    /// On = Borderless (or the mode already in use when it is not windowed); off = Windowed.
    static void SetFullscreen(bool on);
    static std::vector<WindowMode> Modes();                       ///< Windowed, Borderless, Fullscreen
    static const char* ModeName(WindowMode mode);                 ///< "windowed", "borderless", "fullscreen"
    /// Read a mode's name (any case; "window", "fullscreen_desktop" and "0/1/2" are accepted too).
    static bool ParseMode(const std::string& text, WindowMode& out);

    // ---- Resolution ----
    /// False until a resolution was set or loaded: the host keeps its own default size.
    static bool HasResolution();
    static int  Width();                                          ///< 0 while none is set
    static int  Height();
    static DisplayResolution Resolution();
    /// Set the size (clamped into the allowed range; any size in range is kept, a window may be
    /// any size). Bumps Generation().
    static void SetResolution(int width, int height);
    /// Choose from `Resolutions()` by index (out of range = clamped to the ends).
    static void SetResolutionIndex(int index);
    /// Forget the chosen size: the host's default applies again.
    static void ClearResolution();
    /// What a settings screen offers. Set by SetSupportedResolutions, else a standard list
    /// (800x600 ... 3840x2160) cut at SetDesktopResolution() when the host gave it.
    static const std::vector<DisplayResolution>& Resolutions();
    /// The display's own mode list. Entries outside the allowed range and repeats are dropped,
    /// the rest sorted small to large. An empty list goes back to the standard one.
    static void SetSupportedResolutions(const std::vector<DisplayResolution>& modes);
    /// The desktop's current size: the standard list stops there (and includes it), and
    /// Borderless uses it. 0 x 0 = unknown.
    static void SetDesktopResolution(int width, int height);
    static DisplayResolution DesktopResolution();
    /// `width` x `height` forced into the allowed range.
    static DisplayResolution ClampResolution(int width, int height);
    /// The entry of `Resolutions()` closest to the size (same size wins; else the smallest
    /// distance in width and height, the smaller size on a tie). Invalid when the list is empty.
    static DisplayResolution NearestResolution(int width, int height);
    /// Position of the current resolution in `Resolutions()`, or -1 when none is set or it is not listed.
    static int ResolutionIndex();
    /// The size to apply for the current mode: Windowed = the chosen size (clamped); Fullscreen =
    /// the listed size nearest to it; Borderless = the desktop's size when known, else the chosen
    /// size. Invalid (0 x 0) when nothing is chosen and the desktop is unknown: keep the host's size.
    static DisplayResolution EffectiveResolution();

    // ---- VSync ----
    static bool VSync();
    static void SetVSync(bool on);

    // ---- Quality ----
    static int  Quality();          ///< 0 low, 1 medium, 2 high
    static void SetQuality(int q);  ///< clamped to 0..2; applies the preset and bumps Generation()
    static bool QualityChosen();    ///< true once the player (or a saved setting) chose a level
    static const QualityPreset& Preset(int quality);   ///< the table row for a level (clamped)
    static const char* QualityName(int quality);       ///< "Low", "Medium", "High"
    /// Set the renderer's shadow, lighting and post switches from level `quality`, chosen or not.
    static void ApplyPreset(int quality);
    /// Apply the player's chosen quality: a host calls this when Generation() moves. Does
    /// nothing until a level was chosen, so Build Settings values are not overwritten.
    static void ApplyQuality();

    // ---- Persistence ----
    static int  Generation();       ///< bumps on every Set*, not on Load
    static void Load();             ///< prefs -> the values, generation unchanged; bad values are ignored or clamped
    static void Save();             ///< values -> prefs (the host still writes the file)
    /// Load the saved values and bump Generation() when there are any, so a host that watches
    /// it applies them on its first frame. Call once after Prefs::Load at start-up.
    static void Restore();
    /// A short summary: "1920 x 1080, borderless, vsync on, quality High".
    static std::string Summary();
    /// Back to the defaults (windowed, vsync on, quality High not chosen, no resolution, the
    /// standard resolution list); bumps Generation(). For tools and tests.
    static void ResetToDefaults();
};

} // namespace okay
