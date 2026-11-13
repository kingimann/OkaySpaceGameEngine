#pragma once
// Plain data for monitors and display modes: filtering, sorting, the fallback when a
// saved monitor or mode is gone, and the text form of a saved choice. Nothing here
// opens a window or talks to SDL. The player fills the monitor list; DisplaySettings
// stores the choice. See docs/display_settings.md.
#include <string>
#include <vector>

namespace okay {

/// How the game window is shown. A host maps it onto its windowing library.
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

/// One size a monitor can show. `refreshHz` is 0 when the monitor did not name a rate.
struct DisplayMode {
    int width = 0;
    int height = 0;
    int refreshHz = 0;
    bool Valid() const { return width > 0 && height > 0; }
    DisplayResolution Size() const { return {width, height}; }
    /// "1920 x 1080". The rate is separate so a resolution menu can share one row per size.
    std::string Label() const { return std::to_string(width) + " x " + std::to_string(height); }
    bool operator==(const DisplayMode& o) const {
        return width == o.width && height == o.height && refreshHz == o.refreshHz;
    }
    bool operator!=(const DisplayMode& o) const { return !(*this == o); }
};

/// One connected monitor and the modes it listed.
struct DisplayMonitor {
    int index = 0;             ///< the host's display index (SDL display index)
    std::string name;
    int x = 0, y = 0;          ///< top-left of this monitor on the desktop
    int width = 0, height = 0; ///< the desktop size on this monitor
    std::vector<DisplayMode> modes;
    bool Valid() const { return width > 0 && height > 0; }
};

/// A saved choice, including the previous choice while "keep these settings?" is waiting.
struct DisplaySaved {
    std::string mode;          ///< "windowed", "borderless" or "fullscreen" (empty = not saved)
    int width = 0, height = 0;
    int refreshHz = 0;
    std::string monitorName;
    int monitorIndex = -1;     ///< -1 = no monitor was chosen
    bool pending = false;      ///< a confirm was still running when this was written
    bool hasPrevious = false;
    std::string prevMode;
    int prevWidth = 0, prevHeight = 0, prevRefresh = 0;
    std::string prevMonitorName;
    int prevMonitorIndex = -1;
};

/// The choice to apply after matching a save against the monitors that are connected now.
struct DisplayPick {
    int monitorIndex = -1;
    std::string monitorName;
    WindowMode mode = WindowMode::Windowed;
    int width = 0, height = 0;
    int refreshHz = 0;
    bool monitorReplaced = false; ///< the saved monitor is not connected; another one was used
    bool modeReplaced = false;    ///< the saved size or refresh is not on that monitor
};

/// What a host passes to the window: the mode, which monitor, and the size to ask for.
struct DisplayApply {
    WindowMode mode = WindowMode::Windowed;
    int monitorIndex = -1;
    int originX = 0, originY = 0;
    int desktopWidth = 0, desktopHeight = 0;
    int width = 0, height = 0;
    int refreshHz = 0;
    bool hasSize = false;
    bool hasMonitor = false;
};

/// The "keep these settings?" clock. It only counts down; the caller reverts when Tick returns true.
struct DisplayTrial {
    bool active = false;
    float secondsLeft = 0.f;
    /// Starts the clock. A second call while it is running does not move the deadline.
    void Start(float seconds);
    void Clear();
    /// True on the frame the time runs out. The trial is then inactive.
    bool Tick(float dt);
};

inline constexpr int kDisplayMinWidth = 640;
inline constexpr int kDisplayMinHeight = 360;
inline constexpr int kDisplayMaxWidth = 16384;
inline constexpr int kDisplayMaxHeight = 16384;

/// Drop sizes outside the allowed range and refresh rates below 0 or above 1000.
/// Sort small to large, then by refresh. Exact repeats are dropped.
std::vector<DisplayMode> FilterDisplayModes(std::vector<DisplayMode> modes);

/// Unique sizes from `modes`, small to large.
std::vector<DisplayResolution> ResolutionsFromModes(const std::vector<DisplayMode>& modes);

/// Refresh rates (hertz) listed for this exact size, lowest first. A rate of 0 is left out.
std::vector<int> RefreshRatesFor(const std::vector<DisplayMode>& modes, int width, int height);

/// The listed rate closest to `want`. An empty list or `want` below 1 gives 0.
/// The same distance keeps the lower rate.
int NearestRefresh(const std::vector<int>& rates, int want);

/// The listed mode closest to this size. An exact size wins; `refreshHz` below 1 stays 0
/// (the host may pick). A positive rate that is not listed becomes the closest listed rate.
/// An empty list keeps a size that is already allowed, and returns an invalid mode otherwise.
DisplayMode NearestDisplayMode(const std::vector<DisplayMode>& modes, int width, int height, int refreshHz);

/// Which monitor a saved name or index means. `replaced` is set when a saved name is not
/// connected, or a saved index is past the end, and the first monitor is used instead.
/// No monitors: -1. Nothing saved (empty name and index below 0): -1, and `replaced` stays false.
int ChooseMonitorIndex(const std::vector<DisplayMonitor>& monitors, const std::string& name, int savedIndex, bool& replaced);

/// Match a save to the monitors connected now. A pending confirm uses the previous values.
DisplayPick ResolveDisplay(const DisplaySaved& saved, const std::vector<DisplayMonitor>& monitors);

/// The values to keep: the previous ones when a confirm was still running, otherwise `saved`.
DisplaySaved ConfirmedOrPrevious(const DisplaySaved& saved);

/// One `key=value` per line. Empty and zero fields that were never set are left out.
std::string SerializeDisplay(const DisplaySaved& saved);
/// Read that text back. Unknown lines are ignored. False only when `text` is empty of keys
/// we understand; `out` still receives whatever parsed.
bool ParseDisplay(const std::string& text, DisplaySaved& out);

} // namespace okay
