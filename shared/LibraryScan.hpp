#pragma once
// The launcher's library scan: every *.okayscene under a list of folders, each file ONCE.
//
// The launcher looks in its own folder AND in games/, Projects/, community/ under it, plus the folders the user
// added. Those overlap: the launcher's folder contains the other three, and a user folder can sit inside one of
// them. The scan used to list a file once per folder it was found under, so every community game showed twice on
// the Community page (and counted twice in Play). Here a file found again under a second folder is skipped; the
// first place it was found decides its position, so the order is the order of `roots`, then the walk order.
//
// Std only (no SDL, no UI), unit tested in tests/test_library_scan.cpp.
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace okaylib {

/// The same file always gives the same key, whichever folder it was reached through ("a/../b", symlinks).
inline std::string SceneKey(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
    if (ec || c.empty()) c = std::filesystem::absolute(p, ec).lexically_normal();
    std::string key = c.generic_string();
#ifdef _WIN32
    // Windows file names ignore case: "Games" and "games" are one folder, so they must give one key (ASCII letters;
    // weakly_canonical keeps the case it was given for the part it does not look up).
    for (char& ch : key) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
#endif
    return key;
}

/// Every *.okayscene under each folder of `roots` (recursively), once each. `skipDir` returns true for a folder
/// not to walk into (the official game's install folder); it may be empty. Missing folders are ignored.
inline std::vector<std::filesystem::path> FindScenesUnder(
        const std::vector<std::filesystem::path>& roots,
        const std::function<bool(const std::filesystem::directory_entry&)>& skipDir = {}) {
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    std::set<std::string> seen;
    for (const fs::path& dir : roots) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (skipDir && skipDir(*it)) { it.disable_recursion_pending(); continue; }
            std::error_code fec;
            if (it->is_regular_file(fec) && it->path().extension() == ".okayscene" && seen.insert(SceneKey(it->path())).second)
                out.push_back(it->path());
        }
    }
    return out;
}

// ---- Exported games: the only things the launcher plays (owner, 2026-11-28: "i dont want users to be able to play
// anything that wasnt exported") ----------------------------------------------------------------------------------
// What the editor's Build (editor/BuildGame.cpp, engine/src/Core/BuildPipeline.cpp PlanLayout) writes, and so what we
// look for:
//   <root>/<Game>.exe (the player, renamed; no extension on Linux)
//   <data>/game.okayconfig and <data>/*.okayscene, where <data> is <root> itself (the compact layout), <root>/Data
//   (compact with a data folder, or anti-cheat) or <root>/<Game>_Data (the folder layout).
// A project is a folder with project.okayproj (editor/ProjectState.hpp) and its scenes under Assets/; it has neither
// file, and a folder that holds an .okayproj is never an export even if someone copied a game.okayconfig into it. Only
// the folder itself and its data folder are looked at, so an export the editor put inside its project (the default
// output folder is relative to the project) still counts.

/// Is `f` a program a Build put at a game's root: a `.exe` (any case), or on other systems an executable file without
/// an extension?
inline bool IsGameProgram(const std::filesystem::path& f) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(f, ec)) return false;
    std::string ext = f.extension().string();
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (ext == ".exe") return true;
#ifdef _WIN32
    return false;
#else
    if (!ext.empty()) return false;
    const fs::perms p = fs::status(f, ec).permissions();
    return !ec && (p & fs::perms::owner_exec) != fs::perms::none;
#endif
}

/// Does folder `d` hold a project file (*.okayproj)?
inline bool HasProjectFile(const std::filesystem::path& d) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (it->path().extension() == ".okayproj" && it->is_regular_file(fec)) return true;
    }
    return false;
}

/// Does folder `d` hold a game program (IsGameProgram)?
inline bool HasGameProgram(const std::filesystem::path& d) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (IsGameProgram(it->path())) return true;
    return false;
}

/// The root folder of the exported game that `p` belongs to, or "" when it is not one. `p` is a scene (*.okayscene)
/// or a folder (the export's root or its data folder). The scene's folder (the data folder) must hold game.okayconfig,
/// the root must hold the game's program, and neither may hold a project file.
inline std::filesystem::path ExportedGameRoot(const std::filesystem::path& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (p.empty()) return {};
    std::vector<fs::path> dataDirs;
    if (fs::is_directory(p, ec)) {
        dataDirs.push_back(p);
        dataDirs.push_back(p / "Data");
        for (auto it = fs::directory_iterator(p, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const std::string n = it->path().filename().string();
            std::error_code dec;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, "_Data") == 0 && it->is_directory(dec)) dataDirs.push_back(it->path());
        }
    } else {
        if (p.extension() != ".okayscene" || !fs::is_regular_file(p, ec)) return {};
        dataDirs.push_back(p.parent_path());
    }
    for (const fs::path& data : dataDirs) {
        std::error_code dec;
        if (!fs::is_regular_file(data / "game.okayconfig", dec) || HasProjectFile(data)) continue;
        const std::string dn = data.filename().string();
        const bool inDataFolder = dn == "Data" || (dn.size() > 5 && dn.compare(dn.size() - 5, 5, "_Data") == 0);
        if (HasGameProgram(data)) return data;                        // the compact layout: the program beside the data
        if (inDataFolder && HasGameProgram(data.parent_path()) && !HasProjectFile(data.parent_path()))
            return data.parent_path();                                // Data/ or <Game>_Data/ under the program's folder
    }
    return {};
}

/// THE check for "may the launcher play this?": only an exported game (ExportedGameRoot). A project folder, an
/// .okayproj, a project's scene, a loose scene or a data folder with no program is not one.
inline bool IsExportedGame(const std::filesystem::path& p) { return !ExportedGameRoot(p).empty(); }

/// What the launcher says when it refuses to play something that is not an exported game.
inline const char* NotExportedMessage() { return "Export this project from the editor (Build) to play it."; }

} // namespace okaylib
