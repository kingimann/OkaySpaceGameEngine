#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {

class Scene;

/// Builds the text lines of an in-game debug overlay (FPS, frame time, object count,
/// anything you add). No drawing here: the UI draws the returned lines wherever it likes.
///
///     DebugOverlay::Stats s = DebugOverlay::Capture(&scene);
///     for (const std::string& line : DebugOverlay::BuildLines(s)) DrawLabel(line);
///
/// The overlay is switched by the "debug.overlay" cvar (see Enabled / Toggle), so a console
/// command, a key binding or `--cvar debug.overlay=1` all work. Main thread only.
class DebugOverlay {
public:
    struct Stats {
        float fps = 0.0f;                 ///< smoothed frames per second
        float frameMs = 0.0f;             ///< last frame, milliseconds
        std::size_t objectCount = 0;      ///< live game objects in the scene
        std::uint64_t frame = 0;          ///< frame counter
        float timeScale = 1.0f;
        bool  paused = false;
    };

    /// Fill Stats from the Time class and (if not null) the scene.
    static Stats Capture(const Scene* scene);

    /// "FPS: 60.0 (16.7 ms)", "Objects: 42", plus any extra lines. With `detailed`, also the
    /// frame number, time scale and build summary. Empty when the overlay is off and
    /// `ignoreEnabled` is false.
    static std::vector<std::string> BuildLines(const Stats& stats, bool detailed = false, bool ignoreEnabled = true);

    /// The on/off switch (the "debug.overlay" cvar, default off).
    static bool Enabled();
    static void SetEnabled(bool on);
    static void Toggle() { SetEnabled(!Enabled()); }

    /// Add a line produced each time BuildLines runs (e.g. "Bullets: 12"). Returns an id for RemoveLine.
    static int  AddLine(std::function<std::string()> provider);
    static void RemoveLine(int id);
    static void ClearLines();
};

} // namespace okay
