#pragma once
// Where a file named inside a model file may come from: OBJ/MTL textures (`map_Kd` and friends), the `mtllib` material
// files, Mesh::LoadOBJ, and (through ResolveInsideDir in engine/src/Render/SceneRefPath.hpp) the files a scene file names.
//
// A downloaded model must not be able to point the engine at any file on the machine (#753). So:
//   * First, on the text alone and before anything touches the disk, a path is refused when it is empty, holds a NUL,
//     a wildcard or device character (? * " < > |), starts with two slashes or backslashes (a network share
//     "\\host\share" or a device path "\\?\C:\x": opening one can make Windows connect to another machine and send the
//     user's login hash), or holds a ':' other than one drive letter followed by a slash ("C:/x"; "C:x" is refused).
//   * A relative path that stays inside the model's own folder is kept (its "." and ".." resolved on the text), as
//     before, unless links on the disk lead it out of that folder (and out of every allowed root): then it is dropped.
//   * An absolute path, or a relative one whose ".." climbs out of the model's folder, is kept only when its text lies
//     inside one of the allowed roots, and then also the real file (links followed) does. Otherwise it is dropped.
// The allowed roots are empty by default, which is exactly the behaviour since #753 (nothing outside the model's folder).
// The editor sets them to the open project's folder (EditorState::setProjectDir), so a model inside a project may use
// textures from anywhere in that project, written as an absolute path or with "..", as exporters often do. A root must
// be an existing, absolute folder at least two levels deep that is neither a drive or file-system root nor the user's
// home folder (or a folder holding it); other roots are refused.
//
// Written for this engine from the path rules of the file systems (POSIX path resolution, the Windows path formats);
// no other project's code was used.
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace okay {

namespace texture_path_detail {
struct Root { std::filesystem::path lexical, canonical; };
inline std::mutex& Mutex() { static std::mutex m; return m; }
inline std::vector<Root>& Roots() { static std::vector<Root> r; return r; }

// `p` made absolute and canonical as far as it exists (links followed), without throwing.
inline bool Canonical(const std::filesystem::path& p, std::filesystem::path& out) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (ec) return false;
    out = std::filesystem::weakly_canonical(abs, ec);
    if (ec) return false;
    out = out.lexically_normal();
    return true;
}

inline bool SameComponent(const std::filesystem::path& a, const std::filesystem::path& b) {
#ifdef _WIN32
    std::wstring x = a.native(), y = b.native();   // Windows paths compare without regard to case
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i) if (towlower(x[i]) != towlower(y[i])) return false;
    return true;
#else
    return a == b;
#endif
}

// True when `p` is `root` or lies below it, component by component (so "/proj2" is not inside "/proj").
inline bool Inside(const std::filesystem::path& p, const std::filesystem::path& root) {
    auto r = root.begin(), q = p.begin();
    for (; r != root.end(); ++r, ++q) {
        if (r->empty()) continue;   // a trailing separator
        if (q == p.end() || !SameComponent(*r, *q)) return false;
    }
    return true;
}

enum class Kind { Refused, Relative, Absolute };

// The text-only screen (slashes already forward). Nothing here touches the disk.
inline Kind Screen(const std::string& p, std::string* why) {
    auto refuse = [&](const char* w) { if (why) *why = w; return Kind::Refused; };
    if (p.empty()) return refuse("an empty file name");
    if (p.find('\0') != std::string::npos) return refuse("a file name with a NUL character");
    if (p.find_first_of("?*\"<>|") != std::string::npos) return refuse("a file name with a wildcard or device character");
    if (p.size() >= 2 && p[0] == '/' && p[1] == '/') return refuse("a network or device path (\\\\host\\share, \\\\?\\) is not allowed");
    const std::size_t colon = p.find(':');
    if (colon != std::string::npos) {
        const bool drive = colon == 1 && std::isalpha((unsigned char)p[0]) && p.size() > 2 && p[2] == '/' &&
                           p.find(':', 2) == std::string::npos;
        if (!drive) return refuse("a ':' is only allowed after a drive letter (C:/...)");
        return Kind::Absolute;
    }
    return p[0] == '/' ? Kind::Absolute : Kind::Relative;
}

// "a/./b/../c" -> {"a", "c"}. False when a ".." climbs above the start.
inline bool NormalizeRelative(const std::string& p, std::vector<std::string>& parts) {
    parts.clear();
    std::size_t i = 0;
    while (i <= p.size()) {
        std::size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string comp = p.substr(i, j - i);
        i = j + 1;
        if (comp.empty() || comp == ".") continue;
        if (comp == "..") {
            if (parts.empty()) return false;
            parts.pop_back();
            continue;
        }
        parts.push_back(comp);
    }
    return true;
}

inline int Depth(const std::filesystem::path& p) {
    int n = 0;
    for (const auto& c : p.relative_path()) if (!c.empty()) ++n;
    return n;
}

inline std::filesystem::path HomeFolder() {
    const char* h = std::getenv("HOME");
#ifdef _WIN32
    if (!h || !*h) h = std::getenv("USERPROFILE");
#endif
    std::filesystem::path out;
    if (h && *h) Canonical(std::filesystem::u8path(h), out);
    return out;
}
} // namespace texture_path_detail

/// Set the folders whose files a model may name by absolute path or by "..". Replaces the previous list. A folder is
/// refused (left out) unless it is absolute, exists, is at least two levels deep, and is neither a drive or file-system
/// root nor the user's home folder or a folder holding it. Empty entries are ignored. Returns how many were kept.
/// Thread-safe.
inline std::size_t SetModelTextureRoots(const std::vector<std::string>& roots) {
    namespace d = texture_path_detail;
    std::vector<d::Root> list;
    const std::filesystem::path home = d::HomeFolder();
    for (const std::string& r : roots) {
        if (r.empty() || r.find('\0') != std::string::npos) continue;
        const std::filesystem::path given = std::filesystem::u8path(r);
        if (!given.is_absolute()) continue;
        std::error_code ec;
        if (!std::filesystem::is_directory(given, ec) || ec) continue;
        d::Root root;
        root.lexical = given.lexically_normal();
        if (!d::Canonical(given, root.canonical)) continue;
        if (d::Depth(root.lexical) < 2 || d::Depth(root.canonical) < 2) continue;   // "/", "C:/", "/home"
        if (!home.empty() && d::Inside(home, root.canonical)) continue;            // the home folder or above it
        list.push_back(std::move(root));
    }
    const std::size_t kept = list.size();
    std::lock_guard<std::mutex> lock(d::Mutex());
    d::Roots() = std::move(list);
    return kept;
}

/// The allowed roots (canonical, UTF-8).
inline std::vector<std::string> ModelTextureRoots() {
    std::lock_guard<std::mutex> lock(texture_path_detail::Mutex());
    std::vector<std::string> out;
    for (const auto& p : texture_path_detail::Roots()) out.push_back(p.canonical.u8string());
    return out;
}

/// The path to load for a file a model names (`written`, as the file wrote it), or "" when it must not be loaded.
/// `modelDir` is the model file's folder with its trailing slash, or "" for the current folder.
inline std::string ResolveModelTexturePath(std::string written, const std::string& modelDir) {
    namespace d = texture_path_detail;
    namespace fs = std::filesystem;
    for (char& c : written) if (c == '\\') c = '/';
    const d::Kind kind = d::Screen(written, nullptr);
    if (kind == d::Kind::Refused) return {};
    std::vector<d::Root> roots;
    {
        std::lock_guard<std::mutex> lock(d::Mutex());
        roots = d::Roots();
    }
    auto insideARoot = [&](const fs::path& real) {
        for (const auto& root : roots) if (d::Inside(real, root.canonical)) return true;
        return false;
    };
    std::vector<std::string> parts;
    if (kind == d::Kind::Relative && d::NormalizeRelative(written, parts)) {
        if (parts.empty()) return {};   // "." or "./": the folder itself is not a file
        std::string rel;
        for (const std::string& p : parts) { if (!rel.empty()) rel += '/'; rel += p; }
        const std::string joined = modelDir + rel;
        // Inside the model's folder on the text; links on the disk must not lead out of it (or out of every root).
        fs::path base, real;
        if (!d::Canonical(fs::u8path(modelDir.empty() ? std::string(".") : modelDir), base)) return {};
        if (!d::Canonical(fs::u8path(joined), real)) return {};
        if (d::Inside(real, base) || insideARoot(real)) return joined;
        return {};
    }
    // Absolute, or climbing out of the model's folder: only inside an allowed root, first on the text, then for real.
    if (roots.empty()) return {};
    std::error_code ec;
    fs::path lexical;
    if (kind == d::Kind::Absolute) lexical = fs::u8path(written).lexically_normal();
    else {
        const fs::path base = fs::absolute(fs::u8path(modelDir.empty() ? std::string(".") : modelDir), ec);
        if (ec) return {};
        lexical = (base / fs::u8path(written)).lexically_normal();
    }
    bool textInside = false;
    for (const auto& root : roots)
        if (d::Inside(lexical, root.lexical) || d::Inside(lexical, root.canonical)) { textInside = true; break; }
    if (!textInside) return {};   // refused before the disk is touched
    fs::path real;
    if (!d::Canonical(lexical, real)) return {};
    return insideARoot(real) ? real.u8string() : std::string();
}

} // namespace okay
