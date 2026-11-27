#pragma once
// The launcher's Command line page (sidebar: Create > Command line) and the "Open command line" action. The rules live in
// CommandLine.cpp and the terminal emulator in Terminal.cpp (no ImGui, tested headless); this file only draws them: tabs
// ("+", close, rename), each with its shell (PowerShell, Command Prompt or the person's shell, or "okay only") and its
// project folder; the terminal grid in the launcher's mono font with a blinking cursor, mouse selection, the scrollback,
// a right-click menu (Copy, Paste, Select all, Clear) and Ctrl+= / Ctrl+- for the text size. main.cpp calls Tick() once a frame, DrawPage()
// while the page shows and Shutdown() before it closes, and hands over what the page needs through Host, so nothing here
// reaches into main.cpp.
#include <functional>
#include <string>
#include <vector>

#include "okay/UI/OkayUIImGuiNames.hpp"   // OkayUI, in Dear ImGui's names
#include "CommandLine.hpp"   // FoundProgram

namespace okaylcli {

struct ProjectRef {
    std::string name, root;   ///< root: the project folder (the one holding Assets)
};

struct Host {
    std::string okayExe;                    ///< the okay program the launcher ships in Tools ("" = not found)
    std::string toolsDir;                   ///< put first on PATH in a terminal opened from here
    std::vector<ProjectRef> projects;       ///< the Create page's projects, for the picker
    std::vector<FoundProgram> programs;     ///< the programs of their own found beside the launcher (FindPrograms)
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
/// Once a frame, whichever page shows: reads every tab's output (shells and okay commands keep running in the background).
void Tick();
/// One row above the page: a button for each program of its own that is installed (host.programs; none: nothing is drawn).
/// The program starts in the page's project folder.
void DrawProgramsRow(const Host& host);
/// Starts one of host.programs (found by FindPrograms) in a window of its own, without a shell, in the picked project's
/// folder, and toasts the result. The Command line page's row and the launcher's side menu both use it.
bool OpenProgram(const Host& host, const FoundProgram& p);
/// The page body, under the launcher's section header.
void DrawPage(const Host& host);
/// Ends every tab's shell and running command. Call before the launcher closes.
void Shutdown();

/// The one-line hint under the project picker in okay-only mode, and in a shell tab.
const char* HintText();
const char* ShellHintText();

}  // namespace okaylcli
