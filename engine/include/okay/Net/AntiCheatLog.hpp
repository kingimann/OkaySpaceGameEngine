#pragma once
// ---------------------------------------------------------------------------
// AntiCheatLog.hpp : the server's own ban list file and audit log.
//
//   BanList   a plain text file of banned identities. The anti-cheat adds an
//             entry when the detection engine asks for a ban, or when one
//             identity has been kicked `banAfterKicks` times. The server
//             reads it at start and refuses a banned identity at the join.
//             An owner edits it by hand: one line per ban, remove the line
//             (or run the server again after deleting it) to lift a ban.
//   AuditLog  an append-only text file of what the anti-cheat saw and did.
//             It holds a short one-way tag for each player (never a name, an
//             address or an account id) and it stays on the server's disk:
//             nothing here is sent to any outside service.
//
// Written from the plain description in docs/anticheat.md ("Bans and the audit
// log"); no outside code. No <mutex> / <thread> (Windows rule 21a): the server
// calls both from its one update thread.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace okay {

struct BanEntry {
    std::string ident;        ///< "u:<account id>" or "a:<address number>"
    std::int64_t when = 0;    ///< unix seconds the ban was made
    std::int64_t until = 0;   ///< unix seconds it ends; 0 = never
    std::string reason;       ///< a short plain reason (control characters removed)
};

class BanList {
public:
    static constexpr std::size_t kMaxEntries = 10000;     ///< a hostile flood of bans can't grow the file or the memory without end
    static constexpr std::size_t kMaxReasonBytes = 120;
    static constexpr std::size_t kMaxLineBytes = 512;

    /// Read `path`. A missing file is an empty list (true). Lines that don't
    /// parse are skipped and counted in Skipped(). False only when the file
    /// exists but can't be read.
    bool Load(const std::string& path, std::string* error = nullptr);
    const std::string& Path() const { return m_path; }
    int Skipped() const { return m_skipped; }
    std::size_t Size() const { return m_entries.size(); }

    /// Is `ident` banned at unix time `now`? Fills `out` with the entry.
    bool IsBanned(const std::string& ident, std::int64_t now, BanEntry* out = nullptr) const;
    /// Add (or extend) a ban for `hours` (0 = never ends). Expired entries are dropped first.
    /// False when the identity is malformed or the list is full of live bans.
    bool Add(const std::string& ident, std::int64_t now, double hours, const std::string& reason);
    bool Remove(const std::string& ident);
    /// Write the list to its file (temporary file, then rename). False + error on failure.
    bool Save(std::string* error = nullptr) const;

    static bool ValidIdent(const std::string& ident);
    static std::string CleanText(const std::string& s, std::size_t maxBytes);

    const std::unordered_map<std::string, BanEntry>& Entries() const { return m_entries; }

private:
    std::string m_path;
    std::unordered_map<std::string, BanEntry> m_entries;
    int m_skipped = 0;
};

class AuditLog {
public:
    static constexpr std::uint64_t kDefaultMaxBytes = 4u * 1024u * 1024u;   ///< past this the file moves to "<path>.1" and a new one starts

    /// Append to `path` from now on ("" = off). The folder must exist.
    void Open(const std::string& path, std::uint64_t maxBytes = kDefaultMaxBytes);
    bool IsOpen() const { return !m_path.empty(); }
    const std::string& Path() const { return m_path; }
    /// One line: "<UTC time> <event> who=<tag> <detail>". `ident` is turned into
    /// a short tag; `detail` is cleaned (no control characters, 160 bytes at most).
    /// An empty `ident` writes no who= field.
    void Write(std::int64_t unixTime, const std::string& event, const std::string& ident, const std::string& detail);
    std::size_t Lines() const { return m_lines; }
    /// The tag Write() prints for an identity: 10 hex characters of a salted SHA-256.
    /// Stable for one server (so an admin can follow one player through the log), not reversible by reading the file.
    std::string Tag(const std::string& ident) const;

private:
    std::string m_path;
    std::uint64_t m_maxBytes = kDefaultMaxBytes;
    std::uint64_t m_size = 0;
    std::size_t m_lines = 0;
    std::string m_salt;   ///< random per run unless "<path>.salt" exists: kept in a side file so tags stay stable across restarts
};

/// "2026-11-16T18:31:28Z" for a unix time.
std::string UtcTimeText(std::int64_t unixTime);

} // namespace okay
