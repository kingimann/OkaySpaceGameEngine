#pragma once
// The self-updater and runtime fetcher (Updater.cpp, namespace updater): check the releases bucket for a newer editor,
// download it (checked against the published checksum list) and hand the swap to a detached helper; Build Game fetches
// the player runtime with the same download helpers. Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 2).
#include <filesystem>
#include <string>

namespace oked {
namespace updater {

// Result of an update check: the running version, the published version, the
// release notes (if any) and whether an upgrade is actually available.
struct UpdateInfo {
    bool checked = false;
    bool available = false;
    std::string current, latest, notes, error;
    std::string ref = "main";   // commit the check resolved to (install uses the same)
};

// Set true after a successful self-update relaunch: the editor's main loop then quits.
extern bool pendingQuit;
// Absolute path of the running executable.
std::string SelfPath();
// Download a URL to a file (own HTTP client when on, then curl, then a platform fallback); no-cache, cache-busting query.
bool Download(const std::string& url, const std::filesystem::path& outPath);
// URL of a published file in the releases bucket ("sb:<ver>" = that version's folder, anything else = the newest).
std::string RawUrl(const std::string& ref, const std::string& name);
// Ask the releases bucket for the published version and release notes (no files are swapped).
UpdateInfo CheckLatest();
// Launch an executable detached from this process.
void Relaunch(const std::string& path);
// Download one published file of `version` to `dest`.new and check it against the release server's checksum list.
bool FetchVerified(const std::string& version, const std::string& asset, const std::filesystem::path& dest, std::string& reason);
// Download the published editor, stage the swap with a detached helper and relaunch. Returns a status line.
std::string InstallUpdate(const std::string& latest, const std::string& ref = "main");

} // namespace updater
} // namespace oked
