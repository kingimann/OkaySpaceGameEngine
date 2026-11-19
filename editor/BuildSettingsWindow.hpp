#pragma once
// The Build Settings window's drawing (BuildSettingsWindow.cpp). The logic it shows lives in
// BuildSettingsModel.hpp (no ImGui, tested by tests/test_build_settings.cpp); this file only draws it.
// main.cpp keeps the older page bodies (Scenes, Player, Display, Graphics, Audio & Input, Output, Server, Xbox)
// and calls these for the left list, the page header, the Game and Advanced pages, the summary and checks.
#include <functional>
#include <string>
#include <vector>
#include "BuildSettingsModel.hpp"
#include "imgui.h"

namespace oked {

/// What the window needs from the editor each frame.
struct BuildWindowCtx {
    BuildSettings* settings = nullptr;
    char*  name = nullptr;      size_t nameCap = 0;     // the product name box
    char*  dir = nullptr;       size_t dirCap = 0;      // the output folder box
    int    platform = 0;        // 0 Desktop, 1 Web, 2 Android, 3 Server, 4 Xbox
    int    enabledScenes = 0;   // scenes ticked in the Scenes list
    std::uintmax_t estimateBytes = 0;   // what the last Dry Run reported
    bool   openSceneSaved = true;
    std::string projectDir;
    std::function<void(const std::string&)> openFolder;   // "Open output folder"
};

/// The left list: a search box, then the pages (a dot marks a page with changed settings) or the search results.
/// `page` is the chosen page (a bsm::Page id). Call inside a child window.
void DrawBuildNav(BuildWindowCtx& c, int& page);

/// Top of every page: its one-line tip and "Reset page to defaults".
void DrawBuildPageHeader(BuildWindowCtx& c, int page);

/// The Game and Advanced pages. Returns false for any other page (main.cpp draws those).
bool DrawBuildPageExtra(BuildWindowCtx& c, int page);

/// More settings at the END of the older pages (Graphics, Audio & Input, Scenes, Output): the ones added after those pages were written.
void DrawBuildPageMore(BuildWindowCtx& c, int page);

/// "What will happen" in plain words, and the checks with Fix buttons. Returns true if a check found an error.
bool DrawBuildSummary(BuildWindowCtx& c);

/// Under the profile bar: Debug / Release presets, Duplicate, save to / load from a .okaybuild file in the project.
/// `onChanged` is called when the profile list changed and must be saved.
void DrawBuildProfileExtras(BuildWindowCtx& c, std::vector<BuildProfile>& profiles, int& selected, const std::function<void()>& onChanged);

/// Keep the last build's log for the Advanced page (lines coloured: errors red, warnings yellow, good green).
void SetBuildLog(const std::string& status, const std::vector<std::string>& warnings);

/// Record a finished build in the project's history (shown on the Advanced page and in the summary).
void RecordBuildInHistory(const BuildWindowCtx& c, int buildNumber, std::uintmax_t bytes, bool ok);

}  // namespace oked
