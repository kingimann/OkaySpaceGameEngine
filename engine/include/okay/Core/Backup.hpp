#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// How many backups of one file to keep.
struct BackupPolicy {
    /// Always keep this many newest backups (at least 1: the newest is never pruned).
    int keepLast = 10;
    /// Beyond those, also keep the newest backup of each of the last N days
    /// (today counts as day 1). 0 turns the daily rule off.
    int keepDailyDays = 14;
    /// Total size cap per file, oldest first (0 = no cap). The newest backup stays.
    std::uint64_t maxTotalBytes = 0;
};

/// Timestamped backups of project / scene files, taken before they are overwritten.
///
///   BackupManager backups({ "<project>/.backups" });
///   backups.SaveWithBackup(scenePath, text);      // copy the old file aside, then write
///   auto list = backups.List(scenePath);          // newest first
///   backups.Restore(list[2], scenePath);          // roll back (the current file is backed up first)
///
/// Backups live in one directory as `<file name>.<path hash>.<YYYYMMDD-HHMMSS>[-n].bak`
/// (UTC; `-n` separates two backups taken in the same second), so two files with the
/// same name in different folders never mix. Every write goes through
/// savefmt::AtomicWrite. Not thread safe.
class BackupManager {
public:
    struct Config {
        std::string directory;                    ///< where the .bak files go (created on demand)
        BackupPolicy policy;
        std::uint64_t maxFileBytes = 256ull * 1024 * 1024;   ///< bigger files are not backed up
        bool skipIdentical = true;                ///< no new backup if the newest one is identical
        bool backupBeforeRestore = true;          ///< Restore() keeps the file it replaces
        bool autoPrune = true;                    ///< apply the policy after every backup
        bool requireBackup = false;               ///< SaveWithBackup refuses to save if the backup failed
        int dayOffsetSeconds = 0;                 ///< shifts "one per day" to local midnight (UTC offset)
    };

    struct Info {
        std::string path;         ///< the .bak file
        std::string source;       ///< file name of the original ("Level1.okayscene")
        std::int64_t timestamp = 0;   ///< unix seconds (UTC, from the name)
        std::uint64_t size = 0;
        int seq = 0;              ///< 0, or n for the n-th backup within one second
    };

    BackupManager();
    explicit BackupManager(Config cfg);

    /// Where "now" comes from (unix seconds). Default: the system clock.
    void SetClock(std::function<std::int64_t()> clock) { m_clock = std::move(clock); }
    const Config& GetConfig() const { return m_cfg; }
    void SetPolicy(const BackupPolicy& p) { m_cfg.policy = p; }

    /// Copy `file` into the backup directory. True if a backup of the current
    /// content exists afterwards (made now, or the newest one was identical);
    /// false if the file is missing, too big, or the copy failed (LastError()).
    bool Backup(const std::string& file, std::string* outBackup = nullptr, bool* created = nullptr);
    /// Back up the existing `file` (if any), then write `content` over it.
    bool SaveWithBackup(const std::string& file, const std::string& content);

    /// Backups of `file`, newest first.
    std::vector<Info> List(const std::string& file) const;
    /// Every backup in the directory, newest first.
    std::vector<Info> ListAll() const;
    bool Read(const Info& b, std::string& content) const;
    /// Write the backup over `file`. With backupBeforeRestore the file being
    /// replaced is backed up first, so a restore can itself be undone.
    bool Restore(const Info& b, const std::string& file);
    bool Delete(const Info& b);

    /// Apply the policy to the backups of `file`. Returns how many were deleted.
    int Prune(const std::string& file);
    /// Apply it to every file's backups.
    int PruneAll();

    const std::string& LastError() const { return m_error; }

    // ---- Pure helpers (public for tests and tools) -----------------------------------
    /// Indices into `newestFirst` that the policy would delete.
    static std::vector<std::size_t> SelectForRemoval(const std::vector<Info>& newestFirst, const BackupPolicy& policy,
                                                     std::int64_t now, int dayOffsetSeconds = 0);
    static std::string FormatStamp(std::int64_t unixSeconds);                 ///< "YYYYMMDD-HHMMSS" (UTC)
    static bool ParseStamp(const std::string& stamp, std::int64_t& unixSeconds);
    static std::int64_t DayIndex(std::int64_t unixSeconds, int dayOffsetSeconds = 0);

private:
    std::int64_t Now() const;
    std::string KeyPrefix(const std::string& file) const;
    static bool ParseName(const std::string& name, Info& out, std::string& key);
    std::vector<Info> Scan(const std::string* keyPrefix) const;
    int PruneList(std::vector<Info> list);

    Config m_cfg;
    std::function<std::int64_t()> m_clock;
    std::string m_error;
};

} // namespace okay
