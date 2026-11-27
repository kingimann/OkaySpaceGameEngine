// Script Editor program (system "Script Editor", docs/feature_map/13-scripting.md 13.5.13): the script editor in a window of its
// own, without the editor. The Script Editor itself (the project tree, tabs, save, Find in Files, Go to File, Local History, the
// checks, settings) is shared/ScriptIdeApp.hpp, the same one the editor shows in its Script Editor window; this file is
// only the program's window around it (OkayUI's window mode on okay::platform, shared/OkayUIFrameLoop.hpp RunProgram; no Dear ImGui).
//
//   okay-script-editor [FOLDER | FILE ...] [--frames N]
//
// FOLDER opens as the project; each FILE opens in a tab (its folder is the project when none was named). With nothing named, the
// folder that was open last. --frames N: end after N frames (a start-and-quit check with no person at the window).
// Settings are kept in the settings folder (okay::platform PrefPath "OkaySpace/ScriptEditor", script_editor.json).
#define SDL_MAIN_HANDLED
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "ScriptIdeApp.hpp"
#include "OkayUIAppKit.hpp"   // ParseProgramArgs: the programs' command line
#include "FileDialog.hpp"   // File > Open Folder > Browse...: the system's folder dialog
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/UI/OkayUI.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    int frames = 0;
    if (!okayuiapp::ParseProgramArgs(argc, argv,
                                     "Usage: %s [FOLDER | FILE ...] [--frames N]\n"
                                     "  FOLDER     open this folder as the project (its scripts in the tree)\n"
                                     "  FILE       open this file in a tab\n"
                                     "  --frames N end after N frames (a start-and-quit check)\n", paths, frames))
        return 0;

    std::unique_ptr<scriptide::ScriptIdeApp> app;
    okayuihost::platform::ProgramDesc desc;
    desc.tag = "script-editor";
    desc.window.title = "Script Editor";
    desc.window.width = 1280;
    desc.window.height = 800;
    desc.window.resizable = true;
    desc.maxFrames = frames;
    okayuihost::platform::ProgramHooks hooks;
    hooks.start = [&](okay::platform::Backend& be, okay::platform::Painter&) {
        OkayUI::UseLauncherLook(true);   // the editor's look: the proportional UI font (the code is in JetBrains Mono)
        scriptide::ScriptIdeApp::Config cfg;
        const std::string data = be.PrefPath("OkaySpace", "ScriptEditor");
        cfg.settingsPath = data.empty() ? std::string() : data + "script_editor.json";
        cfg.quitItem = true;
        cfg.pickFolder = [](const std::string& start, std::function<void(const std::string&)> chosen) {
            okay::filedialog::FolderOne("Open Folder", start, std::move(chosen));
        };
        app.reset(new scriptide::ScriptIdeApp(cfg));
        bool any = false;
        for (const std::string& p : paths) any = app->OpenPath(p) || any;
        if (!any && paths.empty() && !app->Settings().lastFolder.empty()) app->OpenFolder(app->Settings().lastFolder);
        std::fprintf(stderr, "[script-editor] settings %s, folder %s\n", cfg.settingsPath.empty() ? "(not kept)" : cfg.settingsPath.c_str(),
                     app->Folder().empty() ? "(none)" : app->Folder().c_str());
        return true;
    };
    okayuihost::platform::AppHooks(hooks, app);   // the end writes the settings
    return okayuihost::platform::RunProgram(desc, hooks);
}
