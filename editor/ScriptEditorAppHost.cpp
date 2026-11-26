// The Script Editor app inside the editor (ScriptEditorAppHost.hpp): one shared ScriptIdeApp, fed with the editor's data through
// its Config hooks. No second script editor is written here; this file only connects the shared one to the editor.
#include "ScriptEditorAppHost.hpp"
#include "ScriptAssist.hpp"        // LanguageWordsFor, OkayScriptPalette: the editor's words, hover docs and colours (imgui.h before OkayUI's headers)
#include "ScriptIdeApp.hpp"
#include "OkayPanels.hpp"          // DrawAppWindowOkayUI: a whole OkayUI app in an editor window
#include "PluginRegistry.hpp"      // ScriptEditorPlugins, ScriptProblems: the editor plugins' lint
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "okay/Platform/Platform.hpp"   // PrefPath: the settings folder the Script Editor program uses too

#include <memory>

namespace oked {

bool g_showScriptEditorApp = false;

namespace {
scriptide::ScriptIdeApp& App() {
    static std::unique_ptr<scriptide::ScriptIdeApp> app;
    if (!app) {
        scriptide::ScriptIdeApp::Config cfg;
        if (okay::platform::Backend* be = okay::platform::Default()) {   // the program's settings file: one view for both hosts
            const std::string data = be->PrefPath("OkaySpace", "ScriptEditor");
            if (!data.empty()) cfg.settingsPath = data + "script_editor.txt";
        }
        cfg.idPrefix = "##editor_script_editor_app";
        cfg.wordsFor = [](const std::string& language) { return &LanguageWordsFor(language); };
        cfg.palette = &OkayScriptPalette();
        cfg.moreProblems = [](const std::string& language, const std::string& path, const std::string& text,
                              std::vector<scriptide::Diagnostic> own) {
            pluginreg::DocContext ctx;
            ctx.language = language; ctx.path = path; ctx.text = text;
            return pluginreg::ScriptProblems(pluginreg::ScriptEditorPlugins(), ctx, std::move(own));
        };
        cfg.hostName = "Scene Script Editor";
        cfg.showInHost = [](const std::string& path, int line) { OpenScriptFileInEditorAt(path, line); };
        cfg.saved = [](const std::string& path, const std::string&) { ConsoleLog("[Script Editor] saved " + path); };
        app.reset(new scriptide::ScriptIdeApp(cfg));
    }
    return *app;
}
} // namespace

void DrawScriptEditorApp(okay::editor::EditorState& ed) {
    if (!g_showScriptEditorApp) return;
    scriptide::ScriptIdeApp& app = App();
    // the open project is the app's folder (a project opened later moves it along; a folder opened in the app stays until then)
    static std::string followed;
    if (ed.projectDir() != followed) {
        followed = ed.projectDir();
        if (!followed.empty()) app.OpenFolder(followed);
    }
    DrawAppWindowOkayUI("Script Editor (Files)", &g_showScriptEditorApp, "##okayui_script_editor_app",
                        [&app](float x, float y, float w, float h) { app.Frame(x, y, w, h); }, 980.0f, 640.0f);
}

} // namespace oked
