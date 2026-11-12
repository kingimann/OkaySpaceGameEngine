#pragma once
// The launcher's Versions page (nav: "Versions"): the running version, the Latest / LTS channel, the versions installed next to it
// (size, Remove, default editor), the versions available to install (progress, Cancel), "Keep previous versions: N" and "Open
// install folder". All the rules live in VersionsManager.cpp; this file only draws them and runs the two background jobs (reading
// the version list, and one install). See docs/launcher_versions.md.
//
// main.cpp calls Tick() once per frame, DrawPage() while the page is showing, and Shutdown() before it closes; it hands over what the
// page needs through Host (the launcher's own toast, folder opener, HTTP helpers and colours), so nothing here reaches into main.cpp.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"
#include "VersionsManager.hpp"

namespace okayversionsui {

struct Host {
    std::filesystem::path launcherDir;           ///< <launcher dir>/Versions is where everything goes
    std::string currentVersion;                  ///< the editor in Tools/ (what a plain Open starts)
    std::string launcherVersion;                 ///< this launcher, as built
    okayversions::Sources sources;               ///< the releases bucket, GitHub, ...
    std::string runningEditorVersion;            ///< version of the editor that is open ("" = unknown)
    bool editorRunning = false;
    ImVec4 accent{0.26f, 0.59f, 0.98f, 1.0f}, accentDim{0.19f, 0.43f, 0.72f, 1.0f}, cardBg{0.15f, 0.163f, 0.2f, 1.0f};
    std::function<void(const std::string& message, int kind)> toast;        ///< kind: 0 info, 1 done, 2 error
    std::function<void(const std::string& path)> openFolder;
    std::function<void()> savePrefs;
    std::function<void()> startUpdate;           ///< the launcher's own update (the sidebar's "Install now")
    std::function<void()> channelChanged;        ///< the channel was switched: look for updates again
    okayversions::FetchTextFn fetchText;         ///< a small text file ("" url or unreachable: false)
};

/// `--versions <version>` (or the editor's "Open launcher"): the page is shown and that version highlighted. "" = just the page.
void RequestPage(const std::string& version);
/// True once after RequestPage: the caller switches to the Versions page.
bool TakePageRequest();
/// Once per frame (whatever page is showing): collects a finished download or version-list job and tells the person.
void Tick(const Host& host);
/// The page body, under the launcher's section header.
void DrawPage(const Host& host);
/// A download is running.
bool InstallRunning();
/// Settings > Behavior: the channel as a combo box. True when it changed.
bool DrawChannelCombo(const Host& host, float width);
/// The versions under Versions/ (newest first), cached for a couple of seconds so the project menu can ask every frame.
const std::vector<std::string>& InstalledVersionsCached(const std::filesystem::path& launcherDir);
void InvalidateInstalled();
/// Cancels a running download and joins the threads. Call before the launcher closes.
void Shutdown();

}  // namespace okayversionsui
