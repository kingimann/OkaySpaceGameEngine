// See OkayUIHostPanels.hpp. This file is built as its own object library against okayui/probe first on the include path (so `#include <imgui.h>` in the panel
// sources is OkayUI's compatibility header and no Dear ImGui is involved), without OKAY_HAVE_OKAYUI. The panel sources are #included below, after every
// header they need was included at global scope, with the names they DEFINE renamed (the *_WM macros) so they cannot collide with the Dear ImGui editor's
// copy of the same files in the same program. Names they only USE (g_paused, SaveSettings, ...) stay as they are and resolve to the editor's own.
#include "OkayUIHostPanels.hpp"
#include "DialogService.hpp"

#undef OKAY_HAVE_OKAYUI   // the panel sources must take their Dear ImGui API path (compiled against OkayUI): never the hosted-in-ImGui OkayUI path
#define OKAYUI_STABLE_PANEL_TITLES 1   // ConsolePanel.cpp: no error counts in the window title (the dock layout keys windows by title)

// ProjectPanelUI.cpp draws its import settings with the OkayUI screen okayuiscreens::DrawImportSettings, a name the macro below renames
// for this copy: the renamed name forwards to the screen (defined before the macro, so it reaches the real one).
#include "OkayUIEditorPanels.hpp"
namespace okayuiscreens {
inline ImportSettingsResult DrawImportSettings_WM(okaypanels::ImportSettingsModel& m, okaypanels::ImportSettingsState& st, float x, float y, float w, float h) {
    return DrawImportSettings(m, st, x, y, w, h);
}
} // namespace okayuiscreens

// ---- the names the panel sources define, renamed for this copy: from here on, so the declarations in the editor headers (with their default arguments) are renamed too ----
#define ConsoleLog ConsoleLog_WM
#define DrawConsole DrawConsole_WM
#define ConsoleClear ConsoleClear_WM
// the three calls to the person: in this copy they go to the current DialogService (editor/DialogService.hpp), which window mode points at OkayUI popups
#define ShowErrorPopup ShowErrorPopup_WM
#define RunAfterSavePrompt RunAfterSavePrompt_WM
#define OpenScriptFileInEditorAt OpenScriptFileInEditorAt_WM
#define ConsoleSetMirror ConsoleSetMirror_WM
#define ConsoleRunLine ConsoleRunLine_WM        // the console command line (claude2/found-console): window mode keeps its own copy
#define ConsoleAttachHost ConsoleAttachHost_WM
#define g_consoleMirror g_consoleMirror_WM
#define g_consoleCounts g_consoleCounts_WM
#define ConsolePreferCategory ConsolePreferCategory_WM
#define DrawTbIcon DrawTbIcon_WM
#define IconButton IconButton_WM
#define SearchField SearchField_WM
#define CategoryColor CategoryColor_WM
#define CategoryGlyph CategoryGlyph_WM
#define SameLineRight SameLineRight_WM
#define TipArrowButton TipArrowButton_WM
#define TipSmallButton TipSmallButton_WM
#define IconLabelButton IconLabelButton_WM
#define AccentToggleButton AccentToggleButton_WM
#define AccentCol AccentCol_WM
#define TipButton TipButton_WM
#define SceneBuildSync SceneBuildSync_WM
#define DrawSceneManager DrawSceneManager_WM
#define DrawDialogs DrawDialogs_WM
#define NamePromptOpen NamePromptOpen_WM
#define ShowProperties ShowProperties_WM
#define ShowReferences ShowReferences_WM
#define HasImportSettings HasImportSettings_WM
#define DrawImportSettings DrawImportSettings_WM
#define ShowReferenceProblems ShowReferenceProblems_WM
#define AskName AskName_WM
#define AskDelete AskDelete_WM
#define AskFolder AskFolder_WM
#define DrawContextMenu DrawContextMenu_WM
#define DrawPropertyRows DrawPropertyRows_WM

// ---- every header the panel sources include, at global scope, so the includes inside them are no-ops ----
#include "EditorState.hpp"
#include "SceneManagerPanel.hpp"
#include "okay/Platform/Platform.hpp"
#include "EditorCommon.hpp"
#include "ConsolePanel.hpp"
#include "SceneRenameOps.hpp"
#include "EditorWidgets.hpp"
#include "MainShared.hpp"
#include "ProblemsPanel.hpp"
#include "ConsoleScriptLink.hpp"
#include "CrashContext.hpp"
#include "ProjectCreate.hpp"
#include "ProjectBrowser.hpp"
#include "ProjectPanelUI.hpp"
#include "ProjectContextModel.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIImGuiNames.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <unordered_set>

#include "EditorWidgets.cpp"
#include "ConsolePanel.cpp"
#include "SceneManagerPanel.cpp"
#include "ProjectPanelUI.cpp"
#include "ProjectMenuDraw.cpp"

#undef ConsoleLog
#undef DrawConsole
#undef ConsoleClear
#undef ShowErrorPopup
#undef RunAfterSavePrompt
#undef OpenScriptFileInEditorAt
#undef ConsoleSetMirror
#undef ConsoleRunLine
#undef ConsoleAttachHost
#undef g_consoleMirror
#undef g_consoleCounts
#undef ConsolePreferCategory
#undef DrawTbIcon
#undef IconButton
#undef SearchField
#undef CategoryColor
#undef CategoryGlyph
#undef SameLineRight
#undef TipArrowButton
#undef TipSmallButton
#undef IconLabelButton
#undef AccentToggleButton
#undef AccentCol
#undef TipButton
#undef SceneBuildSync
#undef DrawSceneManager
#undef DrawDialogs
#undef NamePromptOpen
#undef ShowProperties
#undef ShowReferences
#undef HasImportSettings
#undef DrawImportSettings
#undef ShowReferenceProblems
#undef AskName
#undef AskDelete
#undef AskFolder
#undef DrawContextMenu
#undef DrawPropertyRows

namespace oked {
// the renamed calls of the panel sources (see the *_WM macros above): to the current DialogService; without one the error still reaches the Console
void ShowErrorPopup_WM(const std::string& title, const std::string& text) {
    if (DialogService* d = CurrentDialogs()) d->ShowError(title, text); else ConsoleLog_WM(title + ": " + text, 2);
}
void RunAfterSavePrompt_WM(okay::editor::EditorState&, const std::string& doing, std::function<void()> action, std::function<void()> onCancel) {
    if (DialogService* d = CurrentDialogs()) d->RunAfterSavePrompt(doing, std::move(action), std::move(onCancel)); else if (action) action();
}
void OpenScriptFileInEditorAt_WM(const std::string& path, int line) {
    if (DialogService* d = CurrentDialogs()) d->OpenScriptAt(path, line);
}
void ConsoleSetMirror(void (*fn)(const std::string&, int));   // ConsolePanel.cpp of the Dear ImGui editor's copy (the declaration in this TU's header was renamed)
namespace wm {

namespace {
ProjectProbe g_probe;

// ---- the Project window: written for window mode over the real Browser and the real dialogs of ProjectPanelUI.cpp ----
struct ProjectWindow {
    std::unique_ptr<okaypb::Browser> browser;
    std::string root, current, selected, status;
    bool started = false;
};

std::string ParentOf(const std::string& p) { const size_t k = p.find_last_of('/'); return k == std::string::npos ? p : p.substr(0, k); }

void DrawProjectWindow(ProjectWindow& pw) {
    if (!pw.browser) return;
    using namespace OkayUI;
    okaypb::Browser& b = *pw.browser;
    if (!pw.started) { b.Sync(); pw.started = true; }
    b.Poll((std::uint64_t)okay::platform::TicksMs());
    if (pw.current.empty()) pw.current = pw.root;
    // the path line, "up", and New Folder
    const bool atRoot = pw.current == pw.root;
    if (atRoot) BeginDisabled(true);
    if (Button("Up")) { pw.current = ParentOf(pw.current); pw.selected.clear(); }
    g_probe.up = GetItemRect();
    if (atRoot) EndDisabled();
    SameLine();
    const bool newFolderClicked = Button("New Folder");
    g_probe.newFolder = GetItemRect(); g_probe.current = pw.current;
    if (newFolderClicked) {
        const std::string dir = pw.current; okaypb::Browser* br = &b; std::string* status = &pw.status;
        projectpanel::AskName_WM(projectpanel::NameRequest{"New folder", "Letters, digits, spaces, - and _", "New Folder", "", "Create",
            [br, dir, status](const std::string& name) -> std::string {
                const okaypb::Result r = br->CreateFolder(dir, name);
                if (!r.ok) return r.error.empty() ? "That did not work." : r.error;
                *status = "Created " + name; return std::string();
            }});
    }
    SameLine();
    TextDisabled(pw.current.size() > pw.root.size() ? pw.current.substr(pw.root.size()).c_str() : "/");
    Separator();
    okaypb::ListOptions opt;
    const std::vector<okaypb::Entry> entries = b.List(pw.current, opt);
    if (entries.empty()) TextDisabled("This folder is empty.");
    bool firstRow = true;
    for (const okaypb::Entry& e : entries) {
        const std::string label = std::string(e.isDir ? "[folder]  " : "          ") + e.name + "##" + e.path;
        const bool sel = pw.selected == e.path;
        if (Selectable(label.c_str(), sel)) pw.selected = e.path;
        if (firstRow) { g_probe.firstRow = GetItemRect(); firstRow = false; }
        if (IsItemHovered() && IsMouseDoubleClicked(0) && e.isDir) { pw.current = e.path; pw.selected.clear(); }
        if (BeginPopupContextItem((std::string("ctx") + e.path).c_str())) {
            pw.selected = e.path;
            if (MenuItem("Rename...")) {
                okaypb::Browser* br = &b; const std::string path = e.path; std::string* status = &pw.status;
                std::string stem = e.name; const size_t dot = e.isDir ? std::string::npos : stem.find_last_of('.');
                const std::string suffix = dot == std::string::npos ? std::string() : stem.substr(dot);
                if (dot != std::string::npos) stem = stem.substr(0, dot);
                projectpanel::AskName_WM(projectpanel::NameRequest{"Rename", "", stem, suffix, "Rename",
                    [br, path, status](const std::string& name) -> std::string {
                        const okaypb::Result r = br->Rename(path, name);
                        if (!r.ok) return r.error.empty() ? "That did not work." : r.error;
                        *status = "Renamed to " + name; return std::string();
                    }});
            }
            EndPopup();
        }
    }
    if (!pw.status.empty()) { Separator(); TextDisabled(pw.status.c_str()); }
    g_probe.namePromptOpen = projectpanel::NamePromptOpen_WM();
    projectpanel::DrawDialogs_WM();   // the name prompt and the other dialogs, when one is open
}

}   // namespace

void ConsoleLog(const std::string& msg, int level) { ConsoleLog_WM(msg, level); }
void ConsoleCounts(int out[3]) { for (int i = 0; i < 3; ++i) out[i] = g_consoleCounts_WM[i]; }
const ProjectProbe& GetProjectProbe() { return g_probe; }

PanelHooks MakeHooks(okay::editor::EditorState* ed, const std::string& projectDir, HostModeState* st) {
    PanelHooks h;
    if (st) { h.before = [st] { BeginPanels(*st); }; h.after = [st] { EndPanels(*st); }; }
    h.console = [] { DrawConsole_WM(); };
    oked::ConsoleSetMirror(&ConsoleLog);   // every line the editor's code logs (ShowErrorPopup, tools, scripts) also lands in this Console
    if (ed) h.sceneManager = [ed] { DrawSceneManager_WM(*ed); };
    if (!projectDir.empty()) {
        auto pw = std::make_shared<ProjectWindow>();
        std::error_code ec;
        std::filesystem::path assets = std::filesystem::path(projectDir) / "Assets";
        pw->root = (std::filesystem::is_directory(assets, ec) ? assets : std::filesystem::path(projectDir)).generic_string();
        pw->browser = std::make_unique<okaypb::Browser>(pw->root);
        h.project = [pw, st] {
            if (OkayUI::Begin(kPanelProject, st ? &st->OpenRef(kPanelProject) : nullptr, 0)) DrawProjectWindow(*pw);
            OkayUI::End();
        };
    }
    return h;
}

void BeginPanels(HostModeState& st) {
    OkayUI::SetDockAwareBegin(true);   // Begin(title) of a panel whose title is in the dock layout runs as DockBegin
    g_showConsole = st.IsOpen(kPanelConsole);
    g_showScenes = st.IsOpen(kPanelSceneManager);
}

void EndPanels(HostModeState& st) {
    if (st.open.count(kPanelConsole) || !g_showConsole) st.OpenRef(kPanelConsole) = g_showConsole;
    if (st.open.count(kPanelSceneManager) || !g_showScenes) st.OpenRef(kPanelSceneManager) = g_showScenes;
}

} // namespace wm
} // namespace oked
