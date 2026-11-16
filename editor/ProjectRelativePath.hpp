#pragma once
// What path a file dropped from the Project panel is stored under (ImGui-free; tested in tests/test_drop_path_relative.cpp).
//
// The Project panel drags the file's full path. Stored as is, a scene would name C:\Users\<you>\... and the built game
// would only work on that PC. ProjectRelativePath turns a file inside the project folder into a path relative to the
// project root ("Assets/tex/a.png", forward slashes), which the editor and the player both look up under their asset
// folder (okay::Paths::FindFile: the project folder in the editor, <Name>_Data/ in a built game).
//
// Written from scratch as plain string work (no file system calls, so a Windows path can be tested on Linux):
//  - "\" and "/" are both separators; repeated separators, "." segments and ".." segments are folded;
//  - a path with a drive letter ("C:") compares case-insensitively (Windows file names ignore case), a path that starts
//    with "/" compares exactly;
//  - a path outside the project, a relative path, an empty project folder or the project folder itself come back
//    unchanged.
#include <cctype>
#include <string>
#include <vector>

namespace oked {
namespace relpath_detail {

struct Split {
    std::string root;                 // "c:" (lower-case drive), "/" or "//" (network share); empty: relative
    std::vector<std::string> parts;   // the folders and the file, with "." and ".." folded away
    bool ignoreCase = false;          // drive-letter paths: Windows compares names without case
};

inline Split SplitPath(const std::string& in) {
    Split s;
    std::string p = in;
    for (char& c : p) if (c == '\\') c = '/';
    std::size_t i = 0;
    if (p.size() >= 2 && p[1] == ':' && std::isalpha(static_cast<unsigned char>(p[0]))) {
        s.root = std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(p[0])))) + ":";
        s.ignoreCase = true;
        i = 2;
    } else if (p.compare(0, 2, "//") == 0) {
        s.root = "//";
        s.ignoreCase = true;   // a Windows network share
        i = 2;
    } else if (!p.empty() && p[0] == '/') {
        s.root = "/";
        i = 1;
    }
    std::string seg;
    auto flush = [&] {
        if (seg.empty() || seg == ".") { seg.clear(); return; }
        if (seg == "..") {
            if (!s.parts.empty() && s.parts.back() != "..") s.parts.pop_back();
            else if (s.root.empty()) s.parts.push_back(seg);   // a relative path keeps a leading ".."
            // above the root of an absolute path there is nothing: drop it
        } else {
            s.parts.push_back(seg);
        }
        seg.clear();
    };
    for (; i < p.size(); ++i) {
        if (p[i] == '/') flush();
        else seg += p[i];
    }
    flush();
    return s;
}

inline bool SameName(const std::string& a, const std::string& b, bool ignoreCase) {
    if (a.size() != b.size()) return false;
    if (!ignoreCase) return a == b;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

} // namespace relpath_detail

/// `path` relative to `projectDir` ("Assets/a.png") when it lies inside the project folder; otherwise `path` unchanged.
inline std::string ProjectRelativePath(const std::string& projectDir, const std::string& path) {
    namespace d = relpath_detail;
    if (projectDir.empty() || path.empty()) return path;
    const d::Split proj = d::SplitPath(projectDir);
    const d::Split file = d::SplitPath(path);
    if (proj.root.empty() || file.root.empty() || proj.root != file.root) return path;   // relative, or another drive
    if (file.parts.size() <= proj.parts.size()) return path;                             // the folder itself, or above it
    const bool ignoreCase = file.ignoreCase || proj.ignoreCase;
    for (std::size_t i = 0; i < proj.parts.size(); ++i)
        if (!d::SameName(proj.parts[i], file.parts[i], ignoreCase)) return path;
    std::string out;
    for (std::size_t i = proj.parts.size(); i < file.parts.size(); ++i) {
        if (!out.empty()) out += '/';
        out += file.parts[i];
    }
    return out;
}

/// True for the kinds of file whose loaders find a project-relative path (okay::Paths::FindFile): pictures and fonts.
inline bool DropStoresProjectRelative(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* const kinds[] = {"png", "jpg", "jpeg", "bmp", "tga", "gif", "hdr", "psd", "ttf", "otf"};
    for (const char* k : kinds) if (ext == k) return true;
    return false;
}

/// The path a drop from the Project panel stores: project-relative for pictures and fonts inside the project,
/// otherwise the dragged path as it was.
inline std::string StoredDropPath(const std::string& projectDir, const std::string& path) {
    return DropStoresProjectRelative(path) ? ProjectRelativePath(projectDir, path) : path;
}

} // namespace oked
