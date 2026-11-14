#pragma once
// Naming a visual-script graph file (.okayvs). No ImGui and no engine types, so it is unit-tested
// (tests/test_vs_graph_naming.cpp). The editor (VisualScriptEditor.hpp) asks these functions:
//   * what name to suggest for an untitled graph (from the object's name, never an existing file),
//   * whether a name the user typed is allowed (and whether it would replace a file),
//   * where the file goes (the project's Assets folder, the same folder the editor always used),
//   * how to rename a graph file so scenes and other assets that point at it follow (that part
//     is done by the Project panel's okaypb::Browser::Rename; RenameGraph only checks the name first).
// The checks mirror okaypb::ValidateName (editor/ProjectBrowser.cpp) and add the .okayvs rules; the
// test compares the two on a list of names so they cannot drift apart.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::vsname {

inline constexpr const char* kExt = ".okayvs";
inline constexpr std::size_t kMaxStem = 100;   // characters of the name before ".okayvs"

inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline bool EndsWithExt(const std::string& s) {
    const std::size_t n = std::char_traits<char>::length(kExt);
    return s.size() >= n && Lower(s.substr(s.size() - n)) == kExt;
}
/// "Door.okayvs" -> "Door". A name without the extension is returned as it is.
inline std::string StripExt(const std::string& s) {
    return EndsWithExt(s) ? s.substr(0, s.size() - std::char_traits<char>::length(kExt)) : s;
}
inline std::string TrimSpaces(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

/// The folder new graphs go in: <project>/Assets, or ./Assets with no project open.
inline std::string ScriptsDir(const std::string& projectDir) {
    namespace fs = std::filesystem;
    const fs::path d = projectDir.empty() ? fs::path("Assets") : fs::path(projectDir) / "Assets";
    return d.generic_string();
}

/// "" when the typed name (with or without ".okayvs") is allowed, otherwise a short plain reason.
/// Spaces at the ends are NOT trimmed here: call TrimSpaces first when the box may hold them.
inline std::string ValidateGraphName(const std::string& typed) {
    const std::string stem = StripExt(typed);
    if (TrimSpaces(stem).empty()) return "Type a name.";
    if (stem.size() > kMaxStem) return "That name is too long (most 100 letters).";
    for (unsigned char c : stem) {
        if (c < 32 || c == 127) return "A name cannot contain control characters.";
        if (c == '/' || c == '\\') return "A name cannot contain / or \\ (it is a file name, not a path).";
        if (std::string(":*?\"<>|").find((char)c) != std::string::npos) return "A name cannot contain : * ? \" < > |";
    }
    if (stem.front() == '.') return "A name cannot start with a dot (it would be hidden).";
    if (stem.front() == ' ') return "A name cannot start with a space.";
    if (stem.back() == '.') return "A name cannot end with a dot.";
    if (stem.back() == ' ' || stem.back() == '\t') return "A name cannot end with a space.";
    {
        std::string base = Lower(stem);
        const auto d = base.find('.');
        if (d != std::string::npos) base = base.substr(0, d);   // Windows reserves "con.txt" too
        static const char* kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                          "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
        for (const char* r : kReserved)
            if (base == r) return "That name is reserved by Windows. Pick another.";
    }
    return {};
}

/// The file name for a typed name: the extension is always ".okayvs" (added when missing, kept once).
/// Call only with a name ValidateGraphName accepted.
inline std::string MakeFileName(const std::string& typed) { return StripExt(TrimSpaces(typed)) + kExt; }

/// True when `dir` holds a file or folder called `fileName` (compared without regard to case, as Windows does).
inline bool NameTaken(const std::string& dir, const std::string& fileName) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string want = Lower(fileName);
    if (!fs::is_directory(fs::path(dir), ec)) return false;
    for (const auto& e : fs::directory_iterator(fs::path(dir), ec))
        if (Lower(e.path().filename().string()) == want) return true;
    return false;
}

/// A name for an untitled graph, without the extension: "<ObjectName>Graph" (letters, digits and _ only) or "Script"
/// when no object is known; "Graph2", "Graph3"... when the file is already there. `alsoTaken` holds names other open
/// untitled graphs were already given (no extension) so two prompts do not suggest the same one.
inline std::string SuggestName(const std::string& dir, const std::string& objectName,
                               const std::vector<std::string>& alsoTaken = {}) {
    std::string stem = "Script";
    if (!objectName.empty()) {
        stem.clear();
        for (char c : objectName) stem.push_back((std::isalnum((unsigned char)c) || c == '_') ? c : '_');
        bool anyLetter = false;
        for (char c : stem) if (c != '_') anyLetter = true;
        stem = anyLetter ? stem + "Graph" : std::string("ObjectGraph");
        if (stem.size() > kMaxStem - 6) stem.resize(kMaxStem - 6);
        if (!ValidateGraphName(stem).empty()) stem = "ObjectGraph";   // e.g. an object called "CON"
    }
    auto used = [&](const std::string& s) {
        if (NameTaken(dir, s + kExt)) return true;
        for (const auto& t : alsoTaken) if (Lower(t) == Lower(s)) return true;
        return false;
    };
    std::string pick = stem;
    for (int k = 2; used(pick); ++k) pick = stem + std::to_string(k);
    return pick;
}

struct TargetCheck {
    bool ok = false;           ///< the name can be used (a replace still needs `needsConfirm` answered)
    bool needsConfirm = false; ///< the file exists: ask "Replace?"; call again with replaceConfirmed = true
    std::string error;         ///< plain reason when !ok and !needsConfirm
    std::string message;       ///< the question to show when needsConfirm
    std::string path;          ///< the full target path (forward slashes) when the name is valid
};

/// Where a graph typed as `typed` would be saved inside `dir`, and whether that is allowed.
/// `currentPath` is the graph's own file (may be empty): saving onto itself is not a replace.
inline TargetCheck CheckTarget(const std::string& dir, const std::string& typed, bool replaceConfirmed = false,
                               const std::string& currentPath = {}) {
    TargetCheck r;
    const std::string t = TrimSpaces(typed);
    if (std::string e = ValidateGraphName(t); !e.empty()) { r.error = e; return r; }
    const std::string fileName = MakeFileName(t);
    namespace fs = std::filesystem;
    r.path = (fs::path(dir) / fileName).generic_string();
    if (!currentPath.empty() && Lower(fs::path(currentPath).generic_string()) == Lower(r.path)) { r.ok = true; return r; }
    if (NameTaken(dir, fileName)) {
        if (!replaceConfirmed) {
            r.needsConfirm = true;
            r.message = "\"" + fileName + "\" already exists. Press the button again to replace it.";
            return r;
        }
        std::error_code ec;
        if (fs::is_directory(fs::path(r.path), ec)) { r.error = "\"" + fileName + "\" is a folder."; r.needsConfirm = false; return r; }
    }
    r.ok = true;
    return r;
}

/// "Replace?" without a second dialog: the first Check on a name that exists answers needsConfirm and remembers it;
/// the same name asked again (the user pressed the button again) is allowed. Any other name starts over.
struct ReplaceGate {
    std::string armed;   ///< lower-case target path the user was asked about
    TargetCheck Check(const std::string& dir, const std::string& typed, const std::string& currentPath = {}) {
        TargetCheck r = CheckTarget(dir, typed, false, currentPath);
        if (!r.needsConfirm) { armed.clear(); return r; }
        if (armed == Lower(r.path)) { armed.clear(); return CheckTarget(dir, typed, true, currentPath); }
        armed = Lower(r.path);
        return r;
    }
};

/// Where `path` is after the moves in `moves` (old -> new, as okaypb::Result::moved lists them; a moved folder
/// carries what is inside it). Comparison ignores case and slash style; the result keeps `path` unchanged
/// when nothing matches.
inline std::string RemapPath(const std::string& path, const std::vector<std::pair<std::string, std::string>>& moves) {
    auto norm = [](std::string s) { for (char& c : s) if (c == '\\') c = '/'; return s; };
    const std::string p = norm(path), lp = Lower(p);
    for (const auto& m : moves) {
        const std::string o = norm(m.first), lo = Lower(o);
        if (lp == lo) return norm(m.second);
        if (lp.size() > lo.size() && lp.compare(0, lo.size(), lo) == 0 && lp[lo.size()] == '/')
            return norm(m.second) + p.substr(o.size());
    }
    return path;
}

struct RenameCheck { bool ok = false; std::string error; std::string fileName; };

/// Checks a new name for renaming the graph file `path` (no overwrite, ever: a rename onto another file is refused).
inline RenameCheck CheckRename(const std::string& path, const std::string& typed) {
    RenameCheck r;
    const std::string t = TrimSpaces(typed);
    if (std::string e = ValidateGraphName(t); !e.empty()) { r.error = e; return r; }
    r.fileName = MakeFileName(t);
    namespace fs = std::filesystem;
    const fs::path from(path);
    if (Lower(from.filename().string()) == Lower(r.fileName)) { r.ok = true; return r; }   // same name: nothing to do
    if (NameTaken(from.parent_path().string().empty() ? std::string(".") : from.parent_path().string(), r.fileName)) {
        r.error = "\"" + r.fileName + "\" already exists here.";
        return r;
    }
    r.ok = true;
    return r;
}

/// Rename a graph file through the Project panel's Browser (`Browser` is okaypb::Browser; a template so this header
/// needs no link to it). The Browser keeps GUIDs, moves the .okaymeta and rewrites every scene, prefab or other
/// text asset that points at the old path. Returns the Browser's Result ("ok", "error", "paths", "moved",
/// "referencesRewritten", "filesRewritten"); the editor passes `moved` on to the open scene and the graph tabs.
template <class Browser>
inline auto RenameGraph(Browser& browser, const std::string& path, const std::string& typed) {
    decltype(browser.Rename(path, typed)) r;
    const RenameCheck c = CheckRename(path, typed);
    if (!c.ok) { r.error = c.error; return r; }
    return browser.Rename(path, c.fileName);
}

}  // namespace okay::editor::vsname
