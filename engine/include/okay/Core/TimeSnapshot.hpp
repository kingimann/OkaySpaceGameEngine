#pragma once
// The loop's own state, for a save game: the game clocks (ClockSet: scale, pause, elapsed time and a
// ramp in progress), the gameplay time scale, and the state of random streams (Random::Shared() and any
// generator you name), written as one block of a save document and put back on load.
//
//     // saving (SaveFramework.hpp: a SaveDocument holds named blocks)
//     SaveDocument doc = SaveSystem::CaptureScene(scene);
//     TimeSnapshot::Capture().Write(doc.Writer("time"));
//     TimeSnapshot::Capture().AddRandom("loot", lootRng).Write(doc.Writer("time"));   // with a stream of yours
//
//     // loading
//     TimeSnapshot snap;
//     if (TimeSnapshot::Read(doc.Reader("time"), snap)) { snap.Apply(); snap.ApplyRandom("loot", lootRng); }
//
// Why it matters: after a load the world's own clock ("day length", an event schedule) is where the
// save left it, an effect that slowed a clock is still slowing it, and a loot roll after the load is the
// roll the player would have got, not a fresh one (so reloading cannot be used to re-roll a chest that
// was already opened, and a bug report's save plays the same).
//
// What it does NOT touch: Time::ElapsedTime(), UnscaledTime() and FrameCount() are the process's own
// clocks and keep running forward (code that stamps them, such as SaveSystem::PlayTime or a Stopwatch on
// the game clock, would see time go backwards); play time has its own field (SaveSystem::SetPlayTime).
// Pause reasons, hit stops, the quit request and GameLoop's debug scale are session state and are never
// saved. A built-in clock keeps its source and "holds while paused" setting (they are the code's); only
// a clock the snapshot has and the set does not is created, with the saved settings.
//
// Format: keys of the block, all optional on read: `v` (1), `timeScale`, `clocks.<i>.*`, `randoms.<i>.*`.
// Doubles are written as text with 17 digits (exact), generator states as signed 64-bit integers.
// Reading is defensive: counts are capped, bad numbers are cleaned, and an older block that lacks a
// field leaves that part alone. Main thread only.
#include "okay/Core/Clock.hpp"
#include "okay/Core/Random.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay {

class SaveWriter;   // Core/SaveFramework.hpp
class SaveReader;

struct ClockSnapshot {
    std::string name;
    Clock::Source source = Clock::Source::Unscaled;
    bool holdsWhileGamePaused = false;
    bool paused = false;
    float scale = 1.0f;
    double elapsed = 0.0;
    std::uint64_t ticks = 0;
    bool ramping = false;        ///< a RampScale was running
    float rampTarget = 1.0f;
    float rampSecondsLeft = 0.0f;
};

struct TimeSnapshot {
    static constexpr int kVersion = 1;
    static constexpr int kMaxClocks = 1000;     ///< what Read accepts
    static constexpr int kMaxRandoms = 1000;

    /// What Apply puts back.
    struct ApplyOptions {
        bool clocks = true;
        bool timeScale = true;
        bool random = true;       ///< Random::Shared()
    };

    float timeScale = 1.0f;                     ///< Time::BaseTimeScale() (the scale you set; not the pause)
    /// False when the block Read came from did not hold a time scale: Apply then leaves the gameplay time
    /// scale alone (it used to set it to 1, against the rule that a field an older block lacks is left as it is).
    bool hasTimeScale = true;
    std::vector<ClockSnapshot> clocks;
    /// Named generator states. "shared" is Random::Shared().
    std::map<std::string, Random::State> randoms;

    /// The set's clocks, Time::BaseTimeScale() and, with `sharedRandom`, Random::Shared().
    static TimeSnapshot Capture(const ClockSet& set = ClockSet::Global(), bool sharedRandom = true);
    /// Add a stream of your own (a loot generator, a world generator) under `name`.
    TimeSnapshot& AddRandom(const std::string& name, const Random& rng) {
        randoms[name] = rng.GetState();
        return *this;
    }
    /// Put the state saved under `name` into `rng`; false (and `rng` untouched) when there is none.
    bool ApplyRandom(const std::string& name, Random& rng) const;

    /// Restore the clocks, the time scale and Random::Shared() (what `options` allow).
    void Apply() const { Apply(ClockSet::Global(), ApplyOptions()); }
    void Apply(ClockSet& set, const ApplyOptions& options) const;

    /// Write into a block of a save document (see the top of the file).
    void Write(SaveWriter w) const;
    /// Read what Write wrote. False, with `out` untouched, when the block holds no snapshot.
    static bool Read(const SaveReader& r, TimeSnapshot& out);

    bool operator==(const TimeSnapshot& o) const;
    bool operator!=(const TimeSnapshot& o) const { return !(*this == o); }
};

}  // namespace okay
