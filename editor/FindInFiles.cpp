// Find in Files (FindInFiles.hpp): search every project script for a term; a result opens the Script Editor at
// that line. Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 5).
#include "EditorCommon.hpp"
#include "FindInFiles.hpp"
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)
#include "OkayPanels.hpp"          // DrawFindInFilesOkayUI

namespace oked {

// Lowercase a copy of a string (main.cpp). Declared here, not in MainShared.hpp: nine other editor files that include
// MainShared.hpp have their own Lower (a known duplicate, docs/editor/main_split_plan.md section 5).
std::string Lower(std::string s);

// ---- Find in Files (Ctrl+Shift+F in the Script Editor) ---------------------
// Searches every project script (.okay/.lua/.cs/.okayvs under Assets) for a
// term; clicking a result opens the file in the Script Editor at that line.
// OkayUI draws the window (DrawFindInFilesOkayUI, no Dear ImGui version); the search and the opening happen here.
bool g_showFindInFiles = false;
bool g_fifFocus = false;

namespace {
void SearchScripts(const EditorState& ed, const char* query, std::vector<okaypanels::FindHit>& hits) {
    hits.clear();
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root = ed.projectDir().empty() ? fs::path(".") : fs::path(ed.projectDir()) / "Assets";
    std::string needle = Lower(query);
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && hits.size() < okaypanels::kFindInFilesCap; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = Lower(it->path().extension().string());
        if (ext != ".okay" && ext != ".lua" && ext != ".cs" && ext != ".okayvs") continue;
        std::ifstream f(it->path(), std::ios::binary);
        if (!f) continue;
        std::string line; int ln = 0;
        while (std::getline(f, line) && hits.size() < okaypanels::kFindInFilesCap) {
            ++ln;
            if (Lower(line).find(needle) == std::string::npos) continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::size_t a = line.find_first_not_of(" \t");
            hits.push_back({it->path().string(), ln, a == std::string::npos ? line : line.substr(a)});
        }
    }
}
} // namespace

void DrawFindInFiles(EditorState& ed) {
    if (!g_showFindInFiles) return;
    static okaypanels::FindInFilesModel model;
    static okaypanels::FindInFilesState state;
    model.focusQuery = g_fifFocus;
    okaypanels::FindInFilesIntent intent;
    if (DrawFindInFilesOkayUI(&g_showFindInFiles, model, state, intent)) g_fifFocus = false;   // the field took the keyboard
    if (intent.search && state.query[0]) { SearchScripts(ed, state.query, model.hits); model.ran = true; }
    if (intent.open >= 0 && (std::size_t)intent.open < model.hits.size()) {
        const okaypanels::FindHit hit = model.hits[(std::size_t)intent.open];   // a copy: opening the script may draw again
        OpenScriptFileInEditorAt(hit.path, hit.line);
    }
}

} // namespace oked
