#pragma once
// Deleting a project from the launcher's Create page: what counts as a project
// folder (the same test the project list uses) and which folders the launcher
// never deletes. Header-only so the launcher and the tests share it.
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace okayproj {
namespace fs = std::filesystem;

// <root>/Assets/<scene>.okayscene (or a loose <root>/<scene>.okayscene) -> <root>.
inline fs::path RootOfScene(const fs::path& scene) {
    fs::path root = scene.parent_path();
    if (root.filename() == "Assets") root = root.parent_path();
    return root;
}

// A project as the Create page lists one: <root>/Assets holding a .okayscene, or
// still empty (just made; the editor hasn't saved its first scene yet).
inline bool IsProjectFolder(const fs::path& root) {
    std::error_code ec;
    const fs::path assets = root / "Assets";
    if (!fs::is_directory(assets, ec)) return false;
    if (fs::is_empty(assets, ec)) return !ec;
    for (fs::directory_iterator it(assets, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".okayscene") return true;
    return false;
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
