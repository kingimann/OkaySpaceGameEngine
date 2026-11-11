// The launcher updater's per-file rules, kept free of SDL / ImGui so they can be tested with a fake server and temp folders
// (tests/test_launcher_versions.cpp). The rules:
//   * a file is only downloaded when versions.json lists a SHA-256 for it; no checksum means REFUSE (never "skip the check");
//   * every file is downloaded to <dest>.new and verified (size, then SHA-256) before anything is replaced;
//   * the live file is swapped only after every file was staged and verified, and a failed swap puts the old file back;
//   * every failure comes back as a plain-words reason the launcher shows; nothing here swallows an error.
#pragma once
#include "VersionsManager.hpp"
#include "AppRestart.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace okayupdater {
namespace fs = std::filesystem;

struct Outcome {
    bool ok = false;
    std::string reason;                  ///< "" when ok, else plain words naming the file and why
};

/// Fetch `url` into `dest`; on failure put the reason ("404 not found", "connection refused"...) in `error`.
using FetchFn = std::function<bool(const std::string& url, const fs::path& dest, std::string& error)>;

inline const char* kRepublishHint =
    "The release is missing its checksum list (versions.json). Nothing was installed and your current version is unchanged. "
    "Ask the publisher to re-run the release publish (scripts/publish-supabase.sh), or download the full installer.";

/// Why the index cannot be used for updating `version` ("" = it can). Called before any download.
inline std::string IndexProblem(const okayversions::Index& index, const okayversions::Entry* entry, const std::string& version) {
    if (!index.ok)
        return "The checksum list (versions.json) could not be loaded" +
               (index.error.empty() ? std::string(".") : " (" + index.error + ").") +
               " Updates are never installed without checksums, so nothing was installed and your current version is unchanged. "
               "Check your connection and try again.";
    if (!entry)
        return "versions.json does not list v" + version + ", so its files cannot be checked. " + kRepublishHint;
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

/// Remove a staged file that will not be committed.
inline void Discard(const fs::path& dest) {
    fs::path tmp = dest; tmp += ".new";
    std::error_code ec;
    fs::remove(tmp, ec);
}

}  // namespace okayupdater
