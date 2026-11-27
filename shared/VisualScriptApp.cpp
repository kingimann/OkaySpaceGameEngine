// Visual Script app (VisualScriptApp.hpp): files and tabs, the menus, the Details pane, the panel under the canvas (Problems,
// Navigator, OkayScript, Compare), the paused debugger's bar, the dialogs and the status line. The canvas and the Add Block list
// are VisualScriptAppCanvas.cpp. The graphs are the editor's model (editor/VisualScriptModel.hpp); this file only shows them and
// calls the model's rules.
//
// Written from general editor behaviour (tabs of documents, menus, a properties pane, a problems list, a status line) and the
// OkayUI interface; no code, names, icons or look from any other editor (rule 19a).
#include "VisualScriptAppImpl.hpp"

#include "okay/Core/Json.hpp"            // okay::json::Parse / Escape: the settings file

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace vsapp {

// ---- settings ----------------------------------------------------------------------------------------------------------

std::string AppSettings::Serialize() const {
    auto b = [](bool v) { return v ? "true" : "false"; };
    std::string o = "{\n";
    o += std::string("  \"showDetails\": ") + b(showDetails) + ",\n";
    o += std::string("  \"showMinimap\": ") + b(showMinimap) + ",\n";
    o += std::string("  \"snapGrid\": ") + b(snapGrid) + ",\n";
    o += std::string("  \"scrollPans\": ") + b(scrollPans) + ",\n";
    o += "  \"panel\": " + std::to_string(panel) + ",\n  \"lastFolder\": ";
    okay::json::Escape(lastFolder, o);
    o += ",\n  \"recentFiles\": [";
    for (std::size_t i = 0; i < recentFiles.size(); ++i) { if (i) o += ", "; okay::json::Escape(recentFiles[i], o); }
    return o + "]\n}\n";
}

AppSettings AppSettings::Parse(const std::string& text) {
    AppSettings s;
    okay::json::Value root;
    if (!okay::json::Parse(text, root) || !root.IsObject()) return s;
    for (auto [key, field] : {std::make_pair("showDetails", &s.showDetails), std::make_pair("showMinimap", &s.showMinimap),
                              std::make_pair("snapGrid", &s.snapGrid), std::make_pair("scrollPans", &s.scrollPans)})
        if (const okay::json::Value* v = root.Get(key); v && v->IsBool()) *field = v->boolean;
    if (const okay::json::Value* v = root.Get("panel")) {
        std::uint64_t n = 0;
        if (v->AsIndex(n) && n <= 4) s.panel = (int)n;
    }
    if (const okay::json::Value* v = root.Get("lastFolder"); v && v->IsString()) s.lastFolder = v->string;
    if (const okay::json::Value* list = root.Get("recentFiles"); list && list->IsArray())
        for (const okay::json::Value& f : list->array) {
            if (!f.IsString() || f.string.empty() || s.recentFiles.size() >= 10) continue;
            if (std::find(s.recentFiles.begin(), s.recentFiles.end(), f.string) == s.recentFiles.end()) s.recentFiles.push_back(f.string);
        }
    return s;
}

void AppSettings::NoteFile(const std::string& path) {
    if (path.empty()) return;
    recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
    recentFiles.insert(recentFiles.begin(), path);
    if (recentFiles.size() > 10) recentFiles.resize(10);
}

// ---- tabs and files -------------------------------------------------------------------------------------------------------

VisualScriptApp::Impl::Impl(Config c) : cfg(std::move(c)) {
    if (!cfg.settingsPath.empty()) {
        std::ifstream f(fs::u8path(cfg.settingsPath), std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); settings = AppSettings::Parse(ss.str()); }
    }
}

vsed::VsEditState& VisualScriptApp::Impl::st() { return vsed::State(); }

std::string VisualScriptApp::Impl::root() const {
    if (!folder.empty()) return folder;
    const std::string& p = vsed::State().g.path;
    return p.empty() ? std::string(".") : fs::u8path(p).parent_path().u8string();
}

void VisualScriptApp::Impl::note(const std::string& line, int level) {
    notice = line;
    if (cfg.log) cfg.log(line, level);
}

void VisualScriptApp::Impl::saveSettings() {
    if (cfg.settingsPath.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::u8path(cfg.settingsPath).parent_path(), ec);
    std::ofstream f(fs::u8path(cfg.settingsPath), std::ios::binary | std::ios::trunc);
    f << settings.Serialize();
}

bool VisualScriptApp::Impl::openFile(const std::string& path) {
    vsed::OpenFile(path);
    vsed::VsEditState& s = st();
    if (s.missing) { note("Could not open " + path, 2); return false; }
    settings.NoteFile(path);
    saveSettings();
    note("Opened " + FileName(path));
    return true;
}

bool VisualScriptApp::Impl::saveTab(int index) {
    vsed::VsSession& ses = vsed::Session();
    if (index < 0 || index >= (int)ses.tabs.size()) return false;
    vsed::VsEditState& s = *ses.tabs[(std::size_t)index];
    if (s.missing) { note("This graph's file could not be read, so it is not written over", 1); return false; }
    std::string why;
    bool ok = false;
    if (cfg.save) {
        const int was = ses.active;
        ses.active = index;   // the host's save works on the shown tab (the editor's PublishOpenGraph does)
        ok = cfg.save(s, why);
        ses.active = was;
    } else {
        if (s.g.path.empty()) {   // untitled: ask where
            saveAsTab = index; saveAsBox = true; saveAsFocus = true;
            const fs::path dir = (folder.empty() ? fs::current_path() : fs::u8path(folder)) / "Assets";
            std::snprintf(saveAsBuf, sizeof saveAsBuf, "%s", (dir / "Untitled.okayvs").u8string().c_str());
            return false;
        }
        const std::string text = s.g.toText();
        ok = vsed::SaveTextFile ? vsed::SaveTextFile(s.g.path, text) : s.g.save(s.g.path);
        if (ok) { vsed::VsMarkClean(s); vsed::VsSaveTabList(); }
        else why = "it could not be written";
    }
    if (!ok) { note("Could not save " + FileName(s.g.path) + (why.empty() ? std::string() : ": " + why), 2); return false; }
    settings.NoteFile(s.g.path);
    saveSettings();
    note("Saved " + FileName(s.g.path));
    if (cfg.saved) cfg.saved(s.g.path);
    return true;
}

bool VisualScriptApp::Impl::saveTabAs(int index, const std::string& path) {
    vsed::VsSession& ses = vsed::Session();
    if (index < 0 || index >= (int)ses.tabs.size() || path.empty()) return false;
    std::string p = path;
    if (fs::u8path(p).extension().u8string() != ".okayvs") p += ".okayvs";
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    vsed::VsEditState& s = *ses.tabs[(std::size_t)index];
    const std::string was = s.g.path;
    s.g.path = p;
    s.missing = false;
    if (!saveTab(index)) { s.g.path = was; return false; }
    return true;
}

void VisualScriptApp::Impl::close(int index) {
    vsed::VsCloseTab(index, true);
}

void VisualScriptApp::Impl::requestClose(int index) {
    vsed::VsSession& ses = vsed::Session();
    if (index < 0 || index >= (int)ses.tabs.size()) return;
    if (ses.tabs[(std::size_t)index]->g.dirty) { promptOpen = true; promptQuit = false; promptTab = index; return; }
    close(index);
}

void VisualScriptApp::Impl::requestQuit() {
    if (vsed::VsAnyUnsaved()) { promptOpen = true; promptQuit = true; promptTab = -1; return; }
    quit = true;
}

void VisualScriptApp::Impl::answerPrompt(int answer) {
    if (!promptOpen) return;
    promptOpen = false;
    if (answer == 2) return;   // Cancel
    vsed::VsSession& ses = vsed::Session();
    if (promptQuit) {
        if (answer == 0) {
            for (int i = 0; i < (int)ses.tabs.size(); ++i)
                if (ses.tabs[(std::size_t)i]->g.dirty && !saveTab(i)) return;   // a failed (or untitled) save does not quit
        }
        quit = true;
        return;
    }
    if (answer == 0 && !saveTab(promptTab)) return;
    close(promptTab);
}

void VisualScriptApp::Impl::checkNow() {
    vsed::VsEditState& s = st();
    vsed::VsRefreshChecks(s, s.g, s.g.toText(), root());
}

std::vector<std::string> VisualScriptApp::Impl::compareRows() {
    std::vector<std::string> rows;
    const vsed::VsGraphDiff& d = vsed::VsIdeDiffNow(st());
    for (const auto& n : d.added) rows.push_back("+ " + n.type + " (#" + std::to_string(n.id) + ")");
    for (const auto& n : d.removed) rows.push_back("- " + n.type + " (#" + std::to_string(n.id) + ")");
    for (const auto& n : d.changed) rows.push_back("~ " + n.type + ": " + n.before + " -> " + n.after);
    for (const auto& n : d.moved) rows.push_back("> " + n.type + " moved");
    for (const auto& w : d.wiresAdded) rows.push_back("+ wire " + w);
    for (const auto& w : d.wiresRemoved) rows.push_back("- wire " + w);
    for (const auto& v : d.varsAdded) rows.push_back("+ variable " + v);
    for (const auto& v : d.varsRemoved) rows.push_back("- variable " + v);
    for (const auto& v : d.varsChanged) rows.push_back("~ variable " + v);
    return rows;
}

bool VisualScriptApp::Impl::beginDialog(const std::string& id, float X, float Y, float W, float H, float bw, float bh, float& bx, float& by) {
    UI::BareNextWindow();
    if (!UI::Begin(id.c_str(), X, Y, W, H)) return false;
    const auto& dl = UI::GetWindowDrawList();
    dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), Rgb(0, 0, 0, 90));
    bw = std::min(bw, W - 20.0f);
    const float x0 = X + (W - bw) * 0.5f, y0 = Y + std::max(20.0f, std::min(80.0f, (H - bh) * 0.3f));
    dl->AddRectFilled(UI::Vec2(x0, y0), UI::Vec2(x0 + bw, y0 + bh), kPanel, 6.0f);
    dl->AddRect(UI::Vec2(x0, y0), UI::Vec2(x0 + bw, y0 + bh), kLine, 6.0f, 1.0f);
    bx = x0 + 14.0f;
    by = y0 + 12.0f;
    return true;
}

// ---- the menus ------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawMenu(float X, float Y, float W, float h) {
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_menu").c_str(), X, Y, W, h)) {
        UI::GetWindowDrawList()->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
        vsed::VsEditState& s = st();
        vsed::VsSession& ses = vsed::Session();
        const bool any = !s.sel.empty() || s.selected >= 0;
        auto cmd = [&](const char* title, const char* id, const char* keys, bool enabled = true) {
            if (UI::MenuItem(title, keys, false, enabled) && !vsed::VsRunCommand(s, id, root())) note(std::string(title) + ": nothing to do here", 1);
        };
        if (UI::BeginMenuBar()) {
            if (UI::BeginMenu("File")) {
                if (UI::MenuItem("New Graph", "Ctrl+N")) vsed::NewGraph(true);
                if (UI::MenuItem("New Empty Graph")) vsed::NewGraph(false);
                if (UI::MenuItem("Open...", "Ctrl+O")) { openBox = true; openFocus = true; openBuf[0] = 0; }
                if (UI::BeginMenu("Open Recent")) {
                    if (settings.recentFiles.empty()) UI::MenuItem("(none yet)", nullptr, false, false);
                    for (const std::string& f : std::vector<std::string>(settings.recentFiles))
                        if (UI::MenuItem(f.c_str())) openFile(f);
                    UI::EndMenu();
                }
                UI::MenuSeparator();
                if (UI::MenuItem("Save", "Ctrl+S")) saveTab(ses.active);
                if (UI::MenuItem("Save As...")) {
                    saveAsTab = ses.active; saveAsBox = true; saveAsFocus = true;
                    std::snprintf(saveAsBuf, sizeof saveAsBuf, "%s", s.g.path.c_str());
                }
                if (UI::MenuItem("Save All", "Ctrl+Shift+S")) for (int i = 0; i < (int)ses.tabs.size(); ++i) if (ses.tabs[(std::size_t)i]->g.dirty) saveTab(i);
                if (UI::MenuItem("Compare with Saved File", nullptr, false, !s.g.path.empty())) {
                    std::ifstream f(fs::u8path(s.g.path), std::ios::binary);
                    std::stringstream ss; ss << f.rdbuf();
                    vsed::VsShowDiff(s.g.path, "the saved file", ss.str());
                    settings.panel = 4;
                }
                UI::MenuSeparator();
                if (UI::MenuItem("Close Tab", "Ctrl+W")) requestClose(ses.active);
                if (UI::MenuItem("Reopen Closed Tab", "Ctrl+Shift+T", false, !okay::editor::docrec::ClosedStack().empty())) vsed::VsReopenClosedGraph();
                if (cfg.quitItem) { UI::MenuSeparator(); if (UI::MenuItem("Quit", "Ctrl+Q")) requestQuit(); }
                UI::EndMenu();
            }
            if (UI::BeginMenu("Edit")) {
                if (UI::MenuItem("Undo", "Ctrl+Z", false, !s.undo.empty())) vsed::VsUndo(s);
                if (UI::MenuItem("Redo", "Ctrl+Y", false, !s.redo.empty())) vsed::VsRedo(s);
                UI::MenuSeparator();
                if (UI::MenuItem("Cut", "Ctrl+X", false, any)) { UI::SetClipboardText(vsed::VsSelectionText(s.g, s.sel).c_str()); vsed::VsDeleteBlocks(s.g, s.sel, true); s.sel.clear(); s.selected = -1; }
                if (UI::MenuItem("Copy", "Ctrl+C", false, any)) UI::SetClipboardText(vsed::VsSelectionText(s.g, s.sel).c_str());
                if (UI::MenuItem("Paste", "Ctrl+V")) {
                    const UI::Vec2 c = toWorld((canvas.x0 + canvas.x1) * 0.5f, (canvas.y0 + canvas.y1) * 0.5f);
                    const char* t = UI::GetClipboardText();
                    if (t && *t) { s.sel = vsed::VsPasteText(s.g, t, c.x, c.y); s.selected = s.sel.empty() ? -1 : *s.sel.begin(); }
                }
                if (UI::MenuItem("Duplicate", "Ctrl+D", false, any)) {
                    const std::string t = vsed::VsSelectionText(s.g, s.sel);
                    float x = 0, y = 0;
                    if (const vsed::VsEditNode* n = s.g.find(s.selected >= 0 ? s.selected : *s.sel.begin())) { x = n->x + 40.0f; y = n->y + 40.0f; }
                    s.sel = vsed::VsPasteText(s.g, t, x, y);
                }
                if (UI::MenuItem("Delete", "Del", false, any)) { vsed::VsDeleteBlocks(s.g, s.sel, false); s.sel.clear(); s.selected = -1; }
                UI::MenuSeparator();
                if (UI::MenuItem("Select All", "Ctrl+A")) { s.sel.clear(); for (const auto& n : s.g.nodes) s.sel.insert(n.id); }
                if (UI::MenuItem("Comment Around Selection", "C")) {
                    const UI::Vec2 c = toWorld((canvas.x0 + canvas.x1) * 0.5f, (canvas.y0 + canvas.y1) * 0.5f);
                    vsed::VsCommentAround(s, c.x, c.y);
                    renameBox = true;
                }
                if (UI::MenuItem("Snap to Grid", nullptr, settings.snapGrid)) { settings.snapGrid = !settings.snapGrid; saveSettings(); }
                UI::EndMenu();
            }
            if (UI::BeginMenu("Graph")) {
                if (UI::MenuItem("Add Block...", "Space")) {
                    const float cx = (canvas.x0 + canvas.x1) * 0.5f, cy = (canvas.y0 + canvas.y1) * 0.5f;
                    const UI::Vec2 w = toWorld(cx - 100.0f, cy - 40.0f);
                    openPalette(cx - 160.0f, cy - 140.0f, w.x, w.y);
                }
                if (UI::BeginMenu("Arrange")) {
                    const std::size_t k = s.sel.size();
                    cmd("Align Left", "vs.alignLeft", "Shift+A", k >= 2);
                    cmd("Align Right", "vs.alignRight", "Shift+D", k >= 2);
                    cmd("Align Top", "vs.alignTop", "Shift+W", k >= 2);
                    cmd("Align Bottom", "vs.alignBottom", "Shift+S", k >= 2);
                    cmd("Align Centers", "vs.alignCenter", nullptr, k >= 2);
                    cmd("Align Middles", "vs.alignMiddle", nullptr, k >= 2);
                    UI::MenuSeparator();
                    cmd("Distribute Horizontally", "vs.distributeH", "Shift+H", k >= 3);
                    cmd("Distribute Vertically", "vs.distributeV", "Shift+V", k >= 3);
                    cmd("Straighten Wire", "vs.straighten", "Q", any);
                    cmd("Auto-Arrange", "vs.autoArrange", "Shift+L", k >= 2);
                    UI::EndMenu();
                }
                std::string why;
                cmd("Collapse to Function", "vs.collapseToFunction", nullptr, vsed::VsCanCollapse(s.g, s.sel, &why));
                UI::MenuSeparator();
                cmd("Toggle Bookmark", "vs.toggleBookmark", "Ctrl+B", any);
                cmd("Next Bookmark", "vs.nextBookmark", "F2");
                cmd("Previous Bookmark", "vs.prevBookmark", "Shift+F2");
                cmd("Find References", "vs.findReferences", "Shift+F12", any);
                if (UI::MenuItem("Find in All Graphs", "Ctrl+Shift+F")) { vsed::VsRunCommand(s, "vs.findInGraphs", root()); settings.panel = 2; }
                cmd("Watch Values", "vs.watchValues", nullptr, any);
                UI::MenuSeparator();
                if (UI::MenuItem("Check", "F7")) { checkNow(); settings.panel = 1; note(std::to_string(s.issues.size()) + " problems"); }
                cmd("Fix Every Problem", "vs.fixAll", nullptr, !s.issues.empty());
                UI::EndMenu();
            }
            if (UI::BeginMenu("Debug")) {
                if (UI::MenuItem("Toggle Breakpoint", "F9", false, any)) {
                    vsed::VsDebugView& dv = vsed::DebugView();
                    (void)dv;
                    if (s.g.path.empty()) note("Save the graph to a file first: breakpoints are kept per graph file.", 1);
                    else for (int id : s.sel) okay::vs::VsDebugger::Get().ToggleBreakpoint(s.g.path, id);
                }
                if (UI::MenuItem("Remove All Breakpoints")) okay::vs::VsDebugger::Get().ClearBreakpoints();
                const bool stopped = vsed::DebugView().stopped;
                using S = okay::vs::VsDebugger::Step;
                auto go = [&](S step) { okay::vs::VsDebugger::Get().RequestStep(step); vsed::DebugView().resume = true; };
                if (UI::MenuItem("Continue", "F5", false, stopped)) go(S::None);
                if (UI::MenuItem("Step", "F10", false, stopped)) go(S::Over);
                if (UI::MenuItem("Step Into", "F11", false, stopped)) go(S::Into);
                if (UI::MenuItem("Step Out", "Shift+F11", false, stopped)) go(S::Out);
                if (cfg.stopPlay && UI::MenuItem("Stop Play", nullptr, false, stopped)) cfg.stopPlay();
                UI::EndMenu();
            }
            if (UI::BeginMenu("View")) {
                bool changed = false;
                if (UI::MenuItem("Details", nullptr, settings.showDetails)) { settings.showDetails = !settings.showDetails; changed = true; }
                if (UI::MenuItem("Minimap", nullptr, settings.showMinimap)) { settings.showMinimap = !settings.showMinimap; changed = true; }
                if (UI::MenuItem("Scroll Moves the View (trackpad)", nullptr, settings.scrollPans)) { settings.scrollPans = !settings.scrollPans; changed = true; }
                UI::MenuSeparator();
                const char* names[] = {"Problems", "Navigator", "OkayScript", "Compare"};
                for (int i = 1; i <= 4; ++i)
                    if (UI::MenuItem(names[i - 1], nullptr, settings.panel == i)) { settings.panel = settings.panel == i ? 0 : i; changed = true; }
                UI::MenuSeparator();
                if (UI::MenuItem("Frame All", "Home")) frameAll(false);
                if (UI::MenuItem("Frame Selection", "F", false, any)) frameAll(true);
                if (UI::MenuItem("Zoom In", "Ctrl+=")) s.zoom = std::clamp(s.zoom * 1.12f, 0.28f, 2.2f);
                if (UI::MenuItem("Zoom Out", "Ctrl+-")) s.zoom = std::clamp(s.zoom / 1.12f, 0.28f, 2.2f);
                if (UI::MenuItem("Actual Size", "Ctrl+0")) s.zoom = 1.0f;
                if (cfg.windowCommands) {
                    const std::vector<AppCommand> extra = cfg.windowCommands();
                    if (!extra.empty()) UI::MenuSeparator();
                    for (const AppCommand& c : extra)
                        if (UI::MenuItem(c.title.c_str(), c.shortcut.empty() ? nullptr : c.shortcut.c_str()) && c.run) c.run();
                }
                if (changed) saveSettings();
                UI::EndMenu();
            }
            UI::EndMenuBar();
        }
    }
    UI::End();
}

// ---- the tabs -------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawTabs(float X, float Y, float W, float h, bool modal) {
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_tabs").c_str(), X, Y, W, h)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
        vsed::VsSession& ses = vsed::Session();
        vsed::State();   // there is always one tab
        const UI::Vec2 m = UI::GetMousePos();
        const float lh = UI::GetTextLineHeight();
        float x = X + 4;
        int toClose = -1;
        for (std::size_t i = 0; i < ses.tabs.size() && x < X + W - 60; ++i) {
            const vsed::VsEditState& t = *ses.tabs[i];
            const std::string label = (t.g.dirty ? "* " : "") + vsed::VsTabTitle(t);
            const float tw = std::min(220.0f, UI::CalcTextWidth(label.c_str()) + 40.0f);
            const bool on = (int)i == ses.active;
            const bool hover = m.x >= x && m.x < x + tw && m.y >= Y && m.y < Y + h;
            dl->AddRectFilled(UI::Vec2(x, Y + 3), UI::Vec2(x + tw, Y + h), on ? kTabOn : (hover ? kHover : kStrip), 4.0f);
            if (on) dl->AddRectFilled(UI::Vec2(x, Y + h - 2), UI::Vec2(x + tw, Y + h), kAccent);
            dl->AddText(UI::Vec2(x + 10, Y + (h - lh) * 0.5f + 1), on ? kText : kDim, Fit(label, tw - 34).c_str());
            const float cx = x + tw - 18;
            const bool overClose = m.x >= cx && m.x < cx + 14 && m.y >= Y + 6 && m.y < Y + h - 4;
            dl->AddText(UI::Vec2(cx + 3, Y + (h - lh) * 0.5f + 1), overClose ? kText : kDim, "x");
            if (hover && !modal && UI::IsMouseClicked(0)) { if (overClose) toClose = (int)i; else ses.active = (int)i; }
            if (hover && !modal && UI::IsMouseClicked(2)) toClose = (int)i;
            x += tw + 2;
        }
        // "+": a new graph
        const bool overPlus = m.x >= x && m.x < x + 26 && m.y >= Y && m.y < Y + h;
        dl->AddText(UI::Vec2(x + 8, Y + (h - lh) * 0.5f + 1), overPlus ? kText : kDim, "+");
        if (overPlus && !modal && UI::IsMouseClicked(0)) vsed::NewGraph(true);
        if (toClose >= 0) requestClose(toClose);
    }
    UI::End();
}

// ---- the Details pane -----------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawDetails(float X, float Y, float W, float H, bool modal) {
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_details").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), kPanel);
        dl->AddLine(UI::Vec2(X, Y), UI::Vec2(X, Y + H), kLine);
        vsed::VsEditState& s = st();
        vsed::VsEditGraph& g = s.g;
        UI::SetCursorScreenPos(UI::Vec2(X + 10, Y + 8));
        if (UI::BeginChild((cfg.idPrefix + "_detailslist").c_str(), W - 16, H - 14, false)) {
            UI::PushTextWrapPos(W - 30);
            vsed::VsEditNode* n = g.find(s.selected);
            const vsed::VsDef* d = n ? vsed::VsDefFor(g, *n) : nullptr;
            if (!n || !d) {
                UI::TextUnformatted("Graph");
                UI::TextDisabled((std::to_string(g.nodes.size()) + " blocks, " + std::to_string(g.execLinks.size() + g.dataLinks.size()) + " wires").c_str());
                UI::Spacing(6);
                UI::TextUnformatted("Variables");
                if (g.vars.empty()) UI::TextDisabled("None yet.");
                for (std::size_t i = 0; i < g.vars.size(); ++i) {
                    const auto& v = g.vars[i];
                    UI::TextUnformatted(Fit(v.name + " : " + v.type + " = " + (v.value.empty() ? std::string("(default)") : v.value) + "  [" + v.scope + "]", W - 30).c_str());
                }
                static char newVar[64] = "";
                UI::SetNextItemWidth(W - 100);
                UI::InputTextWithHint("##vsapp_newvar", "new variable name", newVar, (int)sizeof newVar);
                UI::SameLine();
                if (UI::Button("Add##vsapp_addvar") && !modal && newVar[0]) {
                    std::string why;
                    if (g.declare(newVar, "Number", "object", &why)) { note(std::string("Declared ") + newVar); newVar[0] = 0; }
                    else note("Could not declare it: " + why, 1);
                }
                UI::Spacing(6);
                UI::TextDisabled("Select a block to see what it does and set its values. Right-click the canvas, or press Space, to add one.");
            } else {
                UI::TextUnformatted(vsed::VsNodeTitle(*n, *d).c_str());
                UI::TextDisabled((std::string(n->type) + "  #" + std::to_string(n->id)).c_str());
                if (const char* help = vsed::VsNodeHelp(n->type.c_str())) { UI::Spacing(4); UI::TextWrapped(help); }
                if (d->param) {
                    UI::Spacing(6);
                    UI::TextUnformatted(d->paramHint && *d->paramHint ? d->paramHint : "Setting");
                    char buf[512];
                    std::snprintf(buf, sizeof buf, "%s", n->arg.c_str());
                    UI::SetNextItemWidth(W - 30);
                    if (UI::InputText(("##vsapp_arg" + std::to_string(n->id)).c_str(), buf, (int)sizeof buf) && !modal) { n->arg = buf; g.dirty = true; }
                }
                bool anyInput = false;
                for (int i = 0; i < (int)d->dataIn.size(); ++i) {
                    if (vsed::VsInputWired(g, n->id, i)) continue;
                    if (!anyInput) { UI::Spacing(6); UI::TextUnformatted("Values (inputs with no wire)"); anyInput = true; }
                    char buf[256];
                    const auto it = n->pinDefault.find(i);
                    std::snprintf(buf, sizeof buf, "%s", it == n->pinDefault.end() ? "" : it->second.c_str());
                    UI::TextDisabled(d->dataIn[(std::size_t)i] ? d->dataIn[(std::size_t)i] : "?");
                    UI::SetNextItemWidth(W - 30);
                    if (UI::InputText(("##vsapp_pin" + std::to_string(n->id) + "_" + std::to_string(i)).c_str(), buf, (int)sizeof buf) && !modal) {
                        if (buf[0]) n->pinDefault[i] = buf; else n->pinDefault.erase(i);
                        g.dirty = true;
                    }
                }
                const std::vector<vsed::VsQuickFix> fixes = vsed::VsQuickFixesFor(g, s.issues, n->id);
                if (!fixes.empty()) {
                    UI::Spacing(6);
                    UI::TextUnformatted("Problems on this block");
                    for (std::size_t i = 0; i < fixes.size(); ++i)
                        if (UI::Button(("Fix: " + fixes[i].title + "##vsapp_fix" + std::to_string(i)).c_str()) && !modal) { vsed::VsApplyQuickFix(g, fixes[i]); break; }
                }
                UI::Spacing(6);
                const bool marked = !g.path.empty() && vsed::Bookmarks().has(g.path, n->id);
                if (UI::Button(marked ? "Remove Bookmark" : "Add Bookmark") && !modal) vsed::VsRunCommand(s, "vs.toggleBookmark", root());
                UI::SameLine();
                const bool bp = !g.path.empty() && okay::vs::VsDebugger::Get().HasBreakpoint(g.path, n->id);
                if (UI::Button(bp ? "Remove Breakpoint" : "Add Breakpoint") && !modal) {
                    if (g.path.empty()) note("Save the graph to a file first: breakpoints are kept per graph file.", 1);
                    else okay::vs::VsDebugger::Get().ToggleBreakpoint(g.path, n->id);
                }
                if (UI::Button("Find References") && !modal) { vsed::VsRunCommand(s, "vs.findReferences", root()); settings.panel = 2; }
                if (!d->dataOut.empty()) {
                    UI::SameLine();
                    if (UI::Button(vsed::VsIde(s).watches.has(n->id, 0) ? "Stop Watching" : "Watch Values") && !modal) { vsed::VsRunCommand(s, "vs.watchValues", root()); settings.panel = 2; }
                }
            }
            UI::PopTextWrapPos();
        }
        UI::EndChild();
    }
    UI::End();
}

// ---- the panel under the canvas ---------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawPanel(float X, float Y, float W, float H, bool modal) {
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_panel").c_str(), X, Y, W, H)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), kPanel);
        dl->AddLine(UI::Vec2(X, Y), UI::Vec2(X + W, Y), kLine);
        vsed::VsEditState& s = st();
        vsed::VsEditGraph& g = s.g;
        // the panel's tabs
        const char* names[] = {"Problems", "Navigator", "OkayScript", "Compare"};
        const UI::Vec2 m = UI::GetMousePos();
        float x = X + 8;
        const float th = UI::GetTextLineHeight() + 8;
        for (int i = 1; i <= 4; ++i) {
            std::string label = names[i - 1];
            if (i == 1 && !s.issues.empty()) label += " (" + std::to_string(s.issues.size()) + ")";
            const float tw = UI::CalcTextWidth(label.c_str()) + 18;
            const bool on = settings.panel == i;
            if (on) dl->AddRectFilled(UI::Vec2(x, Y + 3), UI::Vec2(x + tw, Y + th), kTabOn, 4.0f);
            dl->AddText(UI::Vec2(x + 9, Y + 6), on ? kText : kDim, label.c_str());
            if (!modal && UI::IsMouseClicked(0) && m.x >= x && m.x < x + tw && m.y >= Y && m.y < Y + th) { settings.panel = i; saveSettings(); }
            x += tw + 4;
        }
        const bool overX = m.x >= X + W - 24 && m.x < X + W - 6 && m.y >= Y && m.y < Y + th;
        dl->AddText(UI::Vec2(X + W - 20, Y + 6), overX ? kText : kDim, "x");
        if (!modal && overX && UI::IsMouseClicked(0)) { settings.panel = 0; saveSettings(); }
        UI::SetCursorScreenPos(UI::Vec2(X + 10, Y + th + 6));
        if (UI::BeginChild((cfg.idPrefix + "_panellist").c_str(), W - 20, std::max(20.0f, H - th - 12), false)) {
            if (settings.panel == 1) {
                if (s.issues.empty()) UI::TextDisabled("No problems found.");
                for (std::size_t i = 0; i < s.issues.size(); ++i) {
                    const okay::vs::VsIssue& is = s.issues[i];
                    const std::string row = std::string(is.severity == 0 ? "Error: " : "Warning: ") + vsed::VsPlainIssue(is.message);
                    const bool canFix = vsed::VsFixAvailable(g, is.message, is.node, is.pin, is.pinDir);
                    if (UI::Selectable((Fit(row, W - (canFix ? 150.0f : 50.0f)) + "##vsapp_issue" + std::to_string(i)).c_str(), false) && !modal && is.node >= 0) {
                        s.sel = {is.node}; s.selected = is.node; s.focusNode = is.node;
                    }
                    if (canFix) {
                        UI::SameLine(W - 130.0f);
                        if (UI::SmallButton((std::string(vsed::VsPlainFixIt()) + "##vsapp_fixit" + std::to_string(i)).c_str()) && !modal) {
                            vsed::VsFixIssue(g, is.message, is.node, is.pin, is.pinDir);
                            break;   // the list is made again next frame
                        }
                    }
                }
            } else if (settings.panel == 2) {
                vsed::VsNavigator& nav = vsed::Navigator();
                UI::SetNextItemWidth(std::min(320.0f, W * 0.4f));
                if (nav.text[0] && !navBuf[0]) std::snprintf(navBuf, sizeof navBuf, "%s", nav.text);
                UI::InputTextWithHint("##vsapp_navq", "a block type, variable, event or function", navBuf, (int)sizeof navBuf);
                const char* kinds[] = {"Block type", "Variable", "Event", "Function"};
                for (int k = 0; k < 4; ++k) {
                    UI::SameLine();
                    if (UI::Button((std::string(kinds[k]) + "##vsapp_navk").c_str()) && !modal && navBuf[0]) {
                        vsed::VsRefQuery q; q.kind = (vsed::VsFindWhat)k; q.name = navBuf;
                        vsed::VsNavigatorSearch(q, root());
                    }
                }
                if (!nav.searched.empty()) UI::TextDisabled((std::to_string(nav.hits.size()) + " places use " + nav.searched).c_str());
                for (std::size_t i = 0; i < nav.hits.size() && i < 400; ++i) {
                    const vsed::VsRefHit& h = nav.hits[i];
                    const std::string row = FileName(h.path) + ": " + h.type + (h.arg.empty() ? std::string() : " " + h.arg) + " (" + h.role + ")";
                    if (UI::Selectable((Fit(row, W - 40) + "##vsapp_ref" + std::to_string(i)).c_str(), false) && !modal) vsed::VsJumpTo(h.path, h.node);
                }
                const auto bmIt = vsed::Bookmarks().byPath.find(g.path);
                if (!g.path.empty() && bmIt != vsed::Bookmarks().byPath.end() && !bmIt->second.empty()) {
                    UI::Spacing(6);
                    UI::TextUnformatted("Bookmarks");
                    for (const vsed::VsBookmark& b : bmIt->second)
                        if (UI::Selectable(("#" + std::to_string(b.node) + " " + b.label + "##vsapp_bm").c_str(), false) && !modal) vsed::VsJumpTo(g.path, b.node);
                }
                const vsed::VsWatchList& watches = vsed::VsIde(s).watches;
                if (!watches.items.empty()) {
                    UI::Spacing(6);
                    UI::TextUnformatted("Watches");
                    const okay::vs::NodeGraph* live = cfg.liveGraph ? cfg.liveGraph(g.path) : nullptr;
                    for (const vsed::VsWatchRow& r : vsed::VsWatchRows(g, live, watches))
                        UI::TextUnformatted((r.label + " = " + (r.known ? r.value : std::string("(not run yet)"))).c_str());
                }
            } else if (settings.panel == 3) {
                const std::string code = vsed::CompileToOkayScript(g);
                if (UI::SmallButton("Copy##vsapp_copycode") && !modal) { UI::SetClipboardText(code.c_str()); note("The OkayScript is on the clipboard"); }
                std::size_t a = 0;
                int lines = 0;
                while (a < code.size() && lines < 2000) {
                    std::size_t e = code.find('\n', a);
                    if (e == std::string::npos) e = code.size();
                    const std::string line = code.substr(a, e - a);
                    UI::TextUnformatted(line.empty() ? " " : line.c_str());
                    a = e + 1;
                    ++lines;
                }
            } else if (settings.panel == 4) {
                const vsed::VsDiffView& v = vsed::DiffView();
                if (!v.open || v.path != g.path) UI::TextDisabled("File > Compare with Saved File shows what changed since the graph was last saved.");
                else {
                    const std::vector<std::string> rows = compareRows();
                    UI::TextDisabled(("Against " + v.label + ": " + (rows.empty() ? std::string("no changes") : std::to_string(rows.size()) + " changes")).c_str());
                    for (const std::string& r : rows) UI::TextUnformatted(r.c_str());
                }
            }
        }
        UI::EndChild();
    }
    UI::End();
}

// ---- the debugger's bar while the game is paused -----------------------------------------------------------------------------

void VisualScriptApp::Impl::drawDebugBar(float X, float Y, float W, float h) {
    vsed::VsDebugView& dv = vsed::DebugView();
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_debug").c_str(), X, Y, W, h)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), Rgb(77, 61, 20));
        const vsed::VsDef* sd = vsed::FindDef(dv.nodeType);
        const std::string where = "Paused at " + std::string(sd ? sd->title : dv.nodeType.c_str()) + " (#" + std::to_string(dv.node) +
                                  ") on '" + dv.object + "', " + (dv.event.empty() ? std::string("?") : dv.event);
        dl->AddText(UI::Vec2(X + 10, Y + (h - UI::GetTextLineHeight()) * 0.5f), Rgb(255, 210, 76), Fit(where, W * 0.45f).c_str());
        UI::SetCursorScreenPos(UI::Vec2(X + W * 0.47f, Y + 3));
        using S = okay::vs::VsDebugger::Step;
        auto go = [&](S step) { okay::vs::VsDebugger::Get().RequestStep(step); dv.resume = true; };
        if (dv.key != st().g.path && UI::SmallButton("Open Its Graph")) { vsed::OpenFile(dv.key); dv.focusPending = true; }
        UI::SameLine();
        if (UI::SmallButton("Continue (F5)")) go(S::None);
        UI::SameLine();
        if (UI::SmallButton("Step (F10)")) go(S::Over);
        UI::SameLine();
        if (UI::SmallButton("Step Into (F11)")) go(S::Into);
        UI::SameLine();
        if (UI::SmallButton("Step Out")) go(S::Out);
        if (cfg.stopPlay) { UI::SameLine(); if (UI::SmallButton("Stop Play")) cfg.stopPlay(); }
    }
    UI::End();
}

// ---- the status line --------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawStatus(float X, float Y, float W, float h) {
    UI::BareNextWindow();
    if (UI::Begin((cfg.idPrefix + "_status").c_str(), X, Y, W, h)) {
        const auto& dl = UI::GetWindowDrawList();
        dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
        const vsed::VsEditState& s = st();
        int errors = 0, warnings = 0;
        for (const auto& is : s.issues) (is.severity == 0 ? errors : warnings)++;
        char right[160];
        std::snprintf(right, sizeof right, "%d blocks   %d errors, %d warnings   %d%%", (int)s.g.nodes.size(), errors, warnings, (int)(s.zoom * 100.0f + 0.5f));
        const float rw = UI::CalcTextWidth(right);
        const float ty = Y + (h - UI::GetTextLineHeight()) * 0.5f;
        dl->AddText(UI::Vec2(X + W - rw - 10, ty), errors ? kError : kDim, right);
        std::string left = !s.refuseMsg.empty() && s.refuseTime > 0 ? s.refuseMsg : notice;
        if (left.empty()) left = s.g.path.empty() ? std::string("Untitled graph") : s.g.path;
        dl->AddText(UI::Vec2(X + 10, ty), !s.refuseMsg.empty() && s.refuseTime > 0 ? kWarn : kDim, Fit(left, W - rw - 40).c_str());
    }
    UI::End();
}

// ---- the dialogs --------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::drawOpenBox(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    const float bw = 620.0f, bh = 360.0f;
    if (beginDialog(cfg.idPrefix + "_open", X, Y, W, H, bw, bh, bx, by)) {
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted("Open a graph (.okayvs)");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        UI::SetNextItemWidth(std::min(bw, W - 20.0f) - (cfg.pickFile ? 120.0f : 30.0f));
        if (openFocus) { UI::SetKeyboardFocusHere(); openFocus = false; }
        const bool enter = UI::InputTextWithHint("##vsapp_openpath", "a file, or filter the list", openBuf, (int)sizeof openBuf, UI::InputText_EnterReturnsTrue);
        if (cfg.pickFile) {
            UI::SameLine();
            if (UI::Button("Browse...")) {
                cfg.pickFile(folder, [this](const std::string& p) { if (!p.empty()) { openFile(p); openBox = false; } });
            }
        }
        // the folder's graphs, filtered by what is typed
        std::vector<std::string> files = folder.empty() ? std::vector<std::string>() : vsed::VsProjectGraphFiles(folder, 2000);
        std::string want = openBuf;
        for (char& c : want) c = (char)std::tolower((unsigned char)c);
        std::vector<std::string> shown;
        for (const std::string& f : files) {
            std::string low = f;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if (want.empty() || low.find(want) != std::string::npos) shown.push_back(f);
        }
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        if (UI::BeginChild((cfg.idPrefix + "_openlist").c_str(), std::min(bw, W - 20.0f) - 28.0f, bh - 110.0f, true)) {
            if (folder.empty()) UI::TextDisabled("No folder is open: type the path of a graph file.");
            else if (shown.empty()) UI::TextDisabled("No graph matches.");
            for (std::size_t i = 0; i < shown.size() && i < 300; ++i) {
                std::error_code ec;
                const std::string rel = fs::relative(fs::u8path(shown[i]), fs::u8path(folder), ec).generic_u8string();
                if (UI::Selectable(((ec || rel.empty() ? shown[i] : rel) + "##vsapp_of" + std::to_string(i)).c_str(), false)) { openFile(shown[i]); openBox = false; }
            }
        }
        UI::EndChild();
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        if ((UI::Button("Open") || enter) && openBuf[0]) {
            std::string p = openBuf;
            if (!fs::is_regular_file(fs::u8path(p)) && shown.size() == 1) p = shown[0];
            if (openFile(p)) openBox = false;
        }
        UI::SameLine();
        if (UI::Button("Cancel##vsapp_open") || UI::IsKeyPressed(UI::KeyId_Escape, false)) openBox = false;
    }
    UI::End();
}

void VisualScriptApp::Impl::drawSaveAsBox(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    if (beginDialog(cfg.idPrefix + "_saveas", X, Y, W, H, 620.0f, 120.0f, bx, by)) {
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted("Save the graph as");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        UI::SetNextItemWidth(std::min(590.0f, W - 50.0f));
        if (saveAsFocus) { UI::SetKeyboardFocusHere(); saveAsFocus = false; }
        const bool enter = UI::InputText("##vsapp_saveas", saveAsBuf, (int)sizeof saveAsBuf, UI::InputText_EnterReturnsTrue);
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        if ((UI::Button("Save##vsapp_saveas") || enter) && saveAsBuf[0]) { if (saveTabAs(saveAsTab, saveAsBuf)) saveAsBox = false; }
        UI::SameLine();
        if (UI::Button("Cancel##vsapp_saveas") || UI::IsKeyPressed(UI::KeyId_Escape, false)) saveAsBox = false;
    }
    UI::End();
}

void VisualScriptApp::Impl::drawPrompt(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    if (beginDialog(cfg.idPrefix + "_prompt", X, Y, W, H, 480.0f, 104.0f, bx, by)) {
        std::string question;
        vsed::VsSession& ses = vsed::Session();
        if (promptQuit) {
            int n = 0;
            for (const auto& t : ses.tabs) if (t->g.dirty) ++n;
            question = std::to_string(n) + (n == 1 ? " graph has" : " graphs have") + " changes that are not saved.";
        } else {
            const bool ok = promptTab >= 0 && promptTab < (int)ses.tabs.size();
            question = "Save the changes to " + (ok ? vsed::VsTabTitle(*ses.tabs[(std::size_t)promptTab]) : std::string("the graph")) + " before closing it?";
        }
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted(Fit(question, 450).c_str());
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 10));
        int answer = -1;
        if (UI::Button(promptQuit ? "Save All and Quit" : "Save##vsapp_prompt")) answer = 0;
        UI::SameLine();
        if (UI::Button(promptQuit ? "Quit Without Saving" : "Don't Save")) answer = 1;
        UI::SameLine();
        if (UI::Button("Cancel##vsapp_prompt") || UI::IsKeyPressed(UI::KeyId_Escape, false)) answer = 2;
        if (answer >= 0) answerPrompt(answer);
    }
    UI::End();
}

void VisualScriptApp::Impl::drawRenameBox(float X, float Y, float W, float H) {
    vsed::VsEditState& s = st();
    if (s.renameComment < 0 || s.renameComment >= (int)s.g.comments.size()) { renameBox = false; return; }
    float bx = 0, by = 0;
    if (beginDialog(cfg.idPrefix + "_rename", X, Y, W, H, 420.0f, 104.0f, bx, by)) {
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted("Comment");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 4));
        UI::SetNextItemWidth(390.0f);
        UI::SetKeyboardFocusHere();
        const bool enter = UI::InputText("##vsapp_rename", s.renameBuf, (int)sizeof s.renameBuf, UI::InputText_EnterReturnsTrue);
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        if (UI::Button("OK##vsapp_rename") || enter) {
            s.g.comments[(std::size_t)s.renameComment].text = s.renameBuf; s.g.dirty = true;
            s.renameComment = -1; renameBox = false;
        }
        UI::SameLine();
        if (UI::Button("Cancel##vsapp_rename") || UI::IsKeyPressed(UI::KeyId_Escape, false)) { s.renameComment = -1; renameBox = false; }
    }
    UI::End();
}

// ---- keys -------------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::shortcuts(bool modal) {
    const UI::Input& in = UI::GetFrameInput();
    const bool ctrl = in.ctrl, shift = in.shift, alt = UI::IsKeyDown(UI::KeyId_Alt);
    if (ctrl && !alt) UI::GetIO().InputQueueCharacters.resize(0);   // Ctrl+<key> is a shortcut, never typing
    if (modal) return;
    auto key = [](UI::KeyId k) { return UI::IsKeyPressed(k, false); };
    vsed::VsSession& ses = vsed::Session();
    vsed::VsEditState& s = st();
    // the file keys work wherever the keyboard is
    if (ctrl && shift && key(UI::KeyId_S)) { for (int i = 0; i < (int)ses.tabs.size(); ++i) if (ses.tabs[(std::size_t)i]->g.dirty) saveTab(i); return; }
    if (ctrl && key(UI::KeyId_S)) { saveTab(ses.active); return; }
    if (ctrl && key(UI::KeyId_N)) { vsed::NewGraph(true); return; }
    if (ctrl && key(UI::KeyId_O)) { openBox = true; openFocus = true; openBuf[0] = 0; return; }
    if (ctrl && shift && key(UI::KeyId_T)) { vsed::VsReopenClosedGraph(); return; }
    if (ctrl && (key(UI::KeyId_W) || key(UI::KeyId_F4))) { requestClose(ses.active); return; }
    if (ctrl && key(UI::KeyId_Tab) && ses.tabs.size() > 1) { ses.active = (ses.active + (shift ? (int)ses.tabs.size() - 1 : 1)) % (int)ses.tabs.size(); return; }
    if (ctrl && key(UI::KeyId_Q) && cfg.quitItem) { requestQuit(); return; }
    if (vsed::DebugView().stopped) {
        using S = okay::vs::VsDebugger::Step;
        auto go = [&](S step) { okay::vs::VsDebugger::Get().RequestStep(step); vsed::DebugView().resume = true; };
        if (key(UI::KeyId_F5)) go(S::None);
        else if (key(UI::KeyId_F10)) go(S::Over);
        else if (key(UI::KeyId_F11)) go(shift ? S::Out : S::Into);
    }
    if (UI::WantsTextInput() || palOpen) return;   // a field has the keyboard: the rest is typing
    if (!canvasFocused) return;
    if (ctrl && !shift && key(UI::KeyId_Z)) { vsed::VsUndo(s); return; }
    if ((ctrl && key(UI::KeyId_Y)) || (ctrl && shift && key(UI::KeyId_Z))) { vsed::VsRedo(s); return; }
    if (ctrl && key(UI::KeyId_A)) { s.sel.clear(); for (const auto& n : s.g.nodes) s.sel.insert(n.id); return; }
    const bool any = !s.sel.empty();
    if (ctrl && key(UI::KeyId_C) && any) { UI::SetClipboardText(vsed::VsSelectionText(s.g, s.sel).c_str()); note("Copied " + std::to_string(s.sel.size()) + " blocks"); return; }
    if (ctrl && key(UI::KeyId_X) && any) { UI::SetClipboardText(vsed::VsSelectionText(s.g, s.sel).c_str()); vsed::VsDeleteBlocks(s.g, s.sel, true); s.sel.clear(); s.selected = -1; return; }
    if (ctrl && key(UI::KeyId_V)) {
        const UI::Vec2 m = UI::GetMousePos();
        const UI::Vec2 w = canvas.in(m.x, m.y) ? toWorld(m.x, m.y) : toWorld((canvas.x0 + canvas.x1) * 0.5f, (canvas.y0 + canvas.y1) * 0.5f);
        const char* t = UI::GetClipboardText();
        if (t && *t) { s.sel = vsed::VsPasteText(s.g, t, w.x, w.y); s.selected = s.sel.empty() ? -1 : *s.sel.begin(); }
        return;
    }
    if (ctrl && key(UI::KeyId_D) && any) {
        const std::string t = vsed::VsSelectionText(s.g, s.sel);
        float x = 0, y = 0;
        if (const vsed::VsEditNode* n = s.g.find(s.selected >= 0 ? s.selected : *s.sel.begin())) { x = n->x + 40.0f; y = n->y + 40.0f; }
        s.sel = vsed::VsPasteText(s.g, t, x, y);
        s.selected = s.sel.empty() ? -1 : *s.sel.begin();
        return;
    }
    if ((key(UI::KeyId_Delete) || key(UI::KeyId_Backspace)) && any) { vsed::VsDeleteBlocks(s.g, s.sel, false); s.sel.clear(); s.selected = -1; return; }
    if (ctrl && key(UI::KeyId_Equal)) { s.zoom = std::clamp(s.zoom * 1.12f, 0.28f, 2.2f); return; }
    if (ctrl && key(UI::KeyId_Minus)) { s.zoom = std::clamp(s.zoom / 1.12f, 0.28f, 2.2f); return; }
    if (ctrl && key(UI::KeyId_0)) { s.zoom = 1.0f; return; }
    if (!ctrl && !alt && key(UI::KeyId_Home)) { frameAll(false); return; }
    if (!ctrl && !alt && !shift && key(UI::KeyId_F)) { frameAll(any); return; }
    if (!ctrl && !alt && !shift && key(UI::KeyId_C)) {
        const UI::Vec2 c = toWorld((canvas.x0 + canvas.x1) * 0.5f, (canvas.y0 + canvas.y1) * 0.5f);
        vsed::VsCommentAround(s, c.x, c.y);
        renameBox = true;
        return;
    }
    if (!ctrl && !alt && !shift && key(UI::KeyId_F7)) { checkNow(); settings.panel = 1; note(std::to_string(s.issues.size()) + " problems"); return; }
    if (!ctrl && !alt && !shift && key(UI::KeyId_F9) && any) {
        if (s.g.path.empty()) note("Save the graph to a file first: breakpoints are kept per graph file.", 1);
        else for (int id : s.sel) okay::vs::VsDebugger::Get().ToggleBreakpoint(s.g.path, id);
        return;
    }
    if (!ctrl && !alt && (key(UI::KeyId_Space) || key(UI::KeyId_Tab))) {
        const UI::Vec2 m = UI::GetMousePos();
        const bool inside = canvas.in(m.x, m.y);
        const float sx = inside ? m.x : (canvas.x0 + canvas.x1) * 0.5f, sy = inside ? m.y : (canvas.y0 + canvas.y1) * 0.5f;
        const UI::Vec2 w = toWorld(sx, sy);
        openPalette(sx, sy, w.x, w.y);
        return;
    }
    if (alt && !ctrl && key(UI::KeyId_Enter) && s.selected >= 0) {   // the block's first quick fix
        const auto fixes = vsed::VsQuickFixesFor(s.g, s.issues, s.selected);
        if (!fixes.empty()) vsed::VsApplyQuickFix(s.g, fixes.front());
        return;
    }
    // The IDE commands, by the keys VsCommands() lists for them (Shift+A align left, Q straighten, Ctrl+B bookmark, F2 ...).
    static const std::pair<UI::KeyId, const char*> kNamed[] = {
        {UI::KeyId_A, "A"}, {UI::KeyId_B, "B"}, {UI::KeyId_D, "D"}, {UI::KeyId_F, "F"}, {UI::KeyId_H, "H"}, {UI::KeyId_L, "L"},
        {UI::KeyId_Q, "Q"}, {UI::KeyId_S, "S"}, {UI::KeyId_V, "V"}, {UI::KeyId_W, "W"}, {UI::KeyId_F2, "F2"}, {UI::KeyId_F12, "F12"}};
    for (const auto& k : kNamed) {
        if (!key(k.first)) continue;
        const std::string keys = std::string(ctrl ? "Ctrl+" : "") + (shift ? "Shift+" : "") + (alt ? "Alt+" : "") + k.second;
        for (const vsed::VsCommand& c : vsed::VsCommands())
            if (keys == c.shortcut) {
                if (!vsed::VsRunCommand(s, c.id, root())) note(std::string(c.title) + ": nothing to do here", 1);
                if (std::strcmp(c.id, "vs.findReferences") == 0 || std::strcmp(c.id, "vs.findInGraphs") == 0) settings.panel = 2;
                return;
            }
    }
}

// ---- one frame ----------------------------------------------------------------------------------------------------------------

void VisualScriptApp::Impl::frame(float X, float Y, float W, float H) {
    const bool modal = modalOpen();
    shortcuts(modal);
    vsed::VsEditState& s = st();
    if (UI::GetTime() - lastCheckAt > 0.25) { lastCheckAt = UI::GetTime(); checkNow(); }   // checked again only when the graph changed
    if (s.refuseTime > 0) s.refuseTime -= UI::GetIO().DeltaTime > 0 ? UI::GetIO().DeltaTime : 1.0f / 60.0f;
    vsed::VsDebugView& dv = vsed::DebugView();
    if (dv.stopped && dv.focusPending && dv.key == s.g.path) { s.focusNode = dv.node; s.sel = {dv.node}; s.selected = dv.node; dv.focusPending = false; }

    const float menuH = 26.0f, tabH = 30.0f, statusH = 24.0f, dbgH = dv.stopped ? 30.0f : 0.0f;
    const float detailsW = settings.showDetails ? std::min(320.0f, std::max(220.0f, W * 0.26f)) : 0.0f;
    const float panelH = settings.panel == 0 ? 0.0f : std::min(260.0f, std::max(130.0f, H * 0.3f));
    const float bodyY = Y + menuH + tabH + dbgH, bodyH = std::max(40.0f, H - menuH - tabH - dbgH - statusH);
    drawMenu(X, Y, W, menuH);
    drawTabs(X, Y + menuH, W, tabH, modal);
    if (dbgH > 0) drawDebugBar(X, Y + menuH + tabH, W, dbgH);
    const float cw = std::max(60.0f, W - detailsW);
    drawCanvas(X, bodyY, cw, std::max(30.0f, bodyH - panelH), modal);
    if (detailsW > 0) drawDetails(X + cw, bodyY, detailsW, bodyH - panelH, modal);
    if (panelH > 0) drawPanel(X, bodyY + bodyH - panelH, W, panelH, modal);
    drawStatus(X, Y + H - statusH, W, statusH);
    if (nodeMenu >= 0) drawNodeMenu();
    if (palOpen) drawPalette(X, Y, W, H);
    if (openBox) drawOpenBox(X, Y, W, H);
    if (saveAsBox) drawSaveAsBox(X, Y, W, H);
    if (renameBox) drawRenameBox(X, Y, W, H);
    if (promptOpen) drawPrompt(X, Y, W, H);
    vsed::VsTrackUndo(s, !UI::IsMouseDown(0) && !UI::IsAnyItemActive());
    wantsKeys = canvasFocused || UI::WantsTextInput() || modalOpen() || palOpen;
}

// ---- VisualScriptApp ------------------------------------------------------------------------------------------------------------

VisualScriptApp::VisualScriptApp(Config config) : m(new Impl(std::move(config))) {}
VisualScriptApp::~VisualScriptApp() { m->saveSettings(); }

bool VisualScriptApp::OpenFolder(const std::string& folder) {
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(folder), ec)) return false;
    const fs::path a = fs::weakly_canonical(fs::u8path(folder), ec);
    m->folder = (ec ? fs::u8path(folder) : a).generic_u8string();
    m->settings.lastFolder = m->folder;
    m->saveSettings();
    return true;
}
bool VisualScriptApp::OpenFile(const std::string& path) { return m->openFile(path); }
bool VisualScriptApp::OpenPath(const std::string& path) {
    std::error_code ec;
    if (fs::is_directory(fs::u8path(path), ec)) return OpenFolder(path);
    if (!fs::is_regular_file(fs::u8path(path), ec)) return false;
    if (m->folder.empty()) OpenFolder(fs::u8path(path).parent_path().u8string());
    return OpenFile(path);
}
void VisualScriptApp::NewGraph(bool starter) { okay::editor::vsed::NewGraph(starter); }
bool VisualScriptApp::Save() { return m->saveTab(vsed::Session().active); }
bool VisualScriptApp::SaveAs(const std::string& path) { return m->saveTabAs(vsed::Session().active, path); }
int VisualScriptApp::SaveAll() {
    int n = 0;
    vsed::VsSession& ses = vsed::Session();
    for (int i = 0; i < (int)ses.tabs.size(); ++i) if (ses.tabs[(std::size_t)i]->g.dirty && m->saveTab(i)) ++n;
    return n;
}
void VisualScriptApp::RequestClose(int tab) { m->requestClose(tab); }
bool VisualScriptApp::ClosePromptOpen() const { return m->promptOpen; }
void VisualScriptApp::AnswerClosePrompt(int answer) { m->answerPrompt(answer); }
bool VisualScriptApp::ReopenClosed() { return vsed::VsReopenClosedGraph(); }
int VisualScriptApp::TabCount() const { vsed::State(); return (int)vsed::Session().tabs.size(); }
int VisualScriptApp::ActiveTab() const { vsed::State(); return vsed::Session().active; }
void VisualScriptApp::SetActiveTab(int index) { if (index >= 0 && index < TabCount()) vsed::Session().active = index; }
vsed::VsEditState& VisualScriptApp::Active() { return vsed::State(); }

void VisualScriptApp::OpenPalette(float worldX, float worldY) {
    const UI::Vec2 p = m->toScreen(worldX, worldY);
    m->openPalette(p.x, p.y, worldX, worldY);
}
bool VisualScriptApp::PaletteOpen() const { return m->palOpen; }
void VisualScriptApp::SetPaletteQuery(const std::string& query) {
    std::snprintf(m->palBuf, sizeof m->palBuf, "%s", query.c_str());
    m->palSel = 0;
    m->buildPalette();
}
std::vector<std::string> VisualScriptApp::PaletteRows() const {
    m->buildPalette();
    std::vector<std::string> out;
    for (const auto& r : m->palRows) out.push_back(r.title);
    return out;
}
std::vector<std::string> VisualScriptApp::PaletteTypes() const {
    m->buildPalette();
    std::vector<std::string> out;
    for (const auto& r : m->palRows) out.push_back(r.def && r.def->type ? r.def->type : "");
    return out;
}
bool VisualScriptApp::PickPaletteRow(std::size_t index) { m->buildPalette(); return m->pickPalette(index); }
int VisualScriptApp::AddBlock(const std::string& type, float x, float y) {
    const vsed::VsDef* d = vsed::FindDef(type);
    if (!d) return -1;
    vsed::VsEditState& s = m->st();
    s.paletteWorld = vsed::VsPoint(x, y);
    return vsed::VsAddFromPalette(s, *d);
}
bool VisualScriptApp::RunCommand(const std::string& id) { return vsed::VsRunCommand(m->st(), id, m->root()); }
bool VisualScriptApp::Undo() { return vsed::VsUndo(m->st()); }
bool VisualScriptApp::Redo() { return vsed::VsRedo(m->st()); }
void VisualScriptApp::SelectAll() { vsed::VsEditState& s = m->st(); s.sel.clear(); for (const auto& n : s.g.nodes) s.sel.insert(n.id); }
bool VisualScriptApp::CopySelection() {
    vsed::VsEditState& s = m->st();
    if (s.sel.empty()) return false;
    UI::SetClipboardText(vsed::VsSelectionText(s.g, s.sel).c_str());
    return true;
}
bool VisualScriptApp::PasteClipboard() {
    vsed::VsEditState& s = m->st();
    const char* t = UI::GetClipboardText();
    if (!t || !*t) return false;
    const UI::Vec2 c = m->toWorld((m->canvas.x0 + m->canvas.x1) * 0.5f, (m->canvas.y0 + m->canvas.y1) * 0.5f);
    s.sel = vsed::VsPasteText(s.g, t, c.x, c.y);
    s.selected = s.sel.empty() ? -1 : *s.sel.begin();
    return !s.sel.empty();
}
bool VisualScriptApp::DuplicateSelection() {
    vsed::VsEditState& s = m->st();
    if (s.sel.empty()) return false;
    const std::string t = vsed::VsSelectionText(s.g, s.sel);
    const vsed::VsEditNode* n = s.g.find(*s.sel.begin());
    s.sel = vsed::VsPasteText(s.g, t, n ? n->x + 40.0f : 0.0f, n ? n->y + 40.0f : 0.0f);
    s.selected = s.sel.empty() ? -1 : *s.sel.begin();
    return !s.sel.empty();
}
int VisualScriptApp::DeleteSelection() {
    vsed::VsEditState& s = m->st();
    const int n = vsed::VsDeleteBlocks(s.g, s.sel, false);
    s.sel.clear(); s.selected = -1;
    return n;
}
bool VisualScriptApp::CommentSelection() {
    vsed::VsEditState& s = m->st();
    if (s.sel.empty()) return false;
    vsed::VsCommentAround(s, 0, 0);
    s.renameComment = -1;
    return true;
}
void VisualScriptApp::FrameAll() { m->frameAll(false); }
void VisualScriptApp::FrameSelection() { m->frameAll(true); }
bool VisualScriptApp::PlugOnScreen(int node, bool exec, bool output, int pin, float& x, float& y) const {
    for (const auto& p : m->plugs)
        if (p.node == node && p.exec == exec && p.output == output && p.pin == pin) { x = p.x; y = p.y; return true; }
    return false;
}
bool VisualScriptApp::BlockOnScreen(int node, float& x0, float& y0, float& x1, float& y1) const {
    const auto it = m->blockRects.find(node);
    if (it == m->blockRects.end()) return false;
    x0 = it->second.x0; y0 = it->second.y0; x1 = it->second.x1; y1 = it->second.y1;
    return true;
}
std::size_t VisualScriptApp::CheckNow() { m->checkNow(); return m->st().issues.size(); }
std::vector<std::string> VisualScriptApp::ProblemLines() {
    m->checkNow();
    std::vector<std::string> out;
    for (const auto& is : m->st().issues) out.push_back(std::string(is.severity == 0 ? "Error: " : "Warning: ") + vsed::VsPlainIssue(is.message));
    return out;
}
bool VisualScriptApp::FixProblem(std::size_t index) {
    m->checkNow();
    vsed::VsEditState& s = m->st();
    if (index >= s.issues.size()) return false;
    const okay::vs::VsIssue is = s.issues[index];
    return vsed::VsFixIssue(s.g, is.message, is.node, is.pin, is.pinDir);
}
std::string VisualScriptApp::OkayScriptText() { return vsed::CompileToOkayScript(m->st().g); }
bool VisualScriptApp::CompareWithSaved() {
    vsed::VsEditState& s = m->st();
    if (s.g.path.empty()) return false;
    std::ifstream f(fs::u8path(s.g.path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    vsed::VsShowDiff(s.g.path, "the saved file", ss.str());
    m->settings.panel = 4;
    return true;
}
std::vector<std::string> VisualScriptApp::CompareRows() { return m->compareRows(); }
std::vector<std::string> VisualScriptApp::NavigatorRows() const {
    std::vector<std::string> out;
    for (const vsed::VsRefHit& h : vsed::Navigator().hits) out.push_back(FileName(h.path) + ": " + h.type + (h.arg.empty() ? std::string() : " " + h.arg));
    return out;
}
void VisualScriptApp::ShowPanel(int panel) { m->settings.panel = std::clamp(panel, 0, 4); }
int VisualScriptApp::Panel() const { return m->settings.panel; }
bool VisualScriptApp::ToggleBreakpoint() {
    vsed::VsEditState& s = m->st();
    if (s.g.path.empty() || s.selected < 0) return false;
    okay::vs::VsDebugger::Get().ToggleBreakpoint(s.g.path, s.selected);
    return okay::vs::VsDebugger::Get().HasBreakpoint(s.g.path, s.selected);
}
void VisualScriptApp::Frame(float x, float y, float w, float h) { m->frame(x, y, w, h); }
bool VisualScriptApp::WantsQuit() const { return m->quit; }
std::string VisualScriptApp::WindowTitle() const {
    const vsed::VsEditState& s = vsed::State();
    std::string t = (s.g.dirty ? "*" : "") + vsed::VsTabTitle(s) + " - ";
    if (!m->folder.empty()) t += fs::u8path(m->folder).filename().u8string() + " - ";
    return t + "Visual Script";
}
void VisualScriptApp::SaveSettings() { m->saveSettings(); }
const std::string& VisualScriptApp::Folder() const { return m->folder; }
const std::string& VisualScriptApp::Notice() const { return m->notice; }
AppSettings& VisualScriptApp::Settings() { return m->settings; }
bool VisualScriptApp::WantsKeyboard() const { return m->wantsKeys; }

}  // namespace vsapp
