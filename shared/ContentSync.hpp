#pragma once
// Content sync: bring a local content folder up to date with a signed manifest on a server. Docs: docs/release/content_delivery.md.
// A plain state machine over the abstract Transport (ContentTransport.hpp): no sockets, no threads, no engine code here. It runs on the
// caller's thread (the player runs it from its own job); progress and cancel come through callbacks.
//
// Folder layout under the content root:
//   current.txt            the COMMIT RECORD, written last (temp file + rename). Names the version that is visible. Without it nothing is.
//   manifests/<n>.manifest and <n>.manifest.sig     the verified manifest of version n and its signature
//   trees/<n>/<path>       every file of version n, in the same folder layout the game uses (a scene's relative references keep working)
//   trees/<n>.tmp/         version n while it is being built (verified files only; resumed by the next run)
//   staging/<sha256>.part  partial downloads, resumable; never visible to the game
// The previous good version (trees/<m>) stays until the next update commits, for rollback. Files that did not change are hard-linked
// (or copied when the disk cannot link) from the previous tree, so an update downloads only what changed.
//
// A file becomes visible only after: its size and SHA-256 equal the signed manifest's, the optional script validator accepted it,
// every other file of the version is also verified, and the commit record is written. A crash or a cancel at any point leaves the
// previous version in place.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "ContentManifest.hpp"
#include "ContentTransport.hpp"
#include "okay/Core/FileCopy.hpp"   // CopyFileOver: copy_file with overwrite_existing fails over an existing file on Windows (MinGW)

namespace okaycontent {

namespace fs = std::filesystem;

// ---- The commit record ---------------------------------------------------------------------------------------------------------
struct Commit {
    std::string game;
    std::uint64_t version = 0;          ///< the visible version
    std::string manifestSha256;         ///< of that version's manifest bytes
    std::uint64_t previous = 0;         ///< the version kept for rollback (0 = none)
    std::uint64_t floor = 0;            ///< the highest version ever committed here: a manifest below it is a replay and is refused
};

namespace detail {
inline fs::path P(const std::string& s) { return fs::u8path(s); }

inline bool ReadAll(const fs::path& p, std::string& out, std::uint64_t maxBytes) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    const std::uint64_t sz = fs::file_size(p, ec);
    if (ec || sz > maxBytes) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(static_cast<std::size_t>(sz), '\0');
    if (sz) in.read(&out[0], static_cast<std::streamsize>(sz));
    return static_cast<std::uint64_t>(in.gcount()) == sz;
}

/// Write to "<p>.new", flush, close, rename over `p`. The rename is the commit point.
inline bool WriteAtomic(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p; tmp += ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) { out.close(); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, p, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

inline std::string FileSha256Hex(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::string();
    okay::crypto::Sha256 h;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) h.Update(reinterpret_cast<const std::uint8_t*>(buf.data()), static_cast<std::size_t>(n));
    }
    if (in.bad()) return std::string();
    std::uint8_t d[32];
    h.Final(d);
    return okayac::ToHex(d, 32);
}

inline bool FileHasSizeAndHash(const fs::path& p, const Entry& e, bool checkHash) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != e.size || ec) return false;
    return !checkHash || FileSha256Hex(p) == e.sha256;
}

/// Hard link, else copy. Both leave `from` untouched.
inline bool LinkOrCopy(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::remove(to, ec);
    fs::create_hard_link(from, to, ec);
    if (!ec) return true;
    return okay::CopyFileOver(from, to, ec);   // `to` may still be there if the remove above failed
}
}  // namespace detail

/// Paths inside a content root.
struct StorePaths {
    std::string root;
    fs::path Root() const { return detail::P(root); }
    fs::path CommitFile() const { return Root() / "current.txt"; }
    fs::path ManifestFile(std::uint64_t v) const { return Root() / "manifests" / (std::to_string(v) + ".manifest"); }
    fs::path SigFile(std::uint64_t v) const { return Root() / "manifests" / (std::to_string(v) + ".manifest.sig"); }
    fs::path Tree(std::uint64_t v) const { return Root() / "trees" / std::to_string(v); }
    fs::path TmpTree(std::uint64_t v) const { return Root() / "trees" / (std::to_string(v) + ".tmp"); }
    fs::path Staging() const { return Root() / "staging"; }
};

inline std::string SerializeCommit(const Commit& c) {
    return "okay-content-commit 1\ngame " + c.game + "\nversion " + std::to_string(c.version) + "\nmanifest-sha256 " + c.manifestSha256 +
           "\nprevious " + std::to_string(c.previous) + "\nfloor " + std::to_string(c.floor) + "\n";
}

/// Strict read of current.txt. False when it is missing or damaged (the game then sees no downloaded content at all).
inline bool ReadCommit(const StorePaths& sp, Commit& out) {
    out = Commit();
    std::string text;
    if (!detail::ReadAll(sp.CommitFile(), text, 4096)) return false;
    const std::vector<std::string> lines = detail::Split(text, '\n');
    if (lines.size() != 7 || !lines[6].empty() || lines[0] != "okay-content-commit 1") return false;
    auto val = [&](std::size_t i, const char* key, std::string& v) {
        const std::string k = std::string(key) + " ";
        if (lines[i].compare(0, k.size(), k) != 0) return false;
        v = lines[i].substr(k.size());
        return true;
    };
    std::string game, ver, sha, prev, floor;
    if (!val(1, "game", game) || !val(2, "version", ver) || !val(3, "manifest-sha256", sha) || !val(4, "previous", prev) || !val(5, "floor", floor)) return false;
    Commit c;
    c.game = game;
    c.manifestSha256 = sha;
    if (!detail::IdOk(game, 64) || !detail::IsLowerHex(sha, 64)) return false;
    std::uint64_t pv = 0;
    if (!detail::Digits(ver, c.version, 1ull << 53) || !detail::Digits(floor, c.floor, 1ull << 53)) return false;
    if (prev != "0") { if (!detail::Digits(prev, pv, 1ull << 53)) return false; c.previous = pv; }
    out = c;
    return true;
}

/// The manifest of the committed version, read from the store and parsed (not re-verified: see ContentOverlay for that).
inline bool ReadCommittedManifest(const StorePaths& sp, const Commit& c, Manifest& m, std::string* manifestText = nullptr, const Limits& lim = Limits()) {
    std::string text;
    if (!detail::ReadAll(sp.ManifestFile(c.version), text, lim.maxManifestBytes)) return false;
    if (Sha256Hex(text) != c.manifestSha256) return false;
    if (!Parse(text, m, nullptr, lim) || m.version != c.version) return false;
    if (manifestText) *manifestText = std::move(text);
    return true;
}

/// Put the previous version back as the visible one (a bad release). Keeps `floor`. False when there is no usable previous version.
inline bool Rollback(const StorePaths& sp, std::string* why = nullptr) {
    Commit c;
    if (!ReadCommit(sp, c) || c.previous == 0) { if (why) *why = "there is no previous content version to go back to"; return false; }
    Commit prev = c;
    prev.version = c.previous;
    prev.previous = 0;
    std::string text;
    std::error_code ec;
    if (!detail::ReadAll(sp.ManifestFile(c.previous), text, 4u << 20) || !fs::is_directory(sp.Tree(c.previous), ec)) {
        if (why) *why = "the previous version's files are gone";
        return false;
    }
    prev.manifestSha256 = Sha256Hex(text);
    if (!detail::WriteAtomic(sp.CommitFile(), SerializeCommit(prev))) { if (why) *why = "could not write the commit record"; return false; }
    fs::remove_all(sp.Tree(c.version), ec);
    fs::remove(sp.ManifestFile(c.version), ec);
    fs::remove(sp.SigFile(c.version), ec);
    return true;
}

// ---- Configuration and result ----------------------------------------------------------------------------------------------------
enum class Phase { Manifest, Planning, Downloading, Verifying, Committing, Done };

struct Progress {
    Phase phase = Phase::Manifest;
    std::string file;                 ///< the file being fetched ("" outside Downloading)
    int fileIndex = 0, fileCount = 0; ///< among the files that must be downloaded
    std::uint64_t bytesDone = 0, bytesTotal = 0;   ///< over all files that must be downloaded
};

struct SyncConfig {
    std::string root;                  ///< content folder (created on first use)
    std::string game;                  ///< the manifest's game id must equal this
    std::string appVersion;            ///< the running app version, for min-app ("" = unknown: min-app requirements are then NOT met)
    std::string trustedKeys;           ///< "<hex>[,<hex>]" ed25519 public keys (see CheckSignature)
    bool allowUnsigned = false;        ///< developer switch: skip the signature (never set by a shipped game by default)
    std::vector<std::string> groups;   ///< wanted groups; empty = all. Files with no group are always wanted
    Limits limits;
    std::uint64_t budgetBytes = 0;     ///< most bytes one version may take on disk (0 = limits.maxTotalBytes); a bigger manifest is refused
    RetryPolicy retry;
    bool allowDowngrade = false;       ///< accept a version below the floor (never from a shipped game)
    bool deepVerify = false;           ///< re-hash files reused from the previous version (slow; default trusts the commit)
    /// Called with every file of kind Script, Dialogue or Data that was just verified (path = the verified file, not yet visible). Return ""
    /// to accept or a plain reason to refuse; a refusal fails the whole sync. This is where graphs and scripts get their safety checks.
    std::function<std::string(const Entry&, const std::string& path)> validate;
    std::function<bool(const Progress&)> progress;   ///< return false to cancel (partial files stay for the next run)
    std::function<void(int ms)> sleepMs;             ///< wait between retries; empty = do not wait (tests)
    std::function<void(const std::string&)> log;     ///< one plain line per event; empty = silent
};

enum class Status { Updated, UpToDate, Cancelled, Failed };
enum class Reason { None, Network, ServerMissingFile, ManifestBad, SignatureBad, GameMismatch, AppTooOld, OverBudget, Downgrade, FileCorrupt, Validation, Disk, Cancelled };

struct SyncResult {
    Status status = Status::Failed;
    Reason reason = Reason::None;
    std::string message;               ///< plain words for the person
    std::uint64_t version = 0;         ///< the visible version after the run
    int filesDownloaded = 0, filesReused = 0, filesSkipped = 0;
    std::uint64_t bytesDownloaded = 0;
    int retries = 0;
    int corruptRejected = 0;           ///< files whose bytes did not match the manifest and were thrown away
    std::vector<std::string> removed;  ///< old versions removed after the commit ("version 4"); nothing is removed silently
};

inline const char* ReasonName(Reason r) {
    switch (r) {
        case Reason::None: return "none";
        case Reason::Network: return "network";
        case Reason::ServerMissingFile: return "server-missing-file";
        case Reason::ManifestBad: return "manifest-bad";
        case Reason::SignatureBad: return "signature-bad";
        case Reason::GameMismatch: return "game-mismatch";
        case Reason::AppTooOld: return "app-too-old";
        case Reason::OverBudget: return "over-budget";
        case Reason::Downgrade: return "downgrade";
        case Reason::FileCorrupt: return "file-corrupt";
        case Reason::Validation: return "validation";
        case Reason::Disk: return "disk";
        case Reason::Cancelled: return "cancelled";
    }
    return "none";
}

/// Which manifest entries this app wants: group filter and per-entry min-app. `skipped` counts the rest.
inline std::vector<Entry> SelectEntries(const Manifest& m, const SyncConfig& cfg, int& skipped) {
    std::vector<Entry> out;
    skipped = 0;
    for (const Entry& e : m.entries) {
        if (!e.group.empty() && !cfg.groups.empty() && std::find(cfg.groups.begin(), cfg.groups.end(), e.group) == cfg.groups.end()) { ++skipped; continue; }
        if (!e.minApp.empty() && (cfg.appVersion.empty() || okayver::CompareVersions(cfg.appVersion, e.minApp) < 0)) { ++skipped; continue; }
        out.push_back(e);
    }
    return out;
}

class Sync {
public:
    Sync(SyncConfig cfg, Transport& t) : m_cfg(std::move(cfg)), m_t(t), m_sp{m_cfg.root} {}

    SyncResult Run() {
        SyncResult r;
        std::error_code ec;
        fs::create_directories(m_sp.Root(), ec);
        if (!fs::is_directory(m_sp.Root(), ec)) return Fail(r, Reason::Disk, "Cannot create the content folder.");

        // 1. manifest + signature --------------------------------------------------------------------------------------------------
        Report(Phase::Manifest, "", 0, 0, 0, 0);
        std::string manText, sigText;
        if (!FetchSmall(kManifestName, m_cfg.limits.maxManifestBytes, manText, r)) return r;
        {
            SyncResult tmp;
            // A missing signature file is a normal verdict ("not signed"), not a network failure: fetch it quietly.
            if (!FetchSmall(kSigName, okayupdate::kMaxSigBytes, sigText, tmp, /*optional=*/true)) {
                if (tmp.reason == Reason::Network) return Fail(r, Reason::Network, tmp.message);
            }
        }
        const SigVerdict sv = CheckSignature(m_cfg.trustedKeys, manText, sigText, m_cfg.allowUnsigned);
        if (!sv.Accepts()) return Fail(r, Reason::SignatureBad, sv.message);
        Manifest man;
        std::string why;
        if (!Parse(manText, man, &why, m_cfg.limits)) return Fail(r, Reason::ManifestBad, "The content list is not valid: " + why);
        if (man.game != m_cfg.game) return Fail(r, Reason::GameMismatch, "This content is for the game '" + man.game + "', not '" + m_cfg.game + "'.");
        if (!man.minApp.empty() && (m_cfg.appVersion.empty() || okayver::CompareVersions(m_cfg.appVersion, man.minApp) < 0))
            return Fail(r, Reason::AppTooOld, "This content needs app version " + man.minApp + " or newer. Update the game first.");

        Commit cur;
        const bool haveCur = ReadCommit(m_sp, cur) && cur.game == m_cfg.game;
        const std::uint64_t floor = haveCur ? cur.floor : 0;
        if (!m_cfg.allowDowngrade && man.version < floor)
            return Fail(r, Reason::Downgrade, "The server offered content version " + std::to_string(man.version) + ", older than the " + std::to_string(floor) +
                                                  " this game already has. Refused (possible replay of an old list).");
        const std::string manSha = Sha256Hex(manText);
        if (haveCur && cur.version == man.version) {
            std::error_code e2;
            if (cur.manifestSha256 != manSha) return Fail(r, Reason::ManifestBad, "The server's content list changed without a new version number. Refused.");
            if (fs::is_directory(m_sp.Tree(cur.version), e2)) {
                r.status = Status::UpToDate; r.version = cur.version; r.message = "Content is up to date (version " + std::to_string(cur.version) + ").";
                Report(Phase::Done, "", 0, 0, 0, 0);
                return r;
            }
        }

        // 2. plan ------------------------------------------------------------------------------------------------------------------
        Report(Phase::Planning, "", 0, 0, 0, 0);
        int skipped = 0;
        std::vector<Entry> want = SelectEntries(man, m_cfg, skipped);
        r.filesSkipped = skipped;
        std::uint64_t total = 0;
        for (const Entry& e : want) total += e.size;
        const std::uint64_t budget = m_cfg.budgetBytes ? m_cfg.budgetBytes : m_cfg.limits.maxTotalBytes;
        if (total > budget)
            return Fail(r, Reason::OverBudget, "This content update needs " + std::to_string(total) + " bytes, more than the " + std::to_string(budget) +
                                                   " this game may keep. Nothing was downloaded and nothing was deleted.");

        Manifest prevMan;
        std::map<std::string, const Entry*> prevByPath;
        std::map<std::string, std::string> prevPathBySha;   // sha -> a path in the previous tree
        const bool havePrev = haveCur && ReadCommittedManifest(m_sp, cur, prevMan, nullptr, m_cfg.limits);
        if (havePrev) for (const Entry& e : prevMan.entries) { prevByPath[e.path] = &e; prevPathBySha.emplace(e.sha256, e.path); }

        const fs::path tmpTree = m_sp.TmpTree(man.version);
        std::error_code e3;
        fs::create_directories(tmpTree, e3);
        fs::create_directories(m_sp.Staging(), e3);

        std::vector<const Entry*> toDownload;
        std::map<std::string, std::string> placedBySha;   // sha -> path (relative) already in tmpTree this run
        std::vector<std::pair<const Entry*, fs::path>> fresh;   // files that need the validator
        for (const Entry& e : want) {
            const fs::path dest = tmpTree / detail::P(e.path);
            if (detail::FileHasSizeAndHash(dest, e, /*hash=*/true)) { ++r.filesReused; placedBySha.emplace(e.sha256, e.path); fresh.emplace_back(&e, dest); continue; }
            bool placed = false;
            auto pp = prevByPath.find(e.path);
            if (pp != prevByPath.end() && pp->second->sha256 == e.sha256 && pp->second->size == e.size &&
                detail::FileHasSizeAndHash(m_sp.Tree(cur.version) / detail::P(e.path), e, m_cfg.deepVerify))
                placed = detail::LinkOrCopy(m_sp.Tree(cur.version) / detail::P(e.path), dest);
            if (!placed) {
                auto ps = prevPathBySha.find(e.sha256);
                if (ps != prevPathBySha.end() && detail::FileHasSizeAndHash(m_sp.Tree(cur.version) / detail::P(ps->second), e, m_cfg.deepVerify))
                    placed = detail::LinkOrCopy(m_sp.Tree(cur.version) / detail::P(ps->second), dest);
            }
            if (placed) { ++r.filesReused; placedBySha.emplace(e.sha256, e.path); continue; }
            toDownload.push_back(&e);
        }
        std::uint64_t bytesTotal = 0;
        for (const Entry* e : toDownload) bytesTotal += e->size;
        {
            std::error_code se;
            const fs::space_info si = fs::space(m_sp.Root(), se);
            if (!se && si.available != static_cast<std::uintmax_t>(-1) && bytesTotal > si.available)
                return Fail(r, Reason::Disk, "Not enough free disk space for the content update (needs " + std::to_string(bytesTotal) + " bytes).");
        }

        // 3. download --------------------------------------------------------------------------------------------------------------
        std::uint64_t doneBytes = 0;
        int index = 0;
        for (const Entry* e : toDownload) {
            ++index;
            const fs::path dest = tmpTree / detail::P(e->path);
            auto same = placedBySha.find(e->sha256);
            if (same != placedBySha.end() && detail::FileHasSizeAndHash(tmpTree / detail::P(same->second), *e, false) &&
                detail::LinkOrCopy(tmpTree / detail::P(same->second), dest)) {
                ++r.filesReused; doneBytes += e->size; fresh.emplace_back(e, dest); continue;   // same bytes as a file already in this version
            }
            if (!FetchFile(*e, dest, index, static_cast<int>(toDownload.size()), doneBytes, bytesTotal, r)) return r;
            doneBytes += e->size;
            placedBySha.emplace(e->sha256, e->path);
            fresh.emplace_back(e, dest);
        }

        // 4. verify + validate -----------------------------------------------------------------------------------------------------
        Report(Phase::Verifying, "", 0, static_cast<int>(want.size()), bytesTotal, bytesTotal);
        if (m_cfg.validate) {
            for (auto& f : fresh) {
                if (f.first->kind != Kind::Script && f.first->kind != Kind::Dialogue && f.first->kind != Kind::Data) continue;
                const std::string bad = m_cfg.validate(*f.first, f.second.string());
                if (!bad.empty()) {
                    std::error_code e4;
                    fs::remove_all(tmpTree, e4);   // a refused script never stays around, not even invisibly
                    return Fail(r, Reason::Validation, "The file '" + f.first->path + "' was refused: " + bad);
                }
            }
        }
        for (const Entry& e : want)   // belt and braces: every file of the version is really there with its size
            if (!detail::FileHasSizeAndHash(tmpTree / detail::P(e.path), e, false)) return Fail(r, Reason::Disk, "A file of the new version is missing: " + e.path);

        // 5. commit ----------------------------------------------------------------------------------------------------------------
        Report(Phase::Committing, "", 0, 0, bytesTotal, bytesTotal);
        std::error_code e5;
        fs::remove_all(m_sp.Tree(man.version), e5);   // a stale, uncommitted tree of this version number
        fs::rename(tmpTree, m_sp.Tree(man.version), e5);
        if (e5) return Fail(r, Reason::Disk, "Could not put the new content in place: " + e5.message());
        if (!detail::WriteAtomic(m_sp.ManifestFile(man.version), manText) || (!sigText.empty() && !detail::WriteAtomic(m_sp.SigFile(man.version), sigText)))
            return Fail(r, Reason::Disk, "Could not save the content list.");
        Commit nc;
        nc.game = m_cfg.game;
        nc.version = man.version;
        nc.manifestSha256 = manSha;
        nc.previous = (haveCur && cur.version != man.version) ? cur.version : (haveCur ? cur.previous : 0);
        nc.floor = std::max<std::uint64_t>(floor, man.version);
        if (!detail::WriteAtomic(m_sp.CommitFile(), SerializeCommit(nc))) return Fail(r, Reason::Disk, "Could not write the content commit record. The old content stays in use.");

        // 6. tidy: only after the commit; says what it removed -----------------------------------------------------------------------
        const std::set<std::uint64_t> keep = {nc.version, nc.previous};
        for (const char* sub : {"trees", "manifests"}) {
            std::error_code e6;
            for (fs::directory_iterator it(m_sp.Root() / sub, e6), end; !e6 && it != end; it.increment(e6)) {
                std::string n = it->path().filename().string();
                std::string digits = n.substr(0, n.find_first_not_of("0123456789"));
                std::uint64_t v = 0;
                if (!digits.empty() && detail::Digits(digits, v, 1ull << 53) && keep.count(v)) continue;
                std::error_code e7;
                fs::remove_all(it->path(), e7);
                if (std::string(sub) == "trees") r.removed.push_back("old content version " + digits);
            }
        }
        { std::error_code e8; fs::remove_all(m_sp.Staging(), e8); }

        r.status = Status::Updated;
        r.version = nc.version;
        r.message = "Content updated to version " + std::to_string(nc.version) + " (" + std::to_string(r.filesDownloaded) + " downloaded, " + std::to_string(r.filesReused) + " already here).";
        Log(r.message);
        Report(Phase::Done, "", 0, 0, bytesTotal, bytesTotal);
        return r;
    }

private:
    SyncConfig m_cfg;
    Transport& m_t;
    StorePaths m_sp;
    bool m_cancelled = false;

    void Log(const std::string& s) { if (m_cfg.log) m_cfg.log(s); }
    SyncResult& Fail(SyncResult& r, Reason why, const std::string& msg) {
        r.status = (why == Reason::Cancelled) ? Status::Cancelled : Status::Failed;
        r.reason = why;
        r.message = msg;
        Log("content sync: " + msg);
        return r;
    }
    bool Report(Phase ph, const std::string& file, int idx, int count, std::uint64_t done, std::uint64_t total) {
        if (!m_cfg.progress) return true;
        Progress p; p.phase = ph; p.file = file; p.fileIndex = idx; p.fileCount = count; p.bytesDone = done; p.bytesTotal = total;
        if (!m_cfg.progress(p)) m_cancelled = true;
        return !m_cancelled;
    }
    void Wait(int retryNumber) { if (m_cfg.sleepMs) m_cfg.sleepMs(m_cfg.retry.DelayBeforeRetry(retryNumber)); }

    // A small whole-body fetch (manifest, signature) with the retry policy.
    bool FetchSmall(const char* name, std::uint64_t maxBytes, std::string& out, SyncResult& r, bool optional = false) {
        std::string lastErr;
        for (int attempt = 1; attempt <= m_cfg.retry.maxAttempts; ++attempt) {
            if (attempt > 1) { ++r.retries; Wait(attempt - 1); }
            out.clear();
            int status = 0;
            bool tooBig = false;
            GetHandlers h;
            h.onHead = [&](const GetHead& hd) { status = hd.status; return hd.status == 200; };
            h.onData = [&](const std::uint8_t* d, std::size_t n) {
                if (out.size() + n > maxBytes) { tooBig = true; return false; }
                out.append(reinterpret_cast<const char*>(d), n);
                return true;
            };
            const GetResult g = m_t.Get(name, 0, maxBytes, h);
            if (tooBig) { Fail(r, Reason::ManifestBad, std::string("The server's ") + name + " is larger than allowed."); return false; }
            if (g.reached && status == 200) return true;
            if (g.aborted || g.reached) {
                if (status == 404 && optional) { out.clear(); return true; }
                if (status >= 400 && status < 500 && status != 408 && status != 429) {
                    Fail(r, Reason::ServerMissingFile, std::string("The server does not have ") + name + " (status " + std::to_string(status) + ").");
                    return false;
                }
                lastErr = "server answered " + std::to_string(status);
            } else lastErr = g.error;
        }
        Fail(r, Reason::Network, "Could not reach the content server (" + lastErr + ").");
        return false;
    }

    // Download one file to staging, verify it, move it to `dest`. On any non-recoverable problem fills `r` and returns false.
    bool FetchFile(const Entry& e, const fs::path& dest, int index, int count, std::uint64_t doneBefore, std::uint64_t grandTotal, SyncResult& r) {
        const fs::path part = m_sp.Staging() / (e.sha256 + ".part");
        std::string lastErr = "no attempt";
        Reason lastReason = Reason::Network;
        for (int attempt = 1; attempt <= m_cfg.retry.maxAttempts; ++attempt) {
            if (attempt > 1) { ++r.retries; Wait(attempt - 1); }
            std::error_code ec;
            std::uint64_t have = fs::is_regular_file(part, ec) ? fs::file_size(part, ec) : 0;
            if (have > e.size) { fs::remove(part, ec); have = 0; }
            if (have < e.size) {
                std::ofstream out;
                std::uint64_t written = have;           // size of the part file as it grows
                bool opened = false, overlong = false, badHead = false, diskFail = false;
                int status = 0;
                GetHandlers h;
                h.onHead = [&](const GetHead& hd) {
                    status = hd.status;
                    if (hd.status != 200 && hd.status != 206) return false;
                    if (hd.totalSize != 0 && hd.status == 200 && hd.totalSize != e.size) { badHead = true; return false; }
                    if (hd.status == 206 && hd.totalSize != 0 && hd.totalSize != e.size) { badHead = true; return false; }
                    std::ios::openmode mode = std::ios::binary;
                    if (hd.status == 206) {
                        if (hd.bodyStart != have) { badHead = true; return false; }   // not the part we asked for: never splice it in
                        mode |= std::ios::app;
                    } else { mode |= std::ios::trunc; written = 0; }   // the server ignored the range: start the file again
                    out.open(part, mode);
                    opened = out.is_open();
                    if (!opened) diskFail = true;
                    return opened;
                };
                h.onData = [&](const std::uint8_t* d, std::size_t n) {
                    if (written + n > e.size) { overlong = true; return false; }   // more than the manifest says: a hostile or broken server
                    out.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(n));
                    if (!out) { diskFail = true; return false; }
                    written += n;
                    return Report(Phase::Downloading, e.path, index, count, doneBefore + std::min<std::uint64_t>(written, e.size), grandTotal);
                };
                const GetResult g = m_t.Get(e.path, have, e.size, h);
                if (opened) out.close();
                if (m_cancelled) { Fail(r, Reason::Cancelled, "Content update cancelled. Nothing was changed."); return false; }
                if (diskFail) { Fail(r, Reason::Disk, "Could not write to the content folder (disk full or not allowed)."); return false; }
                if (overlong) { fs::remove(part, ec); lastErr = "the server sent more bytes than the content list says"; lastReason = Reason::FileCorrupt; ++r.corruptRejected; continue; }
                if (badHead) { fs::remove(part, ec); lastErr = "the server's answer did not match the content list"; lastReason = Reason::FileCorrupt; continue; }
                if (g.aborted || g.reached) {
                    if (status == 416) { fs::remove(part, ec); lastErr = "the server could not resume"; lastReason = Reason::Network; continue; }
                    if (status == 404 || (status >= 400 && status < 500 && status != 408 && status != 429)) {
                        Fail(r, Reason::ServerMissingFile, "The server does not have '" + e.path + "' (status " + std::to_string(status) + ").");
                        return false;
                    }
                    if (status != 200 && status != 206) { lastErr = "server answered " + std::to_string(status); lastReason = Reason::Network; continue; }
                } else { lastErr = g.error; lastReason = Reason::Network; }
                have = fs::is_regular_file(part, ec) ? fs::file_size(part, ec) : 0;
                if (have < e.size) continue;   // cut short: the next attempt resumes from here
            }
            // size matches: verify the bytes
            if (detail::FileSha256Hex(part) != e.sha256) {
                fs::remove(part, ec);
                ++r.corruptRejected;
                lastErr = "the downloaded bytes do not match the signed checksum";
                lastReason = Reason::FileCorrupt;
                Log("content sync: rejected '" + e.path + "': " + lastErr);
                continue;
            }
            fs::create_directories(dest.parent_path(), ec);
            fs::remove(dest, ec);
            fs::rename(part, dest, ec);
            if (ec) { Fail(r, Reason::Disk, "Could not move a verified file into place: " + ec.message()); return false; }
            ++r.filesDownloaded;
            r.bytesDownloaded += e.size;
            return true;
        }
        Fail(r, lastReason, "Could not get '" + e.path + "': " + lastErr + ". The previous content stays in use.");
        return false;
    }
};

}  // namespace okaycontent
