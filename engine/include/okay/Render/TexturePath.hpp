#pragma once
// Where a texture named inside a model file may come from (OBJ/MTL `map_Kd` and friends, and Mesh::LoadOBJ).
//
// A downloaded model must not be able to point the engine at any file on the machine (#753). So:
//   * a relative path that stays inside the model's own folder is kept (its "." and ".." are resolved), as before;
//   * an absolute path, or a relative one whose ".." climbs out of the model's folder, is kept only when the file it names
//     lies inside one of the allowed roots, after links on the disk are followed (so a symbolic link or junction inside a
//     root that points outside it is refused). Otherwise the texture is dropped (the material keeps its colour).
// The allowed roots are empty by default, which is exactly the behaviour since #753 (nothing outside the model's folder).
// The editor sets them to the open project's folder (EditorState::setProjectDir), so a model inside a project may use
// textures from anywhere in that project, written as an absolute path or with "..", as exporters often do.
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace okay {

namespace texture_path_detail {
inline std::mutex& Mutex() { static std::mutex m; return m; }
inline std::vector<std::filesystem::path>& Roots() { static std::vector<std::filesystem::path> r; return r; }

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
} // namespace texture_path_detail

/// Set the folders whose files a model may name by absolute path or by "..". Empty folders are ignored; a folder that does
/// not exist is kept as written. Replaces the previous list. Thread-safe.
inline void SetModelTextureRoots(const std::vector<std::string>& roots) {
    std::vector<std::filesystem::path> list;
    for (const std::string& r : roots) {
        if (r.empty()) continue;
        std::filesystem::path c;
        if (texture_path_detail::Canonical(std::filesystem::u8path(r), c)) list.push_back(c);
    }
    std::lock_guard<std::mutex> lock(texture_path_detail::Mutex());
    texture_path_detail::Roots() = std::move(list);
}

/// The allowed roots (canonical, UTF-8).
inline std::vector<std::string> ModelTextureRoots() {
    std::lock_guard<std::mutex> lock(texture_path_detail::Mutex());
    std::vector<std::string> out;
    for (const auto& p : texture_path_detail::Roots()) out.push_back(p.u8string());
    return out;
}

/// The path to load for a texture a model file names (`written`, as the file wrote it), or "" when it must not be loaded.
/// `modelDir` is the model file's folder with its trailing slash, or "" for the current folder.
inline std::string ResolveModelTexturePath(std::string written, const std::string& modelDir) {
    for (char& c : written) if (c == '\\') c = '/';
    if (written.empty()) return {};
    const bool absolute = written[0] == '/' || (written.size() > 1 && written[1] == ':');
    if (!absolute) {
        // Inside the model's folder: resolve "." and ".." lexically; one ".." too many means it climbs out.
        std::vector<std::string> parts;
        bool climbsOut = false;
        std::size_t i = 0;
        while (i < written.size()) {
            std::size_t a = i;
            while (a < written.size() && written[a] != '/') ++a;
            const std::string comp = written.substr(i, a - i);
            i = a + 1;
            if (comp.empty() || comp == ".") continue;
            if (comp == "..") {
                if (parts.empty()) { climbsOut = true; break; }
                parts.pop_back();
                continue;
            }
            parts.push_back(comp);
        }
        if (!climbsOut) {
            std::string out;
            for (const std::string& p : parts) { if (!out.empty()) out += '/'; out += p; }
            return modelDir + out;
        }
    }
    // Absolute, or climbing out of the model's folder: only inside an allowed root, links followed.
    std::vector<std::filesystem::path> roots;
    {
        std::lock_guard<std::mutex> lock(texture_path_detail::Mutex());
        roots = texture_path_detail::Roots();
    }
    if (roots.empty()) return {};
    const std::filesystem::path named = absolute ? std::filesystem::u8path(written) : std::filesystem::u8path(modelDir + written);
    std::filesystem::path real;
    if (!texture_path_detail::Canonical(named, real)) return {};
    for (const auto& root : roots)
        if (texture_path_detail::Inside(real, root)) return real.u8string();
    return {};
}

} // namespace okay
