#pragma once

namespace okay {

/// Fullscreen, vsync and the Low/Medium/High quality preset the Settings screen
/// drives. Stored in Prefs (video.fullscreen, video.vsync, video.quality) next
/// to the audio mixer settings. Hosts apply a change when Generation() moves.
struct DisplaySettings {
    static bool Fullscreen();
    static bool VSync();
    static int  Quality();          ///< 0 low, 1 medium, 2 high
    static int  Generation();       ///< bumps on every Set*, not on Load

    static void Load();             ///< prefs -> the values, generation unchanged
    static void Save();             ///< values -> prefs (the host still writes the file)

    static void SetFullscreen(bool on);
    static void SetVSync(bool on);
    static void SetQuality(int q);

    /// Push the quality preset into the renderer's shadow / lighting / post flags.
    static void ApplyQuality();
};

} // namespace okay
