// Find in Files (FindInFiles.hpp): search every project script for a term; a result opens the Script Editor at
// that line, with Match case / Whole word / Regex and Replace in All Files. Moved out of main.cpp (docs/editor/main_split_plan.md, move 5).
#include "EditorCommon.hpp"
#include "FindInFiles.hpp"
#include "MainShared.hpp"          // OpenScriptFileInEditorAt (main.cpp)
#include "OkayPanels.hpp"          // DrawFindInFilesOkayUI
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
// OkayUI draws the window (DrawFindInFilesOkayUI, no Dear ImGui version); the search and the opening happen here.
bool g_showFindInFiles = false;
bool g_fifFocus = false;
namespace {
// Every project script's text (.okay / .lua / .cs / .okayvs under Assets), read when a search or a replace runs.
std::map<std::string, std::string> ReadProjectScripts(const EditorState& ed) {
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
}

scriptide::FindOptions OptionsOf(const okaypanels::FindInFilesState& st) {
    scriptide::FindOptions opt;
    opt.matchCase = st.matchCase; opt.wholeWord = st.wholeWord; opt.regex = st.regex;
    return opt;
}

void SearchScripts(const EditorState& ed, const okaypanels::FindInFilesState& st, okaypanels::FindInFilesModel& m) {
    m.hits.clear(); m.error.clear(); m.ran = true;
    for (const auto& fm : scriptide::FindInTexts(ReadProjectScripts(ed), st.query, OptionsOf(st), okaypanels::kFindInFilesCap, &m.error)) {
        std::string line = fm.lineText;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::size_t a = line.find_first_not_of(" \t");
        m.hits.push_back({fm.file, fm.match.line, a == std::string::npos ? line : line.substr(a)});
    }
}

// Replace in every file: each changed script is written back (an open, unchanged tab picks the new text up; one with
// unsaved edits asks which to keep, as for any outside change).
void ReplaceInScripts(const EditorState& ed, const okaypanels::FindInFilesState& st, okaypanels::FindInFilesModel& m) {
    int total = 0, files = 0;
    m.error.clear();
    for (const auto& kv : ReadProjectScripts(ed)) {
        int n = 0;
        const std::string out = scriptide::ReplaceAll(kv.second, st.query, st.replaceWith, OptionsOf(st), &n, &m.error);
        if (n <= 0) continue;
        std::ofstream f(kv.first, std::ios::binary);
        if (f << out) { total += n; ++files; }
    }
    ConsoleLog("Replaced " + std::to_string(total) + " match" + (total == 1 ? "" : "es") + " in " + std::to_string(files) + " file" + (files == 1 ? "" : "s"));
    m.hits.clear(); m.ran = false;
}
} // namespace

void DrawFindInFiles(EditorState& ed) {
    if (!g_showFindInFiles) return;
    static okaypanels::FindInFilesModel model;
    static okaypanels::FindInFilesState state;
    model.focusQuery = g_fifFocus;
    okaypanels::FindInFilesIntent intent;
    if (DrawFindInFilesOkayUI(&g_showFindInFiles, model, state, intent)) g_fifFocus = false;   // the field took the keyboard
    if (intent.search && state.query[0]) SearchScripts(ed, state, model);
    if (intent.replaceAll && okaypanels::FindInFilesCanReplace(model, state)) ReplaceInScripts(ed, state, model);
    if (intent.open >= 0 && (std::size_t)intent.open < model.hits.size()) {
        const okaypanels::FindHit hit = model.hits[(std::size_t)intent.open];   // a copy: opening the script may draw again
        OpenScriptFileInEditorAt(hit.path, hit.line);
    }
}

} // namespace oked
