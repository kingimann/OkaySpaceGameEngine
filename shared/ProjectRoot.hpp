#pragma once
// Which folder is "the project" for a scene. Header-only and free of engine, ImGui and SDL code, so the editor's version
// question (editor/ProjectUpgrade.*) and the launcher's question before it starts the editor (launcher/OpenProjectGate.hpp)
// use exactly the same rule, and cannot drift apart (the launcher does not link the engine).
//
// This was ProjectRootForScene and its helpers in editor/ProjectUpgrade.cpp. They moved here unchanged; the editor still
// calls them as okayup::ProjectRootForScene and friends (editor/ProjectUpgrade.hpp has using-declarations), and the tests
// of that file (tests/test_project_upgrade.cpp) still check them. docs/project_upgrade.md, "Which folder is the project",
// has the rules in words. Written from the folder layout the editor makes; no other code.
//
//   IsProjectRoot(dir)                 `dir` has an Assets folder or a project.okayproj
//   ProjectRootForScene(scene, rules)  the project a scene belongs to ("" for a loose file)
//   IsDriveRoot / SameFolder           the two folder questions the rule asks of the disk
//   RootRules / DefaultRootRules()     what the rule needs from the machine (tests pass their own)
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>

#include "ProjectCreate.hpp"
#include "ProjectVersion.hpp"

namespace okayroot {
namespace fs = std::filesystem;

/// What ProjectRootForScene needs from the machine, so the rules can be tried anywhere (tests pass their own).
struct RootRules {
    bool assetsAnyCase = false;   ///< an "Assets" folder the scene is inside may be spelled in any case (Windows)
    fs::path workingFolder;       ///< the editor's working folder: its project.okayproj is the old shared settings file
    fs::path home;                ///< the person's home folder: never a project
    fs::path ceiling;             ///< tests: the search goes no higher than this folder ("" = up to the drive root)
};

namespace detail {

inline std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A path as a comparison key without asking the disk anything: absolute, normalized, lower case on Windows.
inline std::string PathKey(const fs::path& p) {
    std::string s = okayproj::Absolute(p).lexically_normal().generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#if defined(_WIN32)
    s = Lower(s);
#endif
    return s;
}

// "Assets" as a folder name. Windows folder names ignore case (a project copied around may end up with "assets"), so
// there any case counts (`anyCase`); elsewhere the name must be exactly "Assets", as the editor makes it. Compared on the
// native characters, so a name that has no narrow spelling on Windows can't throw.
inline bool IsAssetsFolderName(const fs::path& name, bool anyCase) {
    const auto& n = name.native();
    static const char kWord[] = "Assets";
    if (n.size() != sizeof(kWord) - 1) return false;
    for (std::size_t i = 0; i < n.size(); ++i) {
        long c = static_cast<long>(n[i]);
        long w = static_cast<long>(static_cast<unsigned char>(kWord[i]));
        if (anyCase) {   // ASCII letters only
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (w >= 'A' && w <= 'Z') w += 'a' - 'A';
        }
        if (c != w) return false;
    }
    return true;
}

// The folder as the disk spells it: links resolved, absolute, no trailing separator (okayproj::Normalized's rule).
inline fs::path Canon(const fs::path& p) {
    std::error_code ec;
    fs::path n = fs::weakly_canonical(p, ec);
    if (ec || n.empty()) n = okayproj::Absolute(p);
    n = n.lexically_normal();
    if (n.filename().empty() && n.has_relative_path()) n = n.parent_path();
    return n;
}

// project.okayproj in `dir` with an engine_version line: written by an editor that knows about versions for this project.
inline bool HasStampedProjectFile(const fs::path& dir) {
    const okayver::Stamp st = okayver::ReadProjectStamp(dir.string());
    return st.fileFound && !st.version.empty();
}

}  // namespace detail

/// True when `dir` is a project folder: it has an Assets folder or a project.okayproj file in it. (A .okay folder alone
/// does not count: the editor leaves one next to any scene it opened, project or not.)
inline bool IsProjectRoot(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    // A ".okay" folder alone is not enough: the editor leaves one (autosave, the build list) next to any scene it opened, so a
    // loose scene folder would turn into a "project" that is asked about and copied whole by an upgrade (issue #888).
    return fs::is_directory(dir / "Assets", ec) || fs::is_regular_file(dir / okayproj::SettingsFileName(), ec);
}

/// True for the root of a drive or a network share ("C:\", "/", "\\server\share").
inline bool IsDriveRoot(const fs::path& dir) {
    if (dir.empty()) return false;
    const fs::path n = detail::Canon(dir);
    const fs::path rel = n.relative_path();
    if (rel.empty()) return true;                                                     // "/", "C:\"
    // "\\server\share": a network share's root (its name is part of the root on Windows)
    return n.root_name().native().size() > 2 && std::distance(rel.begin(), rel.end()) == 1;
}

/// `a` and `b` are the same folder on disk, however each is spelled (a link, 8.3 name, another case on Windows, a trailing
/// separator). False when either is missing.
inline bool SameFolder(const fs::path& a, const fs::path& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    const bool same = fs::equivalent(a, b, ec);                                       // the same folder on disk (links, 8.3 names, \\?\, case)
    if (!ec) return same;
    std::error_code e2;
    if (!fs::exists(a, e2) || !fs::exists(b, e2)) return false;                       // a missing folder is nobody's
    return detail::PathKey(detail::Canon(a)) == detail::PathKey(detail::Canon(b));    // the disk couldn't say: compare the spellings
}

/// This machine's rules: any case on Windows, the current folder, HOME (USERPROFILE on Windows).
inline RootRules DefaultRootRules() {
    RootRules r;
#if defined(_WIN32)
    r.assetsAnyCase = true;
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (home && *home) r.home = fs::path(home);
    std::error_code ec;
    r.workingFolder = fs::current_path(ec);
    if (ec) r.workingFolder.clear();
    return r;
}

/// The project folder a scene belongs to. From the scene's folder upwards, the nearest folder that is clearly a project:
///   - it holds the scene through a folder called Assets (any case when `assetsAnyCase`): <project>/Assets/.../scene;
///   - or it holds a project.okayproj with an engine_version stamp (not the working folder: its file is the old shared one,
///     which a release build stamps too).
/// Only when no folder up the path is clearly a project: the scene's own folder when it holds project.okayproj or an Assets
/// folder (the old flat layout; not the working folder). A drive root and the home folder are never a project. Not evidence:
/// an unstamped project.okayproj above the scene (a copy of the shared file) and an Assets folder beside a folder above the
/// scene (the release folder holds Assets/ next to examples/). "" for a loose file.
inline std::string ProjectRootForScene(const std::string& scene, const RootRules& rules) {
    if (scene.empty()) return {};
    std::error_code ec;
    fs::path abs = fs::absolute(fs::path(scene), ec);
    if (ec) return {};
    abs = abs.lexically_normal();
    const fs::path dir = abs.parent_path();
    auto never = [&](const fs::path& p) { return IsDriveRoot(p) || (!rules.home.empty() && SameFolder(p, rules.home)); };
    auto working = [&](const fs::path& p) { return !rules.workingFolder.empty() && SameFolder(p, rules.workingFolder); };
    // The nearest folder that is clearly a project: it holds the scene through Assets/, or it has a stamped project.okayproj.
    const fs::path ceiling = rules.ceiling.empty() ? fs::path() : okayproj::Absolute(rules.ceiling);
    fs::path child;   // the folder below `p` on the way to the scene
    for (fs::path p = dir; !p.empty(); child = p, p = p.parent_path()) {
        if (!ceiling.empty() && child == ceiling) break;                             // tests: nothing above the ceiling
        if (!never(p)) {
            if (!child.empty() && detail::IsAssetsFolderName(child.filename(), rules.assetsAnyCase) && fs::is_directory(child, ec)) return p.string();
            if (!working(p) && detail::HasStampedProjectFile(p)) return p.string();
        }
        if (p == p.parent_path()) break;
    }
    // The old flat layout: project.okayproj (or Assets) right beside the scene.
    if (IsProjectRoot(dir) && !never(dir) && !working(dir)) return dir.string();
    return {};
}

/// ProjectRootForScene with this machine's rules (DefaultRootRules()).
inline std::string ProjectRootForScene(const std::string& scene) { return ProjectRootForScene(scene, DefaultRootRules()); }

/// The project a scene belongs to and the engine version stamped in it: what the launcher needs to ask the version question
/// before it starts the editor. Reads project.okayproj, never writes.
struct ProjectInfo {
    std::string root;      ///< the project folder ("" = the scene is a loose file: nothing to ask about)
    std::string version;   ///< the stamp, normalized ("" = none recorded, or no project)
};
inline ProjectInfo ProjectInfoForScene(const std::string& scene, const RootRules& rules) {
    ProjectInfo info;
    info.root = ProjectRootForScene(scene, rules);
    if (!info.root.empty()) info.version = okayver::ReadProjectVersion(info.root);
    return info;
}
inline ProjectInfo ProjectInfoForScene(const std::string& scene) { return ProjectInfoForScene(scene, DefaultRootRules()); }

}  // namespace okayroot
