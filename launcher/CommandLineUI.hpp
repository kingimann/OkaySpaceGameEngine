#pragma once
// The launcher's Command line page (sidebar: Create > Command line) and the "Open command line" action. The rules live in
// CommandLine.cpp (no ImGui, tested headless); this file only draws them. main.cpp calls Tick() once a frame, DrawPage()
// while the page shows and Shutdown() before it closes, and hands over what the page needs through Host, so nothing here
// reaches into main.cpp.
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

namespace okaylcli {

struct ProjectRef {
    std::string name, root;   ///< root: the project folder (the one holding Assets)
};

struct Host {
    std::string okayExe;                    ///< the okay program the launcher ships in Tools ("" = not found)
    std::string toolsDir;                   ///< put first on PATH in a terminal opened from here
    std::vector<ProjectRef> projects;       ///< the Create page's projects, for the picker
    ImFont* mono = nullptr;                 ///< the output's font (the launcher's JetBrains Mono); null = the default font
    ImVec4 accent{0.26f, 0.59f, 0.98f, 1.0f};
    ImVec4 cardBg{0.15f, 0.163f, 0.2f, 1.0f};
    std::function<void(const std::string& message, int kind)> toast;   ///< kind: 0 info, 1 done, 2 error
};

/// The project commands run in (the project card's "Command line" sets it before showing the page).
void SetProject(const std::string& root, const std::string& name);
/// The "Open command line" action: a terminal window in `root` with the Tools folder first on PATH. Says how it went
/// through host.toast.
void OpenTerminalFor(const Host& host, const std::string& root, const std::string& name);
/// Once a frame, whichever page shows: reads a running command's output.
void Tick();
/// The page body, under the launcher's section header.
void DrawPage(const Host& host);
/// Ends a running command. Call before the launcher closes.
void Shutdown();

/// The one-line hint under the project picker (also read by the test).
const char* HintText();

}  // namespace okaylcli
