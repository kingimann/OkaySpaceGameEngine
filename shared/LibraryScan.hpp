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
    return c.generic_string();
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

} // namespace okaylib
