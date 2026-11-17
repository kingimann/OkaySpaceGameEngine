#pragma once
// Saving and loading rules for the editor, as plain functions and small state machines that need no window, no
// ImGui and no real clock: tests drive them with a fake clock and a fake file system.
//
// What is here (all of it is "policy"; the editor draws it and calls it):
//   - WriteUserFile       one atomic write for everything the editor saves (old file whole or new file whole)
//   - DiskWatch           "did this file change on disk while it was open?" (git pull, another editor, Notepad)
//   - ExternalPrompt      the words for that question
//   - DocState            the unsaved / saved / failed state of one document and its "Saved 14:03" text
//   - UnsavedSummary      "3 things have unsaved changes: Level1, Player prefab, enemy.os"
//   - AutosavePolicy      when a recovery copy may be written (not in Play, rate limited, not mid-drag)
//   - SummarizeRecovery   what the "editor closed unexpectedly" box says about a recovery copy
//   - FormatLoadError     a scene loader error in plain words, with the line and the object
//   - CleanRecent         the Open Recent list: drop missing files, keep pinned ones first
//   - CopyName            a free file name for Save a Copy / Duplicate Scene
//
// Written from the editor's own needs (no outside code). Headers only; no <mutex>, <thread> or
// <condition_variable> (the Windows build rule), so the launcher, editor and player can all reach it.
#include "okay/Core/SaveData.hpp"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::savesafe {

// ---------------------------------------------------------------------------------------------------------------
// 1. One atomic write
// ---------------------------------------------------------------------------------------------------------------

/// Replace `path` with `text` all at once: write a temp file beside it, flush it to the disk, swap it in. If anything
/// fails (disk full, locked, the editor killed half way) the old file is untouched. `error` says why in plain words.
/// `sync` flushes to the disk before the swap (a power cut keeps the file); on by default for things a person saved.
inline bool WriteUserFile(const std::string& path, const std::string& text, std::string* error = nullptr, bool sync = true) {
    savefmt::WriteOptions opt;
    opt.sync = sync;
    return savefmt::AtomicWriteChecked(path, text, error, opt);
}

inline bool ReadWholeFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// 2. Did the file change on disk?
// ---------------------------------------------------------------------------------------------------------------

struct FileStamp {
    bool exists = false;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;     ///< last write time in file-clock ticks (only compared, never shown)
    std::uint64_t hash = 0;     ///< FNV-1a of the content (only when read with `withHash`)
    bool hashed = false;
};

/// Looks at a file. `withHash` reads it whole (do that on load and save, not every frame).
using StampProbe = std::function<FileStamp(const std::string& path, bool withHash)>;

inline FileStamp RealProbe(const std::string& path, bool withHash) {
    namespace fs = std::filesystem;
    FileStamp s;
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec) || ec) return s;
    s.exists = true;
    s.size = (std::uint64_t)fs::file_size(path, ec);
    const auto t = fs::last_write_time(path, ec);
    if (!ec) s.mtime = (std::int64_t)t.time_since_epoch().count();
    if (withHash) {
        std::string text;
        if (ReadWholeFile(path, text)) { s.hash = savefmt::Fnv1a(text.data(), text.size()); s.hashed = true; s.size = text.size(); }
    }
    return s;
}

enum class DiskChange {
    None,       ///< same as when we last read or wrote it
    Modified,   ///< somebody else wrote it
    Deleted,    ///< gone (moved, deleted, or its folder was)
    Appeared,   ///< we had no file and now there is one
};

/// Remembers what one file looked like when the editor last read or wrote it, and says when that changes.
/// Check() is cheap enough to call about once a second: it only looks at the size and write time, and reads the file
/// only when those disagree in a way that could still be a no-op (same size, new time: a "touch").
class DiskWatch {
public:
    explicit DiskWatch(StampProbe probe = RealProbe) : m_probe(std::move(probe)) {}

    /// Call right after reading or writing the file: this is now "the editor's version".
    void Remember(const std::string& path) {
        m_path = path;
        m_known = m_probe(path, true);
        m_watching = !path.empty();
    }
    void Forget() { m_path.clear(); m_known = FileStamp{}; m_watching = false; }
    bool Watching() const { return m_watching; }
    const std::string& Path() const { return m_path; }

    DiskChange Check() {
        if (!m_watching) return DiskChange::None;
        const FileStamp now = m_probe(m_path, false);
        if (!now.exists) return m_known.exists ? DiskChange::Deleted : DiskChange::None;
        if (!m_known.exists) return DiskChange::Appeared;
        if (now.size != m_known.size) return DiskChange::Modified;
        if (now.mtime == m_known.mtime) return DiskChange::None;
        // Same size, new time. A real edit or only a touch (a backup tool, git checking out the same text)? Read it.
        const FileStamp full = m_probe(m_path, true);
        if (full.hashed && m_known.hashed && full.hash == m_known.hash) { m_known = full; return DiskChange::None; }
        return DiskChange::Modified;
    }

    /// The user looked at the outside change and chose to keep going ("Keep mine", "Overwrite"): stop warning about
    /// this version of the file. A later change warns again.
    void AcceptDisk() { if (m_watching) m_known = m_probe(m_path, true); }

private:
    StampProbe m_probe;
    std::string m_path;
    FileStamp m_known;
    bool m_watching = false;
};

struct ExternalPrompt {
    std::string title;
    std::string body;
    std::vector<std::string> buttons;   ///< in order; the first is the safe default
};

/// The question to ask when a file changed outside the editor. `atSave` = the user just pressed Save;
/// otherwise the editor noticed on its own. `unsavedEdits` = the editor has changes of its own in it.
inline ExternalPrompt BuildExternalPrompt(const std::string& fileName, DiskChange change, bool unsavedEdits, bool atSave) {
    ExternalPrompt p;
    const std::string q = "\"" + fileName + "\"";
    if (change == DiskChange::Modified) {
        p.title = "This file changed outside the editor";
        if (atSave) {
            p.body = q + " was changed on disk after you opened it (a git pull, another editor or a text editor?). "
                     "If you overwrite it, those outside changes are lost. If you reload, your unsaved edits here are lost. "
                     "Save a Copy keeps both.";
            p.buttons = {"Save a Copy", "Overwrite", "Reload", "Cancel"};
        } else if (unsavedEdits) {
            p.body = q + " was changed on disk, and you also have unsaved edits here. "
                     "Reloading throws your edits away. Keep Mine leaves the file alone until you save. "
                     "Save a Copy writes your edits to a new file first.";
            p.buttons = {"Keep Mine", "Save a Copy", "Reload"};
        } else {
            p.body = q + " was changed on disk (a git pull or another editor). You have no edits here, so it is safe to load the new version.";
            p.buttons = {"Reload", "Keep Mine"};
        }
    } else if (change == DiskChange::Deleted) {
        p.title = "This file is gone from the disk";
        p.body = q + " was deleted or moved while it was open. " +
                 (atSave ? "Save writes it again at the same place, or Save As puts it somewhere else."
                         : "What is open here is still safe. Save writes it again, or Save As puts it somewhere else.");
        p.buttons = atSave ? std::vector<std::string>{"Save Here", "Save As...", "Cancel"} : std::vector<std::string>{"OK"};
    } else if (change == DiskChange::Appeared) {
        p.title = "A file now exists at this name";
        p.body = q + " did not exist when this scene was started, but there is one now. Saving would replace it.";
        p.buttons = {"Save a Copy", "Overwrite", "Cancel"};
    }
    return p;
}

// ---------------------------------------------------------------------------------------------------------------
// 3. Unsaved / saved state of one document
// ---------------------------------------------------------------------------------------------------------------

/// "14:03" for a unix time, given the local offset from UTC in seconds.
inline std::string FormatClock(std::int64_t unixSeconds, int utcOffsetSeconds) {
    std::int64_t t = unixSeconds + utcOffsetSeconds;
    std::int64_t day = ((t % 86400) + 86400) % 86400;
    char b[16];
    std::snprintf(b, sizeof(b), "%02d:%02d", (int)(day / 3600), (int)((day % 3600) / 60));
    return b;
}

/// The local offset from UTC at that moment (seconds east of UTC).
inline int LocalUtcOffsetSeconds(std::int64_t unixSeconds) {
    const std::time_t t = (std::time_t)unixSeconds;
    std::tm local{}, utc{};
#if defined(_WIN32)
    localtime_s(&local, &t);
    gmtime_s(&utc, &t);
#else
    localtime_r(&t, &local);
    gmtime_r(&t, &utc);
#endif
    const int dayDiff = local.tm_yday - utc.tm_yday;
    int day = 0;
    if (local.tm_year != utc.tm_year) day = local.tm_year > utc.tm_year ? 1 : -1;
    else day = dayDiff;
    return day * 86400 + (local.tm_hour - utc.tm_hour) * 3600 + (local.tm_min - utc.tm_min) * 60 + (local.tm_sec - utc.tm_sec);
}

/// The state of one open document: scene, prefab, script, graph. Edited() raises the mark, Saved() clears it and
/// remembers when, SaveFailed() keeps the mark and the reason. Undoing back to the saved text clears the mark too.
class DocState {
public:
    enum class Kind { New, Clean, Dirty, SaveFailed };

    void Loaded() { m_dirty = false; m_failed.clear(); m_savedAt = 0; m_everSaved = false; m_loaded = true; }
    void Edited() { m_dirty = true; m_loaded = true; }
    void Saved(std::int64_t now) { m_dirty = false; m_failed.clear(); m_savedAt = now; m_everSaved = true; m_loaded = true; }
    void SaveFailed(const std::string& why) { m_failed = why.empty() ? "unknown error" : why; m_dirty = true; }
    /// Undo or Redo landed on exactly the saved text.
    void ReachedSavePoint() { m_dirty = false; m_failed.clear(); }
    /// Reverted to the file on disk.
    void Reverted() { m_dirty = false; m_failed.clear(); }

    bool Dirty() const { return m_dirty; }
    bool EverSaved() const { return m_everSaved; }
    std::int64_t SavedAt() const { return m_savedAt; }
    const std::string& FailureReason() const { return m_failed; }
    Kind State() const {
        if (!m_failed.empty()) return Kind::SaveFailed;
        if (m_dirty) return Kind::Dirty;
        return m_everSaved || m_loaded ? Kind::Clean : Kind::New;
    }
    /// The status line: "Saved 14:03", "Unsaved changes", "Save failed: the disk is full", or "".
    std::string StatusText(int utcOffsetSeconds) const {
        switch (State()) {
            case Kind::SaveFailed: return "Save failed: " + m_failed;
            case Kind::Dirty: return m_everSaved ? "Unsaved changes (last saved " + FormatClock(m_savedAt, utcOffsetSeconds) + ")" : "Unsaved changes";
            case Kind::Clean: return m_everSaved ? "Saved " + FormatClock(m_savedAt, utcOffsetSeconds) : "No changes";
            default: return "";
        }
    }

private:
    bool m_dirty = false, m_everSaved = false, m_loaded = false;
    std::int64_t m_savedAt = 0;
    std::string m_failed;
};

/// A tab or title label with its unsaved mark: "Level1 *".
inline std::string MarkedName(const std::string& name, bool unsaved) { return unsaved ? name + " *" : name; }

// ---------------------------------------------------------------------------------------------------------------
// 4. The list of what is unsaved
// ---------------------------------------------------------------------------------------------------------------

struct UnsavedItem {
    std::string kind;   ///< "scene", "prefab", "script", "graph", "asset", "settings"
    std::string path;   ///< file path (may be empty for a document that was never saved)
    std::string title;  ///< what the user calls it when there is no path ("Untitled")
};

inline std::string FileLeaf(const std::string& path) {
    const std::size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}
inline std::string FileStem(const std::string& path) {
    const std::string leaf = FileLeaf(path);
    const std::size_t d = leaf.find_last_of('.');
    return d == std::string::npos || d == 0 ? leaf : leaf.substr(0, d);
}

/// How one unsaved item is named in the list: scenes by their name ("Level1"), prefabs with the word ("Player prefab"),
/// scripts and graphs by their file name ("enemy.os").
inline std::string DisplayName(const UnsavedItem& it) {
    const std::string stem = it.path.empty() ? it.title : FileStem(it.path);
    if (it.kind == "scene") return stem.empty() ? "this new scene" : stem;
    if (it.kind == "prefab") return (stem.empty() ? std::string("Prefab") : stem) + " prefab";
    const std::string leaf = it.path.empty() ? (it.title.empty() ? std::string("Untitled") : it.title) : FileLeaf(it.path);
    return leaf;
}

struct UnsavedSummary {
    std::string headline;               ///< "3 things have unsaved changes:"
    std::vector<std::string> names;     ///< one per item, in the order given
    std::string oneLine;                ///< "3 things have unsaved changes: Level1, Player prefab, enemy.os"
    std::string saveAllLabel;           ///< "Save All (3)"
};

inline UnsavedSummary BuildUnsavedSummary(const std::vector<UnsavedItem>& items) {
    UnsavedSummary s;
    std::set<std::string> seen;
    for (const UnsavedItem& it : items) {
        std::string n = DisplayName(it);
        if (!seen.insert(it.kind + "|" + (it.path.empty() ? n : it.path)).second) continue;   // the same file twice is one item
        s.names.push_back(std::move(n));
    }
    const std::size_t n = s.names.size();
    s.headline = n == 0 ? "Nothing has unsaved changes." : n == 1 ? "1 thing has unsaved changes:" : std::to_string(n) + " things have unsaved changes:";
    s.oneLine = s.headline;
    for (std::size_t i = 0; i < n; ++i) s.oneLine += (i ? ", " : " ") + s.names[i];
    s.saveAllLabel = n > 1 ? "Save All (" + std::to_string(n) + ")" : "Save";
    return s;
}

/// What Save All did, for the Console and the status bar: "Saved 3 things: Level1, Player prefab, enemy.os" or what failed.
struct SaveAllReport {
    std::vector<std::string> saved;
    std::vector<std::pair<std::string, std::string>> failed;   ///< (name, reason)
    std::vector<std::string> skipped;                          ///< (name: reason it was left alone)
    bool Ok() const { return failed.empty(); }
    std::string Text() const {
        std::string t;
        if (saved.empty() && failed.empty()) t = "Nothing needed saving.";
        else if (!saved.empty()) {
            t = "Saved " + std::to_string(saved.size()) + (saved.size() == 1 ? " thing: " : " things: ");
            for (std::size_t i = 0; i < saved.size(); ++i) t += (i ? ", " : "") + saved[i];
            t += ".";
        }
        for (const auto& f : failed) t += " Could not save " + f.first + ": " + f.second + ".";
        for (const auto& s : skipped) t += " Left alone: " + s + ".";
        return t;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// 5. Autosave rules
// ---------------------------------------------------------------------------------------------------------------

struct AutosaveSettings {
    bool enabled = true;
    float intervalSeconds = 30.0f;     ///< between recovery copies
    int keep = 3;                      ///< how many recovery copies are kept (rotating files)
    float minGapSeconds = 5.0f;        ///< never closer together than this, whatever asks (focus lost, a timer)
    float maxDeferSeconds = 15.0f;     ///< a busy moment (dragging, typing) may postpone a copy this long at most
    static float ClampInterval(float s) { return s < 5.0f ? 5.0f : (s > 3600.0f ? 3600.0f : s); }
    static int ClampKeep(int k) { return k < 1 ? 1 : (k > 20 ? 20 : k); }
    void Clamp() { intervalSeconds = ClampInterval(intervalSeconds); keep = ClampKeep(keep); if (minGapSeconds < 1.0f) minGapSeconds = 1.0f; }
};

struct AutosaveContext {
    bool anythingUnsaved = false;
    bool playing = false;              ///< the scene is not the user's during Play
    bool inPrefabMode = false;
    bool userBusy = false;             ///< mouse button held, or typing in a field: a hitch would be felt
    float secondsSinceLast = 1e9f;     ///< since the last recovery copy
    float secondsSinceRequested = 0;   ///< how long this copy has been due (for maxDeferSeconds)
    bool urgent = false;               ///< the window lost focus: the user may be about to leave
};

enum class AutosaveVerdict { Go, Disabled, Playing, NothingToSave, TooSoon, NotDueYet, Busy };

inline AutosaveVerdict DecideAutosave(const AutosaveSettings& s, const AutosaveContext& c) {
    if (!s.enabled) return AutosaveVerdict::Disabled;
    if (c.playing) return AutosaveVerdict::Playing;
    if (!c.anythingUnsaved) return AutosaveVerdict::NothingToSave;
    if (c.secondsSinceLast < s.minGapSeconds) return AutosaveVerdict::TooSoon;
    if (!c.urgent && c.secondsSinceLast < s.intervalSeconds) return AutosaveVerdict::NotDueYet;
    if (c.userBusy && !c.urgent && c.secondsSinceRequested < s.maxDeferSeconds) return AutosaveVerdict::Busy;
    return AutosaveVerdict::Go;
}

inline const char* AutosaveVerdictText(AutosaveVerdict v) {
    switch (v) {
        case AutosaveVerdict::Go: return "writing a recovery copy";
        case AutosaveVerdict::Disabled: return "autosave is off";
        case AutosaveVerdict::Playing: return "not while the game is playing";
        case AutosaveVerdict::NothingToSave: return "nothing has changed";
        case AutosaveVerdict::TooSoon: return "one was just written";
        case AutosaveVerdict::NotDueYet: return "not due yet";
        case AutosaveVerdict::Busy: return "waiting for a quiet moment";
    }
    return "";
}

/// Keeps a running cost of the recovery copies so the editor can show what autosave costs and slow itself down when
/// it is expensive: the next interval is never shorter than `100 x cost` (autosave stays under 1 percent of the time).
class AutosaveCost {
public:
    void Add(double milliseconds) {
        ++m_count;
        m_last = milliseconds;
        m_total += milliseconds;
        if (milliseconds > m_worst) m_worst = milliseconds;
    }
    double LastMs() const { return m_last; }
    double WorstMs() const { return m_worst; }
    double AverageMs() const { return m_count ? m_total / (double)m_count : 0.0; }
    int Count() const { return m_count; }
    float SuggestedInterval(float wanted) const {
        const float floorSec = (float)(m_last * 100.0 / 1000.0);
        return wanted > floorSec ? wanted : floorSec;
    }
private:
    int m_count = 0;
    double m_last = 0, m_total = 0, m_worst = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// 6. What the recovery box says
// ---------------------------------------------------------------------------------------------------------------

/// "just now", "3 minutes ago", "2 hours ago", "yesterday", "5 days ago".
inline std::string AgeText(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    auto plural = [](std::int64_t n, const char* w) { return std::to_string((long long)n) + " " + w + (n == 1 ? "" : "s") + " ago"; };
    if (seconds < 45) return "just now";
    if (seconds < 90) return "a minute ago";
    if (seconds < 3600) return plural((seconds + 30) / 60, "minute");
    if (seconds < 86400) return plural((seconds + 1800) / 3600, "hour");
    if (seconds < 2 * 86400) return "yesterday";
    return plural(seconds / 86400, "day");
}

/// Number of objects in a scene text (its `gameobject` records), or -1 when it is not scene text.
inline int CountSceneObjects(const std::string& text) {
    if (text.compare(0, 9, "okayscene") != 0) return -1;
    int n = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 11, "gameobject ") == 0) ++n;
        const std::size_t e = text.find('\n', i);
        if (e == std::string::npos) break;
        i = e + 1;
    }
    return n;
}

struct RecoveredDoc {
    std::string kind;   ///< "scene", "script", "graph", "closed"
    std::string key;    ///< path, or "untitled-scene" and so on
    std::string text;
};

struct RecoveryLine {
    std::string name;       ///< "Level1"
    std::string detail;     ///< "42 objects (the saved file has 40), changed since the last save"
    bool sameAsDisk = false;
    bool hasFile = false;
};

struct RecoverySummary {
    std::string headline;               ///< "The editor closed unexpectedly. Restore your last autosave?"
    std::string when;                   ///< "Autosaved 3 minutes ago (14:03)"
    std::vector<RecoveryLine> lines;
    bool worthRestoring = false;        ///< at least one document differs from its file
    int sceneObjects = -1;              ///< objects in the recovered scene, -1 when there is none
};

/// `readDisk(path, out)` gives the file as it is now (false when there is none): a fake in tests.
inline RecoverySummary SummarizeRecovery(const std::vector<RecoveredDoc>& docs, std::int64_t writtenAt, std::int64_t now, int utcOffsetSeconds,
                                         const std::function<bool(const std::string&, std::string&)>& readDisk) {
    RecoverySummary r;
    r.headline = "The editor closed unexpectedly. Restore your last autosave?";
    r.when = "Autosaved " + AgeText(now - writtenAt) + " (" + FormatClock(writtenAt, utcOffsetSeconds) + ")";
    for (const RecoveredDoc& d : docs) {
        RecoveryLine l;
        const bool untitled = d.key.rfind("untitled", 0) == 0 || d.key.empty();
        std::string disk;
        l.hasFile = !untitled && readDisk && readDisk(d.key, disk);
        l.sameAsDisk = l.hasFile && disk == d.text;
        const std::string leaf = untitled ? (d.kind == "scene" ? "a new scene that was never saved" : "an untitled " + d.kind) : FileLeaf(d.key);
        l.name = d.kind == "scene" ? "Scene: " + leaf : d.kind == "script" ? "Script: " + leaf : d.kind == "graph" ? "Graph: " + leaf : d.kind == "closed" ? "Closed tab: " + leaf : leaf;
        if (d.kind == "scene") {
            const int n = CountSceneObjects(d.text);
            r.sceneObjects = n;
            std::string detail = n >= 0 ? std::to_string(n) + (n == 1 ? " object" : " objects") : std::string("unreadable scene text");
            if (l.hasFile) {
                const int m = CountSceneObjects(disk);
                if (m >= 0 && n >= 0) {
                    detail += " (the saved file has " + std::to_string(m);
                    if (n != m) detail += n > m ? ", " + std::to_string(n - m) + " more here" : ", " + std::to_string(m - n) + " fewer here";
                    detail += ")";
                }
            }
            l.detail = detail;
        } else {
            l.detail = std::to_string(d.text.size()) + " characters";
        }
        if (l.sameAsDisk) l.detail += "; identical to the saved file";
        else if (!l.hasFile) l.detail += untitled ? "; never saved" : "; the file is missing";
        else l.detail += "; changed since the last save";
        if (!l.sameAsDisk && !d.text.empty()) r.worthRestoring = true;
        r.lines.push_back(std::move(l));
    }
    return r;
}

/// Where "Open both" puts the recovered scene so the file on disk is not touched: next to it, with "(recovered)".
inline std::string RecoveredCopyPath(const std::string& scenePath, const std::function<bool(const std::string&)>& exists) {
    namespace fs = std::filesystem;
    const fs::path p(scenePath.empty() ? std::string("Recovered.okayscene") : scenePath);
    const std::string stem = p.stem().string(), ext = p.extension().string().empty() ? ".okayscene" : p.extension().string();
    for (int k = 0; k < 1000; ++k) {
        const std::string name = stem + " (recovered" + (k ? " " + std::to_string(k + 1) : std::string()) + ")" + ext;
        const std::string full = (p.parent_path() / name).string();
        if (!exists(full)) return full;
    }
    return (p.parent_path() / (stem + " (recovered)" + ext)).string();
}

// ---------------------------------------------------------------------------------------------------------------
// 7. Load errors in plain words
// ---------------------------------------------------------------------------------------------------------------

struct LoadErrorText {
    std::string headline;     ///< what went wrong, one sentence
    std::string where;        ///< "line 412, in or after the object \"Door\"" (empty when unknown)
    std::string object;       ///< "Door"
    int line = 0;
    std::string advice;       ///< what to do next
    std::string Full() const {
        std::string t = headline;
        if (!where.empty()) t += " Look at " + where + ".";
        if (!advice.empty()) t += " " + advice;
        return t;
    }
};

/// Turns the loader's message ("meshgeo: bad vertex count (line 412, in or after the object \"Door\")") into words a
/// person can act on. `backupExists` = a `.bak` copy of the last good save is next to the file.
inline LoadErrorText FormatLoadError(const std::string& raw, const std::string& fileName, bool backupExists) {
    LoadErrorText t;
    std::string msg = raw;
    // Pull the "(line N, in or after the object "Name")" tail off.
    const std::size_t lp = msg.rfind(" (line ");
    if (lp != std::string::npos && msg.back() == ')') {
        const std::string tail = msg.substr(lp + 7, msg.size() - lp - 8);   // N, in or after the object "Name"
        t.line = std::atoi(tail.c_str());
        const std::size_t q1 = tail.find('"');
        const std::size_t q2 = tail.rfind('"');
        if (q1 != std::string::npos && q2 > q1) t.object = tail.substr(q1 + 1, q2 - q1 - 1);
        t.where = "line " + std::to_string(t.line);
        if (!t.object.empty()) t.where += ", in or after the object \"" + t.object + "\"";
        msg.erase(lp);
    }
    const std::string q = "\"" + fileName + "\"";
    auto has = [&](const char* s) { return msg.find(s) != std::string::npos; };
    if (msg.rfind("cannot open", 0) == 0 || has("can't open"))
        t.headline = "Couldn't open " + q + ": the file is missing, in use by another program, or you have no permission to read it.";
    else if (has("tampered or unreadable"))
        t.headline = q + " looks damaged: it could not be decoded, or it was changed by something other than the editor.";
    else if (has("missing 'okayscene' header"))
        t.headline = q + " is not an OkaySpace scene file: it does not start with the scene header. It may be a different kind of file, or empty.";
    else if (has("bad 'okayscene' version"))
        t.headline = q + " has a damaged scene header (the format number is wrong).";
    else if (has("duplicate gameobject index"))
        t.headline = q + " lists two objects under the same number, so it can't be read safely (usually a bad merge of two copies of the file).";
    else if (has("parent cycle"))
        t.headline = q + " has objects that are each other's parents, which can't be loaded (" + msg + ").";
    else if (has("truncated gameobject header") || has("record cut short") || has("bad count") || has("damaged"))
        t.headline = q + " is cut short or damaged at one record (" + msg + "). This often means the file was only partly written.";
    else if (has("out of range"))
        t.headline = q + " has a record that points outside its own data (" + msg + ").";
    else
        t.headline = "Couldn't read " + q + ": " + msg + ".";
    t.advice = "The scene you had open is unchanged and nothing was written.";
    if (backupExists) t.advice += " A copy of the last good save is next to the file (" + fileName + ".bak): open that one instead.";
    return t;
}

// ---------------------------------------------------------------------------------------------------------------
// 8. Open Recent
// ---------------------------------------------------------------------------------------------------------------

struct RecentResult {
    std::vector<std::string> list;      ///< pinned first (in pin order), then the rest, newest first
    std::vector<std::string> dropped;   ///< missing files that were removed
};

/// Drop the files that are gone, move pinned ones to the top, cap the length. A pinned file that is missing is also
/// dropped from the list (the pin is kept by the caller until the user unpins it, so a file on an unplugged drive
/// comes back with the drive).
inline RecentResult CleanRecent(const std::vector<std::string>& recent, const std::vector<std::string>& pinned,
                                const std::function<bool(const std::string&)>& exists, std::size_t maxCount = 10) {
    RecentResult r;
    std::set<std::string> seen;
    auto take = [&](const std::string& p, std::vector<std::string>& into) {
        if (p.empty() || !seen.insert(p).second) return;
        if (exists && !exists(p)) { r.dropped.push_back(p); return; }
        into.push_back(p);
    };
    std::vector<std::string> pins, rest;
    const std::set<std::string> pinSet(pinned.begin(), pinned.end());
    for (const std::string& p : pinned) take(p, pins);
    for (const std::string& p : recent) { if (pinSet.count(p)) continue; take(p, rest); }
    r.list = pins;
    for (const std::string& p : rest) { if (r.list.size() >= std::max(maxCount, pins.size())) break; r.list.push_back(p); }
    return r;
}

inline bool TogglePin(std::vector<std::string>& pinned, const std::string& path) {
    auto it = std::find(pinned.begin(), pinned.end(), path);
    if (it != pinned.end()) { pinned.erase(it); return false; }
    pinned.push_back(path);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// 9. Names for copies
// ---------------------------------------------------------------------------------------------------------------

/// "Level1.okayscene" -> "Level1 copy.okayscene", then "Level1 copy 2.okayscene" ... the first that is free.
inline std::string CopyName(const std::string& path, const std::function<bool(const std::string&)>& exists) {
    namespace fs = std::filesystem;
    const fs::path p(path);
    std::string stem = p.stem().string();
    const std::string ext = p.extension().string();
    // "Level1 copy 3" copies as "Level1 copy 4", not "Level1 copy 3 copy".
    const std::size_t c = stem.rfind(" copy");
    if (c != std::string::npos) {
        const std::string tail = stem.substr(c + 5);
        if (tail.empty() || (tail[0] == ' ' && tail.size() > 1 && std::all_of(tail.begin() + 1, tail.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })))
            stem.erase(c);
    }
    for (int k = 1; k < 10000; ++k) {
        const std::string name = stem + (k == 1 ? " copy" : " copy " + std::to_string(k)) + ext;
        const std::string full = (p.parent_path() / name).string();
        if (!exists(full)) return full;
    }
    return (p.parent_path() / (stem + " copy" + ext)).string();
}

/// A user typed or picked a name: it must end in `ext`, and a name that is already taken needs a yes before it is used.
inline std::string WithExtension(std::string path, const std::string& ext) {
    const std::size_t n = ext.size();
    if (path.size() < n || path.compare(path.size() - n, n, ext) != 0) path += ext;
    return path;
}

}  // namespace okay::editor::savesafe
