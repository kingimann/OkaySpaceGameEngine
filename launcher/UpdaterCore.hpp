// The launcher updater's per-file rules, kept free of SDL / ImGui so they can be tested with a fake server and temp folders
// (tests/test_launcher_versions.cpp). The rules:
//   * a file is only downloaded when versions.json lists a SHA-256 for it; no checksum means REFUSE (never "skip the check");
//   * every file is downloaded to <dest>.new and verified (size, then SHA-256) before anything is replaced;
//   * the live file is swapped only after every file was staged and verified, and a failed swap puts the old file back;
//   * the swap of several files is all or nothing: when one fails the ones already swapped are put back from their backups (CommitAll);
//   * every failure comes back as a plain-words reason the launcher shows; nothing here swallows an error.
#pragma once
#include "VersionsManager.hpp"
#include "AppRestart.hpp"
#include "TestBuild.hpp"   // DisplayVersion: "v2.903.8.115 (test build)"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace okayupdater {
namespace fs = std::filesystem;

struct Outcome {
    bool ok = false;
    std::string reason;                  ///< "" when ok, else plain words naming the file and why
};

/// Fetch `url` into `dest`; on failure put the reason ("404 not found", "connection refused"...) in `error`.
using FetchFn = std::function<bool(const std::string& url, const fs::path& dest, std::string& error)>;

inline const char* kRepublishHint =
    "Nothing was installed and your current version is unchanged. This release has no usable checksums, so it can't be checked: "
    "tell the publisher to publish it again (scripts/publish-supabase.sh), or download the full installer.";

/// Why the index cannot be used for updating `version` ("" = it can). Called before any download.
inline std::string IndexProblem(const okayversions::Index& index, const okayversions::Entry* entry, const std::string& version) {
    if (index.signatureRefused) return index.error;   // already says "Update refused: ..." and what to do
    if (!index.ok)
        return "The checksum list (versions.json) could not be loaded" +
               (index.error.empty() ? std::string(".") : " (" + index.error + ").") +
               " Updates are never installed without checksums, so nothing was installed and your current version is unchanged. "
               "Check your connection and try again.";
    if (index.fallback)
        return "The release server has no checksum list (versions.json) for v" + version + ". " + kRepublishHint;
    if (!entry)
        return "The checksum list (versions.json) does not list v" + version + ", so its files cannot be checked. " + kRepublishHint;
    return {};
}

/// Why `check` is not good enough to install `name` ("" = fine): the index must list a 64-hex SHA-256 for it.
inline std::string RequireChecksum(const std::string& name, const okayversions::FileInfo* check) {
    if (!check) return name + ": versions.json does not list this file. " + kRepublishHint;
    if (check->sha256.size() != 64) return name + ": versions.json has no valid SHA-256 for this file. " + kRepublishHint;
    return {};
}

/// Download `name` to `dest`.new and check it against `check`. On any failure the staged file is removed and `dest` is untouched.
inline Outcome Stage(const std::string& name, const std::string& url, const fs::path& dest,
                     const okayversions::FileInfo* check, const FetchFn& fetch) {
    Outcome o;
    fs::path tmp = dest; tmp += ".new";
    std::error_code ec;
    if (const std::string why = RequireChecksum(name, check); !why.empty()) { o.reason = why; return o; }
    fs::remove(tmp, ec);
    std::string err;
    if (!fetch(url, tmp, err)) {
        fs::remove(tmp, ec);
        o.reason = "Could not download " + name + (err.empty() ? std::string() : " (" + err + ")") +
                   ". Nothing was installed and your current version is unchanged.";
        return o;
    }
    const std::string why = okayversions::VerifyFile(tmp, check->size, check->sha256);
    if (!why.empty()) {
        fs::remove(tmp, ec);
        o.reason = "The downloaded " + name + " is not the published file (" + why +
                   "). Nothing was installed and your current version is unchanged.";
        return o;
    }
    o.ok = true;
    return o;
}

/// Swap the staged `dest`.new in. The old file moves to `dest`.old first and is put back if the swap fails.
inline Outcome Commit(const std::string& name, const fs::path& dest, std::uint64_t minBytes, bool keepBackup) {
    Outcome o;
    fs::path tmp = dest; tmp += ".new";
    fs::path old = dest; old += ".old";
    const okay::restart::SwapResult sw = okay::restart::SwapFile(dest, tmp, old, minBytes, keepBackup);
    std::error_code ec;
    if (!sw.ok) {
        fs::remove(tmp, ec);
        o.reason = "Could not replace " + name + ": " + sw.error + ". The previous file was kept.";
        return o;
    }
    o.ok = true;
    return o;
}

inline void Discard(const fs::path& dest);

/// Like Stage, for a file that may be downloadable from several addresses (the version list's plan): each address is tried in order, and each
/// download is checked. `check` must carry the size and SHA-256 from the index.
inline Outcome StageFromUrls(const std::string& name, const std::vector<std::string>& urls, const fs::path& dest,
                             const okayversions::FileInfo* check, const FetchFn& fetch) {
    Outcome last;
    last.reason = "There is nowhere to download " + name + " from. Nothing was installed and your current version is unchanged.";
    for (const std::string& u : urls) {
        last = Stage(name, u, dest, check, fetch);
        if (last.ok) return last;
        if (const std::string why = RequireChecksum(name, check); !why.empty()) return last;   // no address can fix a missing checksum
    }
    return last;
}

/// One file of a multi-file update.
struct CommitItem {
    std::string name;
    fs::path dest;
    bool keepBackup = false;   ///< keep dest.old after a successful commit (the running program, until the new one is confirmed)
};

/// Swap every staged file in, in the order given (put the riskiest first: the running program). If one fails, the ones already swapped are
/// put back from their backups and every staged file is removed, so the install is exactly as it was. The reason says what failed and
/// whether anything could not be put back. Backups of files that are not kept are deleted only after ALL of them were swapped.
inline Outcome CommitAll(const std::vector<CommitItem>& items, std::uint64_t minBytes) {
    Outcome o;
    std::error_code ec;
    std::vector<const CommitItem*> done;
    for (const CommitItem& it : items) {
        const okay::restart::SwapResult sw = okay::restart::SwapFile(
            it.dest, fs::path(it.dest) += ".new", fs::path(it.dest) += ".old", minBytes, /*keepBackup=*/true);
        if (sw.ok) { done.push_back(&it); continue; }
        std::string restoreNote;
        for (auto r = done.rbegin(); r != done.rend(); ++r) {
            fs::path old = (*r)->dest; old += ".old";
            std::error_code e2;
            if (fs::exists(old, e2)) {
                fs::rename(old, (*r)->dest, e2);   // (replaces the new file: a rename over an existing file is atomic on every OS we ship)
                if (e2) restoreNote += " " + (*r)->name + " could not be put back (" + e2.message() + "); the previous one is " + old.filename().string() + ".";
            } else {
                fs::remove((*r)->dest, e2);
            }
        }
        for (const CommitItem& any : items) Discard(any.dest);
        o.reason = "Could not replace " + it.name + ": " + sw.error + "." +
                   (done.empty() ? std::string(" Nothing was changed.") : restoreNote.empty() ? std::string(" The files already replaced were put back; nothing was changed.") : restoreNote);
        return o;
    }
    for (const CommitItem* d : done) {
        if (d->keepBackup) continue;
        fs::path old = d->dest; old += ".old";
        fs::remove(old, ec);
    }
    o.ok = true;
    return o;
}

/// Remove a staged file that will not be committed.
inline void Discard(const fs::path& dest) {
    fs::path tmp = dest; tmp += ".new";
    std::error_code ec;
    fs::remove(tmp, ec);
}

// ---- which version a program really is -------------------------------------------------------------------------------------
// The launcher, the editor and the player must be one version after an update (owner, 2026-11-25: "the launcher is on 115 but the editor is
// on 114"). VERSION.txt only says what the last update wrote, so the programs themselves are asked: every Windows program we build carries a
// version resource (cmake/okay_version.rc.in) whose "ProductVersion" string is the full version, a test build's four numbers included.
// Written from the documented layout of a version resource's String block: WORD wLength, WORD wValueLength, WORD wType (1 = text), the key
// in UTF-16LE ending in a 0, padding to the next 4-byte boundary, then the value in UTF-16LE ending in a 0. No Windows API, so it is the same
// code (and the same tests) on every platform. A program without one (a Linux build) reads as "" = not known, never as wrong.

/// The ProductVersion in `bytes` (normalized, "" when there is none). `base` is the file offset of bytes[0] (a multiple of 4): the padding
/// after the key is counted from the file, as Windows lays it out. The LAST well-formed one wins.
inline std::string ProgramVersionInBytes(std::string_view bytes, std::uint64_t base = 0) {
    static const char kKey[] = "ProductVersion";
    std::string needle;
    for (const char* k = kKey; *k; ++k) { needle += *k; needle += '\0'; }
    needle += std::string(2, '\0');                                   // the key's terminating 0 (UTF-16)
    std::string found;
    for (std::size_t at = bytes.find(needle); at != std::string_view::npos; at = bytes.find(needle, at + 2)) {
        if (at < 6 || ((base + at) % 2) != 0) continue;
        const auto word = [&](std::size_t i) { return (unsigned)(unsigned char)bytes[i] | ((unsigned)(unsigned char)bytes[i + 1] << 8); };
        const std::size_t start = at - 6;                             // the String block: wLength, wValueLength, wType, then the key
        const unsigned wLength = word(start), wValueLength = word(start + 2), wType = word(start + 4);
        // (wValueLength is counted in characters by rc.exe and in bytes by some other resource compilers: only "some value" is required of it)
        if (wType != 1 || wValueLength == 0 || wLength < 6 + needle.size() + 4) continue;
        std::size_t v = at + needle.size();
        while ((v - start) % 4 != 0) ++v;                              // padding to a 32-bit boundary from the block's start
        std::string text;
        bool good = false;                                             // true once the value's terminating 0 is seen
        for (std::size_t i = 0; i < 64 && v + 1 < bytes.size(); ++i, v += 2) {
            const unsigned c = word(v);
            if (c == 0) { good = true; break; }
            if (c > 0x7E || !(std::isdigit((int)c) || c == '.')) break;
            text += (char)c;
        }
        const std::string n = okayver::NormalizeVersion(text);
        if (good && !n.empty() && n == text) found = n;
    }
    return found;
}

/// The ProductVersion of the program at `exe` ("" = no file, no version resource). Reads the file in 4 MiB pieces (overlapping, so a block
/// across two pieces is still seen).
inline std::string ProgramVersion(const fs::path& exe) {
    std::ifstream in(exe, std::ios::binary);
    if (!in) return {};
    constexpr std::size_t kPiece = 4u << 20, kOverlap = 512;          // kPiece - kOverlap is a multiple of 4: offsets stay aligned
    std::string buf(kPiece, '\0'), found;
    std::uint64_t base = 0;
    std::size_t have = 0;
    for (;;) {
        in.read(&buf[have], (std::streamsize)(kPiece - have));
        const std::size_t got = have + (std::size_t)in.gcount();
        if (got == 0) break;
        const std::string v = ProgramVersionInBytes(std::string_view(buf.data(), got), base);
        if (!v.empty()) found = v;
        if (!in || got < kPiece) break;
        std::memmove(&buf[0], &buf[kPiece - kOverlap], kOverlap);   // keep the tail: a block may straddle the two pieces
        base += kPiece - kOverlap;
        have = kOverlap;
    }
    return found;
}

/// The programs whose real version is not `installed` (what the launcher says is installed), in the order given: {name, its version}.
/// A program whose version is not known ("") is never in the list.
inline std::vector<std::pair<std::string, std::string>> MismatchedPrograms(const std::string& installed,
                                                                          const std::vector<std::pair<std::string, std::string>>& programs) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string want = okayver::NormalizeVersion(installed);
    if (want.empty()) return out;
    for (const auto& p : programs)
        if (!p.second.empty() && okayver::NormalizeVersion(p.second) != want) out.push_back(p);
    return out;
}

/// One plain sentence for a launcher whose editor or player is another version ("" when nothing differs), with what fixes it.
inline std::string MismatchSentence(const std::string& installed, const std::vector<std::pair<std::string, std::string>>& mismatched) {
    if (mismatched.empty()) return {};
    std::string what;
    for (std::size_t i = 0; i < mismatched.size(); ++i) {
        const std::string& n = mismatched[i].first;
        const std::string who = n == "OkayEngine.exe" ? "the editor" : n == "OkaySpacePlayer.exe" ? "the player" : n;
        what += (i == 0 ? "" : i + 1 == mismatched.size() ? " and " : ", ") + who + " is still " + okay::testbuild::DisplayVersion(mismatched[i].second);
    }
    return "This launcher is " + okay::testbuild::DisplayVersion(installed) + ", but " + what +
           ": the last update didn't replace it. Close the editor and use Repair (Versions page) to download " +
           okay::testbuild::DisplayVersion(installed) + " again; your projects are not touched.";
}

}  // namespace okayupdater
