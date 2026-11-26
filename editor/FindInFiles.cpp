// Find in Files (FindInFiles.hpp): search every project script for a term; a result opens the Script Editor at
// that line, with Match case / Whole word / Regex and Replace in All Files. Moved out of main.cpp (docs/editor/main_split_plan.md, move 5).
#include "EditorCommon.hpp"
#include "FindInFiles.hpp"
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "ScriptIde.hpp"           // scriptide::FindInTexts / ReplaceAll: the search with Match case, Whole word and Regex

#include <map>
#include <sstream>

namespace oked {

// Lowercase a copy of a string (main.cpp). Declared here, not in MainShared.hpp: nine other editor files that include
// MainShared.hpp have their own Lower (a known duplicate, docs/editor/main_split_plan.md section 5).
std::string Lower(std::string s);

// ---- Find in Files (Ctrl+Shift+F in the Script Editor) ---------------------
// Searches every project script (.okay/.lua/.cs/.okayvs under Assets) for a
// term; clicking a result opens the file in the Script Editor at that line.
bool g_showFindInFiles = false;
bool g_fifFocus = false;
void DrawFindInFiles(EditorState& ed) {
    if (!g_showFindInFiles) return;
    static char query[128] = {0};
    static char replaceWith[128] = {0};
    static scriptide::FindOptions opt;        // Match case / Whole word / Regex (the Script Editor's find bar has the same three)
    static std::string error;
    struct Hit { std::string path; int line; std::string preview; };
    static std::vector<Hit> hits;
    static bool ran = false;
    ImGui::SetNextWindowSize(ImVec2(640, 440), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Find in Files", &g_showFindInFiles)) { ImGui::End(); return; }
    if (g_fifFocus) { ImGui::SetKeyboardFocusHere(); g_fifFocus = false; }
    ImGui::SetNextItemWidth(-90);
    bool go = ImGui::InputTextWithHint("##fifq", "Search every project script...",
                                       query, sizeof(query), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Search", ImVec2(80, 0))) go = true;
    go |= ImGui::Checkbox("Match case", &opt.matchCase); ImGui::SameLine();
    go |= ImGui::Checkbox("Whole word", &opt.wholeWord); ImGui::SameLine();
    go |= ImGui::Checkbox("Regex", &opt.regex);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A regular expression (ECMAScript); the replacement can use $1, $2 and $&.");
    // Every project script's text (.okay / .lua / .cs / .okayvs under Assets), read when a search or a replace runs.
    auto readAll = [&]() {
        std::map<std::string, std::string> texts;
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path root = ed.projectDir().empty() ? fs::path(".") : fs::path(ed.projectDir()) / "Assets";
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = Lower(it->path().extension().string());
            if (ext != ".okay" && ext != ".lua" && ext != ".cs" && ext != ".okayvs") continue;
            std::ifstream f(it->path(), std::ios::binary);
            if (!f) continue;
            std::stringstream ss; ss << f.rdbuf();
            texts[it->path().string()] = ss.str();
        }
        return texts;
    };
    if (go && query[0]) {
        hits.clear(); ran = true;
        for (const auto& m : scriptide::FindInTexts(readAll(), query, opt, 200, &error)) {
            std::string line = m.lineText;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::size_t a = line.find_first_not_of(" \t");
            hits.push_back({m.file, m.match.line, a == std::string::npos ? line : line.substr(a)});
        }
    }
    // Replace in every file: each changed script is written back (an open, unchanged tab picks the new text up; one with
    // unsaved edits asks which to keep, as for any outside change).
    ImGui::SetNextItemWidth(-170);
    ImGui::InputTextWithHint("##fifr", "Replace with...", replaceWith, sizeof(replaceWith));
    ImGui::SameLine();
    ImGui::BeginDisabled(!query[0] || hits.empty());
    if (ImGui::Button("Replace in All Files", ImVec2(160, 0))) {
        int total = 0, files = 0;
        for (const auto& kv : readAll()) {
            int n = 0;
            const std::string out = scriptide::ReplaceAll(kv.second, query, replaceWith, opt, &n, &error);
            if (n <= 0) continue;
            std::ofstream f(kv.first, std::ios::binary);
            if (f << out) { total += n; ++files; }
        }
        ConsoleLog("Replaced " + std::to_string(total) + " match" + (total == 1 ? "" : "es") + " in " + std::to_string(files) + " file" + (files == 1 ? "" : "s"));
        hits.clear(); ran = false;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (!error.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.5f, 1.0f), "%s", error.c_str());
    else if (!ran)
        ImGui::TextDisabled("Type a term and press Enter. Searches .okay / .lua / .cs scripts under Assets.");
    else if (hits.empty())
        ImGui::TextDisabled("No matches for \"%s\".", query);
    else
        ImGui::TextDisabled("%d match%s%s", (int)hits.size(), hits.size() == 1 ? "" : "es",
                            hits.size() >= 200 ? " (capped at 200)" : "");
    ImGui::BeginChild("##fifres");
    int idx = 0;
    for (const Hit& h : hits) {
        ImGui::PushID(idx++);
        std::string fn = std::filesystem::path(h.path).filename().string();
        char lbl[96]; std::snprintf(lbl, sizeof(lbl), "%s:%d", fn.c_str(), h.line);
        if (ImGui::Selectable(lbl)) OpenScriptFileInEditorAt(h.path, h.line);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", h.path.c_str());
        ImGui::SameLine(180.0f);
        ImGui::TextDisabled("%.120s", h.preview.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace oked
