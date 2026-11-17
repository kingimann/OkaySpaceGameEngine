#pragma once
#include <string>
#include <vector>

namespace okay {

/// What the engine saw the player do, as short lines, for the editor's Input panel and for bug
/// reports. While enabled it notes every key, mouse button, controller button and action that
/// changed since the last frame, and which device was used last. Off by default: nothing is
/// stored and Tick pays one branch.
class InputDebug {
public:
    static void Enable(bool on);
    static bool Enabled();
    /// The latest events, newest last (at most Capacity()). Cleared by Clear().
    static const std::vector<std::string>& Events();
    static void Clear();
    static constexpr std::size_t Capacity() { return 24; }
    /// Called by InputActions::Tick after sampling.
    static void Record();
    /// Lines describing the state right now: keys held, mouse, wheel, each connected controller,
    /// each action that is not zero. For the panel and for text dumps.
    static std::vector<std::string> Snapshot();
};

} // namespace okay
