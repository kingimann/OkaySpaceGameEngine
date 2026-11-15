#pragma once
// Where a scene file may point. A downloaded scene (Irrlicht .irr, an NFF material table) names other files; those must lie
// inside the scene file's own directory tree, so a file from the internet cannot make the importer read model files from
// anywhere else on the machine. Internal: not installed.
#include "okay/Core/Utf8.hpp"
#include "okay/Render/TexturePath.hpp"
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
    namespace tp = texture_path_detail;
    for (char& c : ref) if (c == '\\') c = '/';
    while (ref.compare(0, 2, "./") == 0) ref.erase(0, 2);
    // The text-only screen shared with model texture paths (okay/Render/TexturePath.hpp): empty, NUL, wildcard and
    // device characters, network and device paths ("//host", "\\?\") are refused before the disk is touched.
    const tp::Kind kind = tp::Screen(ref, &why);
    if (kind == tp::Kind::Refused) return false;
    if (kind == tp::Kind::Absolute || ref.find(':') != std::string::npos) { why = "an absolute path is not allowed (a scene may only name files inside its own folder)"; return false; }
    // lexical check: the depth may never go below the scene's folder
    std::vector<std::string> parts;
    if (!tp::NormalizeRelative(ref, parts)) { why = "a path that leaves the scene's folder (..) is not allowed"; return false; }
    if (parts.empty()) { why = "an empty file name"; return false; }
    namespace fs = std::filesystem;
    const std::string joined = baseDir + ref;
    fs::path base, full;
    if (!tp::Canonical(fs::u8path(baseDir.empty() ? std::string(".") : baseDir), base)) { why = "the scene's folder cannot be resolved"; return false; }
    if (!tp::Canonical(fs::u8path(joined), full)) { why = "the path cannot be resolved"; return false; }
    // Component by component (case-insensitive on Windows), after the links on the disk are followed.
    if (!tp::Inside(full, base)) { why = "the path leads out of the scene's folder (through a link)"; return false; }
    out = joined;
    return true;
}

} // namespace detail
} // namespace okay
