#pragma once
// A small "did anything change under this folder" interface, built on plain operating-system
// calls (Linux inotify, Windows ReadDirectoryChangesW). The Project panel uses it to skip its
// periodic scan when nothing happened. Where no native watcher exists (macOS for now, or the
// system refuses, e.g. too many watches) MakeFileWatcher() returns a polling watcher whose
// answer is always "unknown", so the caller keeps scanning on its timer exactly as before.
// See docs/asset_database.md ("File watcher").
#include <memory>
#include <string>
#include <vector>

namespace okay {

enum class WatchState {
    Unknown,   ///< This watcher cannot tell (polling): scan on your own timer.
    Quiet,     ///< Watching, and nothing changed since the last Take().
    Changed,   ///< Something was created, deleted, moved or written since the last Take().
};

class FileWatcher {
public:
    virtual ~FileWatcher() = default;

    /// Start watching `root` and everything below it (folders made later too). False, with
    /// `*error` set, if it cannot (missing folder, no support, watch limit). Calling it again
    /// stops the old watch first.
    virtual bool Start(const std::string& root, std::string* error = nullptr) = 0;
    virtual void Stop() = 0;
    /// True while a native watch is running. A polling watcher is never active.
    virtual bool Active() const = 0;
    /// "inotify", "ReadDirectoryChangesW" or "polling".
    virtual const char* Name() const = 0;

    /// Read what happened since the last call and clear it. Never blocks. Several events
    /// collapse into one Changed. When `paths` is given it receives the changed paths
    /// (absolute, '/' separators; a hint only, a long burst may leave it short, so treat
    /// Changed as "rescan", not as a full list).
    virtual WatchState Take(std::vector<std::string>* paths = nullptr) = 0;
};

/// The operating system's watcher, or null where there is none in this build.
std::unique_ptr<FileWatcher> CreateNativeFileWatcher();
/// The fallback: Start() always succeeds, Take() always answers Unknown.
std::unique_ptr<FileWatcher> CreatePollingFileWatcher();
/// Native if it starts on `root`, otherwise polling (never null; `*usedFallbackReason` says why).
std::unique_ptr<FileWatcher> MakeFileWatcher(const std::string& root, std::string* fallbackReason = nullptr);

} // namespace okay
