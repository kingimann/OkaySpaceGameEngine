#pragma once
// Error console data model (feature map 23.4): a ring buffer over the Log with
// repeat collapsing, filters (level, category, text), search, pause, an error-pause
// request flag, a link back to the source object or asset parsed from the message,
// and a copy-all text export. No UI here: a console window reads Rows() each frame.
//
//     ConsoleModel console;
//     console.Attach();                          // chains into Log::sink, keeps the old sink working
//     ...
//     ConsoleFilter f; f.levelMask = ConsoleModel::LevelBit(Log::Level::Error);
//     console.SetFilter(f);
//     for (const ConsoleRow& r : console.Rows()) draw(r.entry.message, r.repeat);
//     if (console.ConsumePauseRequest()) game.Pause();       // "pause on error"
//
// Log::sink carries only the level and the bare message, so the category comes from
// a leading `[tag]` in the message (see ParseCategoryPrefix) or from Add() when the
// caller knows it. Source links come from message conventions, see ParseConsoleSource.
// Thread safe: Log may call in from any thread while the UI reads.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "okay/Core/Log.hpp"

namespace okay {

/// Where a message points: an object in the scene, an asset, a script or a graph node.
struct ConsoleSource {
    enum class Kind { None, Object, Asset, Script, Graph };
    Kind kind = Kind::None;
    std::string path;          // "World/Props/Crate", "Assets/Level1.okayscene", a script or a graph
    std::string guid;          // the object's persistent id, when the caller knows it
    std::string component;     // component type involved, when there is one
    std::string nodeId;        // visual-script node id, when the message is about one node
    int line = 0;              // 0 = no line given
    int column = 0;
    bool Valid() const {
        return kind != Kind::None && (!path.empty() || !guid.empty() || !nodeId.empty());
    }
};

/// While one of these is alive, log lines on this thread pick up `source`
/// (a script error during Play names the object it ran on). Nested scopes
/// stack; the innermost wins. The editor Console reads it when a line arrives.
class ConsoleSourceScope {
public:
    explicit ConsoleSourceScope(ConsoleSource source);
    ~ConsoleSourceScope();
    ConsoleSourceScope(const ConsoleSourceScope&) = delete;
    ConsoleSourceScope& operator=(const ConsoleSourceScope&) = delete;
};
/// The source set by the innermost ConsoleSourceScope on this thread, or an empty one.
ConsoleSource ActiveConsoleSource();

/// Find a source reference in a message. Recognised, in this order:
///   `[Object: World/Props/Crate]`   (also `[GameObject: ...]`)       -> Kind::Object
///   `[Asset: Assets/crate.png]`     (also `[File: ...]`, may end in :line) -> Kind::Asset
///   `Assets/Level1.okayscene:12` or `script.okayscript:12:5` anywhere  -> Kind::Asset, with line/column
/// The tag names are case insensitive. Returns Kind::None when nothing matches.
ConsoleSource ParseConsoleSource(const std::string& message);

struct ConsoleEntry {
    std::uint64_t seq = 0;         // counts up from 0 over the model's life
    Log::Level level = Log::Level::Info;
    std::string category;          // may be empty
    std::string message;
    double timeSec = 0.0;          // seconds since the model was created
    std::uint64_t frame = 0;       // the model's frame counter when it arrived
    ConsoleSource source;
};

/// One line of the console view. With collapsing, a run of identical entries is one
/// row: `entry` is the first, `repeat` the run length and last* describe the final one.
struct ConsoleRow {
    ConsoleEntry entry;
    int repeat = 1;
    std::uint64_t lastSeq = 0;
    std::uint64_t lastFrame = 0;
    double lastTimeSec = 0.0;
};

struct ConsoleFilter {
    unsigned levelMask = 0xFu;     // OR of ConsoleModel::LevelBit; default shows everything
    std::string category;          // exact match, empty = any
    std::string text;              // substring of the message, category or source path; empty = any
    bool caseSensitive = false;
};

struct ConsoleCounts {
    std::size_t trace = 0, info = 0, warning = 0, error = 0;   // entries currently stored
};

class ConsoleModel {
public:
    explicit ConsoleModel(std::size_t capacity = 2000);
    ~ConsoleModel();
    ConsoleModel(const ConsoleModel&) = delete;
    ConsoleModel& operator=(const ConsoleModel&) = delete;

    static unsigned LevelBit(Log::Level l) { return 1u << static_cast<unsigned>(l); }

    // ---- feeding ----------------------------------------------------------
    /// Add an entry directly (when you know the category). The source is parsed from the message,
    /// then filled in from an active ConsoleSourceScope on this thread.
    void Add(Log::Level level, const std::string& message, const std::string& category = std::string());
    /// Same, with a source the caller already knows (an object guid, a script line, a graph node).
    /// Fields left empty still come from the message text and from an active ConsoleSourceScope.
    void Add(Log::Level level, const std::string& message, const std::string& category, const ConsoleSource& source);
    /// Chain into Log::sink: the previous sink keeps being called first, then each
    /// message is added here. Call from the main thread at startup, like any Log::sink
    /// assignment. False if this model is already attached.
    bool Attach();
    /// Stop receiving. If this model's hook is still the current Log::sink the previous
    /// sink is restored; if something was installed on top of it, that stays and this
    /// model's part just goes quiet. Also done by the destructor.
    void Detach();
    bool Attached() const;
    /// Treat a leading `[tag] ` in messages from the Log as the category (default on).
    /// Tags with a colon or slash (`[Object: A/B]`) are source references, not categories.
    void SetParseCategoryPrefix(bool on);

    // ---- clock ------------------------------------------------------------
    void SetFrame(std::uint64_t frame);
    void NextFrame();
    std::uint64_t Frame() const;
    /// Replace the time source (seconds); the default is a steady clock from construction.
    void SetClock(std::function<double()> clock);

    // ---- state ------------------------------------------------------------
    /// While paused new entries are dropped (and counted); nothing already stored changes.
    void SetPaused(bool paused);
    bool Paused() const;
    std::uint64_t DroppedWhilePaused() const;
    /// With this on, storing an Error raises the pause request flag.
    void SetPauseOnError(bool on);
    bool PauseOnError() const;
    bool PauseRequested() const;
    /// True once per request; the host then pauses play mode.
    bool ConsumePauseRequest();
    void Clear();
    void SetCapacity(std::size_t capacity);

    // ---- counts -----------------------------------------------------------
    std::size_t Size() const;
    std::uint64_t TotalReceived() const;            // ever stored, including those pushed out of the ring
    ConsoleCounts Counts() const;

    // ---- view -------------------------------------------------------------
    void SetFilter(const ConsoleFilter& f);
    ConsoleFilter GetFilter() const;
    /// Collapse identical consecutive entries (after filtering) into one row. Default on.
    void SetCollapse(bool on);
    bool Collapse() const;
    /// The rows to draw: current filter and collapse setting, oldest first.
    std::vector<ConsoleRow> Rows() const;
    std::vector<ConsoleRow> Rows(const ConsoleFilter& f, bool collapse) const;
    /// Every stored entry, unfiltered and uncollapsed, oldest first.
    std::vector<ConsoleEntry> Entries() const;

    // ---- search -----------------------------------------------------------
    /// Row index (in Rows()) of the next row containing `query`, searching from
    /// `startRow` (inclusive) forward or backward and wrapping around; -1 if none.
    long long Find(const std::string& query, std::size_t startRow = 0, bool forward = true,
                   bool caseSensitive = false) const;
    /// Every row index (in Rows()) that contains `query`.
    std::vector<std::size_t> FindAll(const std::string& query, bool caseSensitive = false) const;

    // ---- export -----------------------------------------------------------
    /// All rows of the current view as text, one per line:
    /// `[12.345s f60] ERROR [net] message (x3)`. With visibleOnly false: every stored
    /// entry, uncollapsed, regardless of the filter.
    std::string CopyAll(bool visibleOnly = true) const;
    static std::string FormatRow(const ConsoleRow& row);
    static const char* LevelName(Log::Level l);

    struct Impl;
private:
    std::shared_ptr<Impl> m_impl;
};

} // namespace okay
