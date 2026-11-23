#pragma once
// Path text helpers of the Project panel, header-only so the panel's menus and its file operations (ProjectBrowser.cpp) use
// the same code and the tests can run it without the engine. Everything works on text ('/' or '\' separators); only
// NearestFolder looks at the disk.
//
//   Fold            the text to compare: lower case on Windows (file names ignore case there), as written elsewhere
//   RootLength      how long the root at the start is: 1 for "/", 3 for a drive root "D:/" or "D:\\" (any case), else 0
//   StripSlash      no trailing '/', but a root keeps its slash: "/" and "D:/" stay ("D:" alone would mean the current
//                   folder on drive D on Windows, not its root). "D:" is left as written: no slash is added.
//   Clean           '/' separators, "." and ".." resolved, no trailing '/' (text only, the same on every system)
//   Normalize       Clean of the absolute path (a relative path starts at the current folder)
//   Parent          the folder a '/' path is in ("D:/a" -> "D:/", "/a" -> "/"); "" for a root or a bare name
//   SameOrInside    `child` is `parent` or somewhere below it
//   Outermost       a selection without the items that are inside another selected item ({A, A/x} -> {A}), duplicates dropped
//   NearestFolder   the folder to show when the one on screen is gone: it, or its nearest parent that exists
//
// Written for this panel (the earlier private copies of Fold / StripSlash / Norm / UnderOrEqual / Outermost in
// ProjectBrowser.cpp now call these). Outermost sorts once and scans (n log n); the version it replaces compared every item
// with every other, which froze the panel after Ctrl+A in a folder of thousands of files.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace okaypb {
namespace paths {

/// ASCII lower case (the bytes of a UTF-8 name above 127 are left alone).
inline std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// The text to compare two paths with: Windows file names ignore case; elsewhere they do not.
inline std::string Fold(const std::string& s) {
#ifdef _WIN32
    return Lower(s);
#else
    return s;
#endif
}

/// Length of the root at the start of `s`: 1 for "/", 3 for a drive root ("D:/", "d:\\"), 0 for anything else (a
/// relative path, or "D:" alone, which on Windows is the current folder of drive D). Text only, so it is the same on
/// every system; on Linux "D:/" is never seen after Normalize (it makes every path start with '/').
inline std::size_t RootLength(const std::string& s) {
    if (!s.empty() && s[0] == '/') return 1;
    const bool letter = s.size() >= 3 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'));
    return (letter && s[1] == ':' && (s[2] == '/' || s[2] == '\\')) ? 3 : 0;
}

/// Drops trailing '/' but never the slash of a root: "/a/" -> "/a", "/" and "D:/" stay, "D://" -> "D:/". Only '/' is
/// stripped ('\\' is a file-name character on Linux); "D:\\" stays as it is, being a root.
inline std::string StripSlash(std::string s) {
    const std::size_t keep = std::max<std::size_t>(RootLength(s), 1);
    while (s.size() > keep && s.back() == '/') s.pop_back();
    return s;
}

/// Lexically cleaned, '/' separated, no trailing slash (a root keeps its slash). Text only: does not look at the disk or
/// at the current folder. "" stays "".
inline std::string Clean(const std::string& in) {
    if (in.empty()) return in;
    std::string s = in;
    for (char& c : s) if (c == '\\') c = '/';
    return StripSlash(std::filesystem::u8path(s).lexically_normal().generic_u8string());
}

/// Absolute-ish, lexically cleaned, '/' separated, no trailing slash (a root keeps its slash). "" stays "".
inline std::string Normalize(const std::string& in) {
    if (in.empty()) return in;
    std::string s = in;
    for (char& c : s) if (c == '\\') c = '/';
    namespace fs = std::filesystem;
    fs::path p = fs::u8path(s);
    std::error_code ec;
    if (p.is_relative()) {
        fs::path a = fs::absolute(p, ec);
        if (!ec) p = a;
    }
    return Clean(p.generic_u8string());
}

/// The folder a '/' path is in: "/a/b" -> "/a", "/a" -> "/", "D:/a" -> "D:/" (the drive root, with its slash). "" for a
/// root ("/", "D:/") and for a name with no '/'.
inline std::string Parent(const std::string& p) {
    const std::size_t root = RootLength(p);
    if (p.size() <= root) return std::string();
    const auto sl = p.find_last_of('/');
    if (sl == std::string::npos) return std::string();
    return sl + 1 <= root ? p.substr(0, root) : p.substr(0, sl);
}

/// `child` is `parent` or inside it. Both are '/' paths (run them through Normalize first); case is ignored on Windows.
inline bool SameOrInside(const std::string& parent, const std::string& child) {
    const std::string a = Fold(StripSlash(parent)), b = Fold(StripSlash(child));
    if (a == b) return true;
    return b.size() > a.size() && b.compare(0, a.size(), a) == 0 && (a.empty() || a.back() == '/' || b[a.size()] == '/');
}

/// Keep only the outermost items of a selection: a folder and something inside it give just the folder, and the same path
/// twice (on Windows: in two spellings of the case) once. Every path comes back through Normalize, sorted.
inline std::vector<std::string> Outermost(const std::vector<std::string>& in) {
    struct Row { std::string key, text; };
    std::vector<Row> rows;
    rows.reserve(in.size());
    for (const std::string& p : in) {
        Row r;
        r.text = Normalize(p);
        r.key = Fold(r.text);
        // '/' sorts before every other character in the key, so everything below a folder follows it directly
        for (char& c : r.key) if (c == '/') c = '\x01';
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.key != b.key ? a.key < b.key : a.text < b.text; });
    std::vector<std::string> out;
    const std::string* top = nullptr;   // key of the last outermost item kept
    for (const Row& r : rows) {
        if (top) {
            const std::string& t = *top;
            if (r.key == t) continue;                                                    // the same path again
            if (r.key.size() > t.size() && r.key.compare(0, t.size(), t) == 0 && (t.back() == '\x01' || r.key[t.size()] == '\x01')) continue;   // inside the last one kept
        }
        out.push_back(r.text);
        top = &r.key;
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// The folder to show instead of `dir`: `dir` while it is a folder inside the Assets folder, else its nearest parent that is
/// (down to the Assets folder), and the Assets folder when `dir` is outside it. Looks at the disk (one check per level).
inline std::string NearestFolder(const std::string& assetsRoot, const std::string& dir) {
    namespace fs = std::filesystem;
    const std::string root = Normalize(assetsRoot);
    std::string p = Normalize(dir);
    if (p.empty() || !SameOrInside(root, p)) return root;
    std::error_code ec;
    while (!fs::is_directory(fs::u8path(p), ec)) {
        if (Fold(p) == Fold(root)) return root;
        std::string up = Parent(p);   // "D:/x" -> "D:/" (not "D:", the current folder of drive D)
        if (up.empty()) return root;
        p = std::move(up);
        if (!SameOrInside(root, p)) return root;
    }
    return p;
}

} // namespace paths
} // namespace okaypb
