// Find in Files (FindInFiles.hpp): search every project script for a term; a result opens the Script Editor at
// that line. Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 5).
#include "EditorCommon.hpp"
#include "FindInFiles.hpp"
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)

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
    struct Hit { std::string path; int line; std::string preview; };
    static std::vector<Hit> hits;
    static bool ran = false;
    ImGui::SetNextWindowSize(ImVec2(600, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Find in Files", &g_showFindInFiles)) { ImGui::End(); return; }
    if (g_fifFocus) { ImGui::SetKeyboardFocusHere(); g_fifFocus = false; }
    ImGui::SetNextItemWidth(-90);
    bool go = ImGui::InputTextWithHint("##fifq", "Search every project script...",
                                       query, sizeof(query), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Search", ImVec2(80, 0))) go = true;
    if (go && query[0]) {
        hits.clear(); ran = true;
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path root = ed.projectDir().empty() ? fs::path(".") : fs::path(ed.projectDir()) / "Assets";
        std::string needle = Lower(query);
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator() && hits.size() < 200; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = Lower(it->path().extension().string());
            if (ext != ".okay" && ext != ".lua" && ext != ".cs" && ext != ".okayvs") continue;
            std::ifstream f(it->path(), std::ios::binary);
            if (!f) continue;
            std::string line; int ln = 0;
            while (std::getline(f, line) && hits.size() < 200) {
                ++ln;
                if (Lower(line).find(needle) == std::string::npos) continue;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                std::size_t a = line.find_first_not_of(" \t");
                hits.push_back({it->path().string(), ln,
                                a == std::string::npos ? line : line.substr(a)});
            }
        }
    }
    ImGui::Separator();
    if (!ran)
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
