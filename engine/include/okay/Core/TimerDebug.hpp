#pragma once
// A plain-text view of what the timers and tweens are doing, for a debug overlay or a console
// command (feature map 1.2). It only builds text and numbers from `Scheduler::Tasks()` and
// `TweenManager::Infos()`; nothing is drawn here and nothing is changed.
//
//     for (const std::string& line : TimerDebug::BuildLines(&scene.scheduler(), &scene.tweens()))
//         DrawLabel(line);
//
// With the Core debug overlay (DebugOverlay.hpp) a single line is enough:
//
//     DebugOverlay::AddLine([&] { return TimerDebug::Summary(&scene.scheduler(), &scene.tweens()); });
//
// The lines look like
//
//     Scheduler: 12 tasks (7 delays, 3 repeating, 2 tweens), 2 paused
//       next due in 0.25s (#7), 1 overdue
//       tags: wave x5, ui x3, (none) x4
//       owners: Boss x4, 0x5612a0 x2
//       oldest: #3 repeat every 0.5s [wave] running 2m05s
//     Tweens: 4 (3 playing, 1 paused), 1 done and kept
//       ids: fade x2, (none) x2
//       oldest: #2 value "fade" running 35s, loop 3/forever
//
// Either source may be null (its lines are left out). Ties are broken by name and handle, so the
// text is the same every run. Main thread only, like the things it reads.
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Tween.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace okay {

class TimerDebug {
public:
    struct Options {
        int topGroups = 4;          ///< tags, owners and ids listed (the biggest groups first)
        int topOldest = 3;          ///< longest-running tasks and tweens listed
        bool showGroups = true;     ///< the tag / owner / id lines
        bool showOldest = true;     ///< the "oldest" lines
        // Capture builds the group tables (byTag, byOwner, byId) unless showGroups is false and topGroups is 0,
        // and `oldest` holds at most topOldest entries: Summary and a BuildLines that hides them pay for neither.
    };

    struct GroupCount {
        std::string name;           ///< a tag, an object's name, "0x..." for an owner that is not an object, "(none)"
        std::size_t count = 0;
    };

    struct SchedulerStats {
        std::size_t total = 0;
        std::size_t delays = 0, repeating = 0, tweens = 0;
        std::size_t paused = 0;
        std::size_t overdue = 0;        ///< past their time without having run (paused, or on a clock that does not tick)
        std::size_t ownerGone = 0;      ///< their object or `alive` says they are dead; dropped at the next update
        std::size_t onClock = 0;        ///< follow a named clock channel
        std::size_t unscaled = 0;
        /// Seconds until the soonest task that is waiting (not paused, owner alive, not "never") fires; -1 = none.
        float nextDue = -1.0f;
        Scheduler::Handle nextHandle = Scheduler::InvalidHandle;
        std::vector<GroupCount> byTag;      ///< biggest first; untagged tasks are "(none)"
        std::vector<GroupCount> byOwner;    ///< biggest first; tasks with no owner are "(none)"
        std::vector<Scheduler::TaskInfo> oldest;   ///< longest running first (at most Options::topOldest)
    };

    struct TweenStats {
        std::size_t total = 0, playing = 0, paused = 0;
        std::size_t sequences = 0, nested = 0;   ///< sequences, and the tweens and intervals inside them
        std::size_t endless = 0;                 ///< loop for ever
        std::size_t doneKept = 0;                ///< finished but not removed (auto kill off): they stay until killed
        std::size_t onClock = 0, unscaled = 0, withTarget = 0;
        std::vector<GroupCount> byId;            ///< biggest first; tweens with no id are "(none)"
        std::vector<GroupCount> byTag;
        std::vector<TweenManager::TweenInfo> oldest;
    };

    static SchedulerStats Capture(const Scheduler& scheduler);
    static SchedulerStats Capture(const Scheduler& scheduler, const Options& options);
    static TweenStats Capture(const TweenManager& tweens);
    static TweenStats Capture(const TweenManager& tweens, const Options& options);

    /// The multi-line text above, top to bottom.
    static std::vector<std::string> BuildLines(const Scheduler* scheduler, const TweenManager* tweens);
    static std::vector<std::string> BuildLines(const Scheduler* scheduler, const TweenManager* tweens,
                                               const Options& options);
    /// One line for a HUD corner: `Timers: 12 (2 paused, next 0.25s) | Tweens: 4 (3 playing)`.
    static std::string Summary(const Scheduler* scheduler, const TweenManager* tweens);

    /// "0.25s", "35s", "2m05s", "1h02m", "never" (infinity), "0s" (NaN or negative).
    static std::string FormatSeconds(double seconds);
};

} // namespace okay
