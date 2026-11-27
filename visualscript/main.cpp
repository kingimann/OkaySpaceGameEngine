// Visual Script program (system "Visual Script", docs/feature_map/14-visual-scripting.md 14.9): the Visual Script editor in a window
// of its own, without the editor. The editor itself (tabs of .okayvs graphs, the canvas, the Add Block list, Details, Problems,
// the IDE tools, OkayScript, Compare, settings) is shared/VisualScriptApp.hpp, the same one the editor shows in its Visual Script
// window; this file is only the program's window around it (OkayUI's window mode on okay::platform, shared/OkayUIFrameLoop.hpp
// RunProgram; no Dear ImGui).
//
//   okay-visual-script [FOLDER | FILE.okayvs ...] [--frames N]
//
// FOLDER opens as the folder (File > Open lists its graphs, Find References searches it); each FILE opens in a tab (its folder is
// the folder when none was named). With nothing named, the folder and the tabs that were open last. --frames N: end after N
// frames (a start-and-quit check with no person at the window). Settings, the tab list, bookmarks and the Add Block list's
// favourites are kept in the settings folder (okay::platform PrefPath "OkaySpace/VisualScript", visual_script.json).
#define SDL_MAIN_HANDLED
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "VisualScriptApp.hpp"
#include "VisualScriptModel.hpp"   // okay::editor::vsed::VsRestoreTabs, okay::edtabs::UserFilesDir
#include "FileDialog.hpp"          // File > Open > Browse...: the system's file dialog
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
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::printf("Usage: %s [FOLDER | FILE.okayvs ...] [--frames N]\n"
                        "  FOLDER     open this folder (File > Open lists its graphs)\n"
                        "  FILE       open this graph in a tab\n"
                        "  --frames N end after N frames (a start-and-quit check)\n", argv[0]);
            return 0;
        }
        if (a == "--frames" && i + 1 < argc) {
            const long n = std::strtol(argv[++i], nullptr, 10);
            frames = n < 1 ? 1 : (n > 1000000 ? 1000000 : (int)n);
        } else if (!a.empty() && a[0] != '-') paths.push_back(a);
    }

    std::unique_ptr<vsapp::VisualScriptApp> app;
    okayuihost::platform::ProgramDesc desc;
    desc.tag = "visual-script";
    desc.window.title = "Visual Script";
    desc.window.width = 1280;
    desc.window.height = 800;
    desc.window.resizable = true;
    desc.maxFrames = frames;
    okayuihost::platform::ProgramHooks hooks;
    hooks.start = [&](okay::platform::Backend& be, okay::platform::Painter&) {
        OkayUI::UseLauncherLook(true);   // the editor's look: the proportional UI font
        vsapp::VisualScriptApp::Config cfg;
        const std::string data = be.PrefPath("OkaySpace", "VisualScript");
        // the model's per-user files (the open tabs, bookmarks, the Add Block list's favourites) go to the program's own folder
        if (!data.empty()) okay::edtabs::UserFilesDir() = data;
        cfg.settingsPath = data.empty() ? std::string() : data + "visual_script.json";
        cfg.quitItem = true;
        cfg.pickFile = [](const std::string& start, std::function<void(const std::string&)> chosen) {
            okay::filedialog::OpenOne("Open Graph", start, {{"Visual Script graphs", {"*.okayvs"}}}, std::move(chosen));
        };
        app.reset(new vsapp::VisualScriptApp(cfg));
        bool any = false;
        for (const std::string& p : paths) any = app->OpenPath(p) || any;
        if (!any && paths.empty()) {
            if (!app->Settings().lastFolder.empty()) app->OpenFolder(app->Settings().lastFolder);
            okay::editor::vsed::VsRestoreTabs();   // the graphs that were open last
        }
        std::fprintf(stderr, "[visual-script] settings %s, folder %s, %d tab(s)\n", cfg.settingsPath.empty() ? "(not kept)" : cfg.settingsPath.c_str(),
                     app->Folder().empty() ? "(none)" : app->Folder().c_str(), app->TabCount());
        return true;
    };
    okayuihost::platform::AppHooks(hooks, app);   // the end writes the settings
    return okayuihost::platform::RunProgram(desc, hooks);
}
