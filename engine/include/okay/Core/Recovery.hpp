#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// Periodic autosave and crash recovery for an editor (or any tool that edits a
/// document in memory). Headless and timer free: the host calls Tick(dt) from its
/// frame loop, so nothing here needs threads.
///
/// How it works
///  - Every session has an id. StartSession() writes a small lock file for it;
///    EndSession() removes it. A lock that is still there while its process is
///    gone means the last run ended unclean (crash, kill, power loss).
///  - Autosave writes the document text to a rotating set of files
///    `<dir>/<base>-<session>-<slot>.autosave` (default 3 slots, each write goes
///    to the next slot), through savefmt::AtomicWrite. Each file carries a header
///    with the time, a size and an FNV-1a checksum, so a torn or damaged file is
///    detected and skipped, and the older slot is still offered.
///  - On the next start FindRecoverable() lists the valid autosaves left behind
///    by other sessions, newest first, with timestamps and sizes. The host shows
///    them and calls Restore() or Discard().
///
/// Several editors may share one directory: a lock whose process is still alive
/// marks a live peer, and its autosaves are not offered.
///
/// Not thread safe; call from one thread.
class RecoveryManager {
public:
    struct Config {
        std::string directory;              ///< where the files live (created on demand)
        std::string baseName = "autosave";  ///< file name prefix; use one per document kind
        int slots = 3;                      ///< rotating files per session (>= 1)
        float intervalSeconds = 120.0f;     ///< Tick() autosaves this often
        float retrySeconds = 10.0f;         ///< after a failed write, try again this soon
        std::size_t maxPayloadBytes = 256u * 1024u * 1024u;
        bool skipUnchanged = true;          ///< don't rewrite identical text
        int maxOtherSessions = 8;           ///< keep autosaves of at most this many old sessions
        bool removeCorruptOnStart = true;   ///< delete unreadable files of dead sessions at start
        std::string sessionId;              ///< empty = generate one (set it in tests)
    };

    struct Candidate {
        std::string path;        ///< the autosave file
        std::string sessionId;
        std::uint64_t seq = 0;   ///< write counter within the session
        std::int64_t timestamp = 0;   ///< unix seconds when it was written
        std::uint64_t size = 0;  ///< payload bytes
        std::string label;       ///< what the host stored ("scene path"), may be empty
        bool unclean = false;    ///< its session was found dead (lock left behind) this run
    };

    struct SkippedFile {
        std::string path;
        std::string reason;      ///< "checksum mismatch", "truncated", "bad header", "newer format" ...
    };

    enum class AutosaveResult {
        Written,   ///< a file was written
        Skipped,   ///< nothing to do (provider had nothing, or unchanged)
        Failed,    ///< could not write; see LastError()
    };

    RecoveryManager();
    explicit RecoveryManager(Config cfg);
    RecoveryManager(const RecoveryManager&) = delete;
    RecoveryManager& operator=(const RecoveryManager&) = delete;

    // ---- Hooks ----------------------------------------------------------
    /// Where "now" comes from (unix seconds). Default: the system clock.
    void SetClock(std::function<std::int64_t()> clock) { m_clock = std::move(clock); }
    /// Is the process with this pid still running? Default asks the OS.
    void SetProcessProbe(std::function<bool(long pid)> probe) { m_probe = std::move(probe); }
    /// Gives the text to autosave and a label. Return false when there is nothing
    /// to save (the document isn't modified).
    using Provider = std::function<bool(std::string& payload, std::string& label)>;
    void SetProvider(Provider p) { m_provider = std::move(p); }

    // ---- Session ----------------------------------------------------------
    /// Create the directory, find dead sessions (stale locks), clean up their
    /// half-written files, trim old sessions, and write this session's lock.
    /// False if the directory or lock can't be created (autosave is then off).
    bool StartSession();
    /// A clean exit: removes the lock and (by default) this session's autosaves,
    /// since a clean exit means the user saved or chose not to.
    void EndSession(bool removeAutosaves = true);
    bool Active() const { return m_active; }
    const std::string& SessionId() const { return m_session; }
    /// Did StartSession() find the previous run dead (its lock still there)?
    bool UncleanShutdownDetected() const { return !m_crashed.empty(); }
    const std::vector<std::string>& CrashedSessions() const { return m_crashed; }

    // ---- Autosave -----------------------------------------------------------
    void SetEnabled(bool on) { m_enabled = on; }
    bool Enabled() const { return m_enabled; }
    void SetInterval(float seconds) { m_cfg.intervalSeconds = seconds; }
    float Interval() const { return m_cfg.intervalSeconds; }
    /// Advance the timer; autosaves (via the provider) when the interval has
    /// elapsed. Returns true if a file was written.
    bool Tick(float dt);
    /// Autosave now with the provider.
    AutosaveResult Autosave();
    /// Autosave this text now.
    AutosaveResult AutosavePayload(const std::string& payload, const std::string& label = "");
    /// The user saved the real file: drop this session's autosaves, they are
    /// superseded. (The provider should return false while the document is clean.)
    void OnSaved();
    int AutosaveCount() const { return m_count; }              ///< files written this session
    std::int64_t LastAutosaveTime() const { return m_lastTime; }   ///< 0 if none yet
    const std::string& LastError() const { return m_error; }
    /// Path of the file the next autosave would write.
    std::string NextAutosavePath() const;

    // ---- Recovery -------------------------------------------------------------
    /// Valid autosaves from other (dead) sessions, newest first. Files that fail
    /// validation are left out; SkippedFiles() lists them. `includeOwnSession`
    /// also lists this session's own files.
    std::vector<Candidate> FindRecoverable(bool includeOwnSession = false);
    const std::vector<SkippedFile>& SkippedFiles() const { return m_skipped; }
    /// Re-validate and read the payload. False (and `error`) if it is damaged now.
    bool Restore(const Candidate& c, std::string& payload, std::string* error = nullptr) const;
    /// Restore straight into a file (atomic write).
    bool RestoreToFile(const Candidate& c, const std::string& destPath, std::string* error = nullptr) const;
    /// Delete one autosave.
    bool Discard(const Candidate& c);
    /// Delete every file of other, dead sessions (valid or not). Returns the count.
    int DiscardAll();
    /// Delete the unreadable files of dead sessions. Returns the count.
    int RemoveCorrupt();

    // ---- Format helpers (public for tools and tests) -------------------------------
    /// The bytes of an autosave file.
    static std::string Encode(const std::string& session, std::uint64_t seq, std::int64_t time,
                              const std::string& label, const std::string& payload);
    /// Parse + validate. False with `reason` if torn or damaged.
    static bool Decode(const std::string& file, Candidate& meta, std::string& payload, std::string* reason);

private:
    struct FileEntry { std::string path; std::string name; };
    std::int64_t Now() const;
    bool ProcessAlive(long pid) const;
    std::string PathFor(const std::string& session, int slot) const;
    std::string LockPath(const std::string& session) const;
    std::vector<FileEntry> ListFiles(const char* suffix) const;
    bool SessionLive(const std::string& session, bool* hasLock = nullptr) const;
    void ScanStaleLocks();
    void TrimOldSessions();
    void RemoveOwnAutosaves();
    AutosaveResult WritePayload(const std::string& payload, const std::string& label);

    Config m_cfg;
    std::function<std::int64_t()> m_clock;
    std::function<bool(long)> m_probe;
    Provider m_provider;
    std::string m_session;
    std::vector<std::string> m_crashed;
    std::vector<SkippedFile> m_skipped;
    std::string m_error;
    bool m_active = false;
    bool m_enabled = true;
    float m_elapsed = 0.0f;
    std::uint64_t m_seq = 0;
    int m_count = 0;
    std::int64_t m_lastTime = 0;
    bool m_haveLast = false;
    std::uint64_t m_lastHash = 0;
    std::size_t m_lastSize = 0;
};

} // namespace okay
