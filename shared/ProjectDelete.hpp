#pragma once
// Deleting a project from the launcher's Create page: what counts as a project
// folder (the same test the project list uses) and which folders the launcher
// never deletes. Header-only so the launcher and the tests share it.
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace okayproj {
namespace fs = std::filesystem;

// The project a scene belongs to: the folder that holds the nearest "Assets" folder above the scene
// (<root>/Assets/Scenes/<scene>.okayscene, <root>/Assets/<scene>.okayscene -> <root>), or the scene's own folder for a
// loose <root>/<scene>.okayscene. Text only. Never "Assets" or a folder inside it, so "Delete project" takes the project.
inline fs::path RootOfScene(const fs::path& scene) {
    const fs::path dir = scene.parent_path();
    int depth = 0;
    for (fs::path p = dir; !p.empty() && depth < 32; p = p.parent_path(), ++depth) {
        if (p.filename() == "Assets") return p.parent_path();
        if (p == p.parent_path()) break;
    }
    return dir;
}

// The scenes under <root>/Assets, shallowest first, then by path; at most `limit` (0 = all). Hidden folders (".okay")
// are skipped; the walk stops after `maxEntries` entries so a huge Assets folder can't stall the launcher.
inline std::vector<fs::path> ScenesUnderAssets(const fs::path& root, std::size_t limit = 0, std::size_t maxEntries = 20000) {
    std::vector<fs::path> found;
    std::error_code ec;
    const fs::path assets = root / "Assets";
    if (!fs::is_directory(assets, ec)) return found;
    std::size_t seen = 0;
    for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (++seen > maxEntries) break;
        std::error_code e2;
        const std::string nm = it->path().filename().string();
        if (it->is_directory(e2)) { if (!nm.empty() && nm[0] == '.') it.disable_recursion_pending(); continue; }
        if (it->path().extension() == ".okayscene") found.push_back(it->path());
    }
    auto depthOf = [](const fs::path& p) { return std::distance(p.begin(), p.end()); };
    std::sort(found.begin(), found.end(), [&](const fs::path& a, const fs::path& b) {
        const auto da = depthOf(a), db = depthOf(b);
        return da != db ? da < db : a < b;
    });
    if (limit && found.size() > limit) found.resize(limit);
    return found;
}

// True when <root>/Assets holds no file at all (only folders, or nothing): a project just made, before its first save.
inline bool AssetsHasNoFiles(const fs::path& root) {
    std::error_code ec;
    const fs::path assets = root / "Assets";
    std::size_t seen = 0;
    for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (++seen > 2000) return false;
        std::error_code e2;
        if (!it->is_directory(e2)) return false;
    }
    return !ec;
}

// A project as the Create page lists one: <root>/Assets holding a .okayscene somewhere inside it, or no file yet (just
// made; the editor hasn't saved its first scene).
inline bool IsProjectFolder(const fs::path& root) {
    std::error_code ec;
    const fs::path assets = root / "Assets";
    if (!fs::is_directory(assets, ec)) return false;
    if (AssetsHasNoFiles(root)) return true;
    return !ScenesUnderAssets(root, 1).empty();
}

// Absolute, links resolved, no trailing separator.
inline fs::path Normalized(const fs::path& p) {
    std::error_code ec;
    fs::path n = fs::weakly_canonical(p, ec);
    if (ec) n = fs::absolute(p, ec).lexically_normal();
    if (n.filename().empty() && n.has_relative_path()) n = n.parent_path();
    return n;
}

// `p` is `dir` or somewhere inside it (both Normalized).
inline bool IsSameOrInside(const fs::path& p, const fs::path& dir) {
    auto a = p.begin();
    for (auto b = dir.begin(); b != dir.end(); ++a, ++b) {
        if (a == p.end()) return false;
        fs::path::string_type x = a->native(), y = b->native();
#if defined(_WIN32)
        for (auto& c : x) c = (wchar_t)std::towlower(c);
        for (auto& c : y) c = (wchar_t)std::towlower(c);
#endif
        if (x != y) return false;
    }
    return true;
}

struct Kept { fs::path dir; std::string what; };   // e.g. {home, "your home folder"}

// Why `root` can't be deleted as a project folder, or "" when it can.
inline std::string DeleteRefusal(const fs::path& root, const std::vector<Kept>& keep) {
    if (root.empty()) return "No folder given.";
    fs::path given = root;
    if (given.filename().empty() && given.has_relative_path()) given = given.parent_path();
    std::error_code ec;
    if (!fs::exists(given, ec)) return "The folder is already gone.";
    if (fs::is_symlink(given, ec)) return "The folder is a link, not the project itself.";
    const fs::path n = Normalized(root);
    const fs::path rel = n.relative_path();
    if (n.empty() || rel.empty() ||
        (n.root_name().native().size() > 2 && std::distance(rel.begin(), rel.end()) == 1))   // \\server\share
        return "That's the root of a drive.";
    for (const auto& k : keep)
        if (!k.dir.empty() && IsSameOrInside(Normalized(k.dir), n)) return "It is or holds " + k.what + ".";
    if (!IsProjectFolder(n)) return "It isn't an OkaySpace project folder (no Assets folder with a scene).";
    return "";
}

}  // namespace okayproj
