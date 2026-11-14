#pragma once
// Where a scene file may point. A downloaded scene (Irrlicht .irr, an NFF material table) names other files; those must lie
// inside the scene file's own directory tree, so a file from the internet cannot make the importer read model files from
// anywhere else on the machine. Internal: not installed.
#include "okay/Core/Utf8.hpp"
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace okay {
namespace detail {

// `baseDir` is the scene file's directory with its trailing slash (or empty: the current directory); `ref` is the path as the
// scene wrote it. On success `out` is baseDir + ref (backslashes made into slashes, a leading "./" dropped). Refused (false, with
// `why`): an empty path, an absolute path (a leading slash or backslash, a drive letter, "//host"), any ".." that climbs out of
// the directory, and a path that, once the links on the disk are followed, ends up outside baseDir (a symbolic link or junction).
inline bool ResolveInsideDir(const std::string& baseDir, std::string ref, std::string& out, std::string& why) {
    for (char& c : ref) if (c == '\\') c = '/';
    while (ref.compare(0, 2, "./") == 0) ref.erase(0, 2);
    if (ref.empty()) { why = "an empty file name"; return false; }
    if (ref[0] == '/' || ref.find(':') != std::string::npos) { why = "an absolute path is not allowed (a scene may only name files inside its own folder)"; return false; }
    if (ref.find('\0') != std::string::npos) { why = "a file name with a NUL character"; return false; }
    // lexical check: the depth may never go below the scene's folder
    int depth = 0;
    std::size_t i = 0;
    while (i <= ref.size()) {
        std::size_t j = ref.find('/', i);
        if (j == std::string::npos) j = ref.size();
        const std::string part = ref.substr(i, j - i);
        if (part == "..") { if (--depth < 0) { why = "a path that leaves the scene's folder (..) is not allowed"; return false; } }
        else if (!part.empty() && part != ".") ++depth;
        i = j + 1;
    }
    namespace fs = std::filesystem;
    const std::string joined = baseDir + ref;
    std::error_code ec;
#ifdef _WIN32
    auto mk = [](const std::string& s) { return fs::path(utf8::ToWide(s)); };
#else
    auto mk = [](const std::string& s) { return fs::path(s); };
#endif
    const fs::path base = fs::weakly_canonical(fs::absolute(mk(baseDir.empty() ? std::string(".") : baseDir), ec), ec);
    if (ec) { why = "the scene's folder cannot be resolved"; return false; }
    const fs::path full = fs::weakly_canonical(fs::absolute(mk(joined), ec), ec);
    if (ec) { why = "the path cannot be resolved"; return false; }
    auto b = base.begin(), f = full.begin();
    for (; b != base.end(); ++b, ++f) {
        if (b->empty()) continue;   // a trailing separator
        if (f == full.end() || *f != *b) { why = "the path leads out of the scene's folder (through a link)"; return false; }
    }
    out = joined;
    return true;
}

} // namespace detail
} // namespace okay
