// ScriptIdeUi (ScriptIdeUi.hpp): the Script Editor's IDE panels. The decisions are editor/ScriptIde.hpp's (tested headless in
// tests/test_script_ide.cpp); this file draws them with the editor's widgets.
#include "EditorCommon.hpp"
#include "ScriptIdeUi.hpp"
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "PluginsWindow.hpp"       // DrawPluginPanelBody: a plugin's panel with the ui_* widgets
#include "okay/Platform/Platform.hpp"   // okay::platform::TicksMs32

#include <chrono>
#include <ctime>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace oked::ide {

namespace fs = std::filesystem;

namespace {
struct State {
    std::string project = "\x01";                 // the project the lists below belong to
    std::vector<std::string> scripts;             // every project script
    std::uint32_t nextScan = 0;
    scriptide::RecentFiles recent{30};
    scriptide::SymbolIndex index;
    scriptide::SymbolIndexBuilder builder;
    bool indexWanted = true;
    std::unordered_map<std::string, std::size_t> liveHash;   // the text the index last took for an open script
    scriptide::LocalHistory history;
    // Quick Open
    bool qoOpen = false;
    char qoQuery[160] = "";
    int qoSel = 0;
    // the script tree
    char treeFilter[96] = "";
    std::set<std::string> collapsed;
    // rename preview
    bool rnOpen = false;
    scriptide::RenamePlan plan;
    // quick fixes
    bool qfOpen = false;
    float qfX = -1.0f, qfY = -1.0f;
    std::vector<scriptide::QuickFix> fixes;
    // local history
    bool lhOpen = false;
    std::string lhPath, lhCurrent;
    std::vector<scriptide::HistoryVersion> lhVersions;
    int lhSel = 0;
    std::string lhText;
    std::vector<scriptide::DiffLine> lhDiff;
};
State& S() { static State s; return s; }

std::int64_t WallClockMs() {
    return (std::int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::uint32_t TicksMs() { return okay::platform::TicksMs32(); }
std::string RecentFile() { return S().project.empty() ? std::string() : (fs::path(S().project) / ".okay" / "recent_scripts.txt").string(); }
void SaveRecentScripts() {
    const std::string f = RecentFile();
    if (f.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(f).parent_path(), ec);
    std::ofstream(f, std::ios::binary) << S().recent.Serialize();
}
std::string RootForLabels() { return S().project.empty() ? std::string() : S().project; }
} // namespace

void Tick(const std::string& projectDir) {
    State& s = S();
    if (s.project != projectDir) {
        s.project = projectDir;
        s.scripts.clear();
        s.index.Clear();
        s.liveHash.clear();
        s.history.SetProject(projectDir);
        s.recent.Parse(extide::ReadFile(RecentFile()));
        s.nextScan = 0;
        s.indexWanted = true;
    }
    if (s.nextScan == 0 || (std::int32_t)(TicksMs() - s.nextScan) >= 0) {   // every 5 seconds: the list of scripts (added or removed outside)
        const std::string root = projectDir.empty() ? std::string("Assets") : (fs::path(projectDir) / "Assets").generic_string();
        std::vector<std::string> now = scriptide::ListProjectScripts(root);
        if (now != s.scripts) { s.scripts = std::move(now); s.indexWanted = true; }
        s.nextScan = (TicksMs() + 5000) | 1u;   // (never 0: 0 means "now")
    }
    if (s.indexWanted && !s.builder.Busy()) { s.builder.Start(s.scripts); s.indexWanted = false; }
    scriptide::SymbolIndex built;
    if (s.builder.Poll(built)) {
        // Open scripts keep their unsaved text in the index.
        for (const auto& path : s.index.Files())
            if (s.liveHash.count(path)) if (const std::string* t = s.index.Text(path)) built.SetFile(path, *t);
        s.index = std::move(built);
    }
}

void NoteOpened(const std::string& path) {
    if (path.empty()) return;
    S().recent.Push(fs::path(path).generic_string());
    SaveRecentScripts();
}
void NoteSaved(const std::string& path, const std::string& text) {
    if (path.empty()) return;
    S().history.Record(path, text, WallClockMs(), "Saved");
    S().index.SetFile(fs::path(path).generic_string(), text);
}
void NoteEdited(const std::string& path, const std::string& text) {
    if (path.empty()) return;
    const std::string key = fs::path(path).generic_string();
    const std::size_t h = std::hash<std::string>{}(text);
    auto it = S().liveHash.find(key);
    if (it != S().liveHash.end() && it->second == h) return;
    S().liveHash[key] = h;
    S().index.SetFile(key, text);
    S().history.MaybeRecord(path, text, WallClockMs());
}
const std::vector<std::string>& ProjectScripts() { return S().scripts; }
const std::vector<std::string>& RecentScripts() { return S().recent.Items(); }
scriptide::SymbolIndex& Index() { return S().index; }
bool SymbolIndexBusy() { return S().builder.Busy(); }

// ---- Quick Open ---------------------------------------------------------------------------------------------------------
void OpenQuickOpen() { S().qoOpen = true; }

void DrawQuickOpen() {
    State& s = S();
    if (s.qoOpen) { s.qoOpen = false; s.qoQuery[0] = '\0'; s.qoSel = 0; ImGui::OpenPopup("Go to File##qo"); }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() * 0.5f, ImGui::GetWindowPos().y + 60.0f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("Go to File##qo")) return;
    ImGui::TextDisabled("Go to File   (type part of a name; name:line jumps to a line)");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    const bool enter = ImGui::InputTextWithHint("##qoq", "Player, ui/pause, enemy:40 ...", s.qoQuery, sizeof s.qoQuery, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited()) s.qoSel = 0;
    const auto items = scriptide::RankQuickOpen(s.qoQuery, s.scripts, s.recent.Items(), RootForLabels(), 40);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) s.qoSel = std::min(s.qoSel + 1, (int)items.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) s.qoSel = std::max(s.qoSel - 1, 0);
    const int line = scriptide::ParseQuickOpenQuery(s.qoQuery).line;
    ImGui::BeginChild("##qores", ImVec2(0, 320));
    if (items.empty()) ImGui::TextDisabled(s.scripts.empty() ? "No scripts in this project yet." : "Nothing matches.");
    for (int i = 0; i < (int)items.size(); ++i) {
        const auto& it = items[(std::size_t)i];
        ImGui::PushID(i);
        const std::string name = fs::path(it.path).filename().string();
        bool pick = ImGui::Selectable(name.c_str(), i == s.qoSel) || (enter && i == s.qoSel);
        if (i == s.qoSel && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        ImGui::SameLine(200.0f);
        ImGui::TextDisabled("%s%s", it.label.c_str(), it.recent ? "   (recent)" : "");
        if (pick) {
            OpenScriptFileInEditorAt(it.path, line > 0 ? line : 1);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Enter opens   Up/Down choose   Esc closes");
    ImGui::EndPopup();
}

void DrawRecentMenu() {
    const auto& r = S().recent.Items();
    if (r.empty()) { ImGui::TextDisabled("(none yet)"); return; }
    int n = 0;
    for (const auto& p : r) {
        if (++n > 15) break;
        const std::string label = fs::path(p).filename().string() + "##rec" + std::to_string(n);
        if (ImGui::MenuItem(label.c_str())) OpenScriptFileInEditorAt(p, 1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Clear Recent")) { S().recent = scriptide::RecentFiles(30); SaveRecentScripts(); }
}

// ---- the script tree ----------------------------------------------------------------------------------------------------
void DrawScriptTree(float width, float height, const std::string& activePath) {
    State& s = S();
    ImGui::BeginChild("##scripttree", ImVec2(width, height), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##treefilter", "Filter scripts", s.treeFilter, sizeof s.treeFilter);
    const std::string root = s.project.empty() ? std::string() : s.project;
    const auto rows = scriptide::BuildFileTree(s.scripts, root, s.treeFilter);
    const std::string active = fs::path(activePath).generic_string();
    const bool filtering = s.treeFilter[0] != '\0';
    int hideBelow = -1;                                   // inside a collapsed folder: rows deeper than this are hidden
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (hideBelow >= 0) { if (r.depth > hideBelow) continue; hideBelow = -1; }
        ImGui::PushID((int)i);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f * (float)r.depth);
        if (r.folder) {
            const bool closed = !filtering && s.collapsed.count(r.path);
            const std::string lbl = std::string(closed ? "> " : "v ") + r.name;
            if (ImGui::Selectable(lbl.c_str(), false)) { if (closed) s.collapsed.erase(r.path); else s.collapsed.insert(r.path); }
            if (closed) hideBelow = r.depth;
        } else {
            if (ImGui::Selectable(r.name.c_str(), r.path == active)) OpenScriptFileInEditorAt(r.path, 1);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.path.c_str());
        }
        ImGui::PopID();
    }
    if (rows.empty()) ImGui::TextDisabled(s.scripts.empty() ? "No scripts yet." : "Nothing matches.");
    ImGui::EndChild();
}

// ---- across files -------------------------------------------------------------------------------------------------------
bool GoToDefinitionInProject(const std::string& word, const std::string& currentPath) {
    if (word.empty()) return false;
    const std::string cur = fs::path(currentPath).generic_string();
    for (const auto& d : S().index.Definitions(word)) {
        if (d.file == cur) continue;
        OpenScriptFileInEditorAt(d.file, d.line);
        ConsoleLog("Go to " + d.display + " (" + fs::path(d.file).filename().string() + " line " + std::to_string(d.line) + ")");
        return true;
    }
    return false;
}

std::vector<scriptide::Occurrence> ReferencesElsewhere(const std::string& word, const std::string& currentPath) {
    std::vector<scriptide::Occurrence> out;
    const std::string cur = fs::path(currentPath).generic_string();
    for (auto& o : S().index.References(word)) if (o.file != cur) out.push_back(std::move(o));
    return out;
}

void OpenRenamePreview(const std::string& from, const std::string& to) {
    S().plan = scriptide::PlanRename(S().index, from, to);
    S().rnOpen = true;
}

void DrawRenamePreview(const std::function<bool(const std::string&, const std::string&)>& applyOpen) {
    State& s = S();
    if (s.rnOpen) { s.rnOpen = false; ImGui::OpenPopup("Rename in Project##rn"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Rename in Project##rn", nullptr)) return;
    const auto& p = s.plan;
    ImGui::Text("Rename '%s' to '%s'", p.from.c_str(), p.to.c_str());
    if (!p.error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "%s", p.error.c_str());
    } else {
        ImGui::TextDisabled("%d change%s in %d file%s. Comments and strings are left alone.", p.count, p.count == 1 ? "" : "s",
                            (int)p.newTexts.size(), p.newTexts.size() == 1 ? "" : "s");
        ImGui::BeginChild("##rnlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4.0f), ImGuiChildFlags_Borders);
        std::string lastFile;
        for (const auto& c : p.changes) {
            if (c.file != lastFile) {
                lastFile = c.file;
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", fs::path(c.file).filename().string().c_str());
            }
            ImGui::TextDisabled("%5d", c.line); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.50f, 1.0f), "- %s", c.before.c_str());
            ImGui::TextDisabled("     "); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.55f, 1.0f), "+ %s", c.after.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::BeginDisabled(!p.error.empty() || p.newTexts.empty());
    if (ImGui::Button("Rename", ImVec2(140, 0))) {
        int written = 0, open = 0;
        for (const auto& kv : p.newTexts) {
            if (applyOpen && applyOpen(kv.first, kv.second)) { ++open; continue; }
            std::ofstream f(kv.first, std::ios::binary);
            if (f << kv.second) ++written;
        }
        for (const auto& kv : p.newTexts) s.index.SetFile(kv.first, kv.second);
        ConsoleLog("Renamed " + p.from + " -> " + p.to + ": " + std::to_string(p.count) + " change(s); " + std::to_string(open) +
                   " open tab(s) changed (save them), " + std::to_string(written) + " file(s) written");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---- quick fixes --------------------------------------------------------------------------------------------------------
void OpenQuickFixes(const std::vector<scriptide::QuickFix>& fixes, float x, float y) {
    if (fixes.empty()) return;
    S().fixes = fixes;
    S().qfOpen = true;
    S().qfX = x; S().qfY = y;
}
void DrawQuickFixes(const std::function<void(const scriptide::QuickFix&)>& apply) {
    State& s = S();
    if (s.qfOpen) {
        s.qfOpen = false;
        if (s.qfX >= 0.0f) ImGui::SetNextWindowPos(ImVec2(s.qfX, s.qfY));
        ImGui::OpenPopup("##quickfixes");
    }
    if (!ImGui::BeginPopup("##quickfixes")) return;
    ImGui::TextDisabled("Quick fixes");
    ImGui::Separator();
    for (std::size_t i = 0; i < s.fixes.size(); ++i) {
        ImGui::PushID((int)i);
        if (ImGui::Selectable(s.fixes[i].title.c_str()) || (i == 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsWindowAppearing())) {
            if (apply) apply(s.fixes[i]);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

// ---- local history ------------------------------------------------------------------------------------------------------
void OpenLocalHistory(const std::string& path, const std::string& currentText) {
    State& s = S();
    s.lhPath = path; s.lhCurrent = currentText;
    s.lhVersions = s.history.Versions(path);
    s.lhSel = s.lhVersions.empty() ? -1 : 0;
    s.lhText = s.lhSel >= 0 ? s.history.Load(s.lhVersions[0]) : std::string();
    s.lhDiff = scriptide::LineDiff(s.lhText, s.lhCurrent);
    s.lhOpen = true;
}
void DrawLocalHistory(const std::function<void(const std::string&)>& restore) {
    State& s = S();
    if (s.lhOpen) { s.lhOpen = false; ImGui::OpenPopup("Local History##lh"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(860, 520), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Local History##lh", nullptr)) return;
    ImGui::Text("%s", fs::path(s.lhPath).filename().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  copies kept in the project's .okay/history folder on every save and every 5 minutes of edits");
    if (s.lhVersions.empty()) {
        ImGui::TextDisabled(s.history.Project().empty() ? "Open a project to keep a local history." : "No copies yet: save the script once.");
    } else {
        ImGui::BeginChild("##lhlist", ImVec2(230, -ImGui::GetFrameHeightWithSpacing() - 4.0f), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)s.lhVersions.size(); ++i) {
            const auto& v = s.lhVersions[(std::size_t)i];
            const std::time_t t = (std::time_t)(v.time / 1000);
            char when[64] = "";
            if (const std::tm* tm = std::localtime(&t)) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tm);
            const std::string lbl = std::string(when) + "  " + v.label + "##lh" + std::to_string(i);
            if (ImGui::Selectable(lbl.c_str(), i == s.lhSel)) {
                s.lhSel = i;
                s.lhText = s.history.Load(v);
                s.lhDiff = scriptide::LineDiff(s.lhText, s.lhCurrent);
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##lhdiff", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4.0f), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("- only in the copy   + only in the script now");
        for (const auto& d : s.lhDiff) {
            if (d.op == '-') ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.50f, 1.0f), "- %s", d.text.c_str());
            else if (d.op == '+') ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.55f, 1.0f), "+ %s", d.text.c_str());
            else ImGui::TextDisabled("  %s", d.text.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::BeginDisabled(s.lhSel < 0);
    if (ImGui::Button("Restore This Copy", ImVec2(170, 0))) {
        if (restore) restore(s.lhText);
        ConsoleLog("Restored a local history copy of " + s.lhPath + " (Ctrl+Z takes it back)");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---- plugins ------------------------------------------------------------------------------------------------------------
namespace {
ImGuiKey ImGuiKeyNamed(const std::string& k) {
    if (k.size() == 1) {
        const char c = k[0];
        switch (c) {
            case 'A': return ImGuiKey_A; case 'B': return ImGuiKey_B; case 'C': return ImGuiKey_C; case 'D': return ImGuiKey_D;
            case 'E': return ImGuiKey_E; case 'F': return ImGuiKey_F; case 'G': return ImGuiKey_G; case 'H': return ImGuiKey_H;
            case 'I': return ImGuiKey_I; case 'J': return ImGuiKey_J; case 'K': return ImGuiKey_K; case 'L': return ImGuiKey_L;
            case 'M': return ImGuiKey_M; case 'O': return ImGuiKey_O; case 'P': return ImGuiKey_P; case 'Q': return ImGuiKey_Q;
            case 'R': return ImGuiKey_R; case 'S': return ImGuiKey_S; case 'T': return ImGuiKey_T; case 'V': return ImGuiKey_V;
            case 'W': return ImGuiKey_W; case 'X': return ImGuiKey_X; case 'Y': return ImGuiKey_Y; case 'Z': return ImGuiKey_Z;
#ifndef OKAY_ON_OKAYUI   // (the OkayUI compatibility names do not have these yet)
            case 'N': return ImGuiKey_N; case 'U': return ImGuiKey_U;
            case '4': return ImGuiKey_4; case '5': return ImGuiKey_5; case '6': return ImGuiKey_6; case '7': return ImGuiKey_7;
            case '8': return ImGuiKey_8; case '9': return ImGuiKey_9;
#endif
            case '0': return ImGuiKey_0; case '1': return ImGuiKey_1; case '2': return ImGuiKey_2; case '3': return ImGuiKey_3;
            case '/': return ImGuiKey_Slash; case '.': return ImGuiKey_Period; case ',': return ImGuiKey_Comma;
            case ';': return ImGuiKey_Semicolon; case '[': return ImGuiKey_LeftBracket; case ']': return ImGuiKey_RightBracket;
            case '-': return ImGuiKey_Minus; case '=': return ImGuiKey_Equal; case '`': return ImGuiKey_GraveAccent;
            default: return ImGuiKey_None;
        }
    }
    if (k == "F1") return ImGuiKey_F1;
    if (k == "F2") return ImGuiKey_F2;
    if (k == "F3") return ImGuiKey_F3;
    if (k == "F5") return ImGuiKey_F5;
    if (k == "F9") return ImGuiKey_F9;
    if (k == "F10") return ImGuiKey_F10;
    if (k == "F11") return ImGuiKey_F11;
    if (k == "F12") return ImGuiKey_F12;
    if (k == "ENTER") return ImGuiKey_Enter;
    if (k == "SPACE") return ImGuiKey_Space;
    if (k == "TAB") return ImGuiKey_Tab;
    if (k == "DELETE") return ImGuiKey_Delete;
    if (k == "HOME") return ImGuiKey_Home;
    if (k == "END") return ImGuiKey_End;
    if (k == "UP") return ImGuiKey_UpArrow;
    if (k == "DOWN") return ImGuiKey_DownArrow;
    if (k == "LEFT") return ImGuiKey_LeftArrow;
    if (k == "RIGHT") return ImGuiKey_RightArrow;
    return ImGuiKey_None;
}
} // namespace

bool RunPluginShortcut() {
    const ImGuiIO& io = ImGui::GetIO();
    for (const pluginreg::Command* c : pluginreg::ScriptEditorPlugins().Commands()) {
        pluginreg::Shortcut s;
        if (c->shortcut.empty() || !pluginreg::ParseShortcut(c->shortcut, s)) continue;
        if (s.ctrl != io.KeyCtrl || s.shift != io.KeyShift || s.alt != io.KeyAlt) continue;
        const ImGuiKey key = ImGuiKeyNamed(s.key);
        if (key == ImGuiKey_None || !ImGui::IsKeyPressed(key, false)) continue;
        const std::string id = c->id;                 // the command may reload plugins: copy the id first
        pluginreg::ScriptEditorPlugins().Run(id);
        return true;
    }
    return false;
}

bool DrawPluginPanels() {
    const auto& panels = pluginreg::ScriptEditorPlugins().Panels();
    if (panels.empty()) return false;
    for (std::size_t i = 0; i < panels.size(); ++i) {
        const std::string hdr = panels[i].title + "##pp" + panels[i].id;
        if (!ImGui::CollapsingHeader(hdr.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
        ImGui::PushID((int)i);
        const auto draw = panels[i].draw;             // a copy: drawing may reload the plugin and change the list
        DrawPluginPanelBody(draw);
        ImGui::PopID();
        if (i >= pluginreg::ScriptEditorPlugins().Panels().size()) break;
    }
    return true;
}

} // namespace oked::ide
