#pragma once
// The gate every script-facing file API goes through (OkayScript builtins, visual-script nodes, action lists, plugins).
// Docs: docs/security.md "Script file paths". The rules live in PathSafety.hpp; this adds the symlink check against the
// two folders a script may touch (the game folder and the save folder) and the log line that tells the author why.
#include "okay/Core/Log.hpp"
#include "okay/Core/PathSafety.hpp"
#include "okay/Core/SaveData.hpp"
#include <chrono>
#include <string>
#include <iterator>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace okay {

namespace detail {
/// A script-chosen name made safe to log (control characters would forge log lines).
inline std::string LogName(const std::string& n) {
    std::string o = n.substr(0, 120);
    for (char& c : o) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = '?';
    return o;
}
}  // namespace detail

/// May a script open `name` (read or write)? True for a relative name that stays inside the game folder and the save
/// folder (when one is set), also through links already on disk. A refusal is logged with the reason; `api` names the
/// script function so the author can find it.
inline bool ScriptPathOk(const std::string& name, const char* api) {
    std::string why;
    const pathsafety::Why w = pathsafety::Check(name);
    if (w == pathsafety::Why::Ok) {
        // The link check asks the file system, and scripts call file functions every frame (data_num in update()), so a
        // verdict is remembered for two seconds (per thread; a link created in that time is seen a moment later).
        struct Memo { bool ok; double at; std::string cwd; };
        static thread_local std::unordered_map<std::string, Memo> memo;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::error_code ec;
        const std::string cwd = std::filesystem::current_path(ec).string();
        const std::string key = name + '\n' + Save::BaseDir();
        auto it = memo.find(key);
        if (it != memo.end() && it->second.cwd == cwd && now - it->second.at < 2.0) {
            if (it->second.ok) return true;
        } else {
            const bool ok = pathsafety::StaysInside(std::filesystem::path(), name, nullptr, &why) &&
                            (Save::BaseDir().empty() || pathsafety::StaysInside(std::filesystem::path(Save::BaseDir()), name, nullptr, &why));
            if (memo.size() > 512) memo.clear();
            memo[key] = Memo{ok, now, cwd};
            if (ok) return true;
        }
        if (why.empty()) why = "resolves outside the game's folders (a link points out of it)";
    } else {
        why = pathsafety::Describe(w);
    }
    OKAY_ERROR("[script] ", api, ": refusing file '", detail::LogName(name), "': ", why,
               " (use a plain name or a relative path inside the game's folders)");
    return false;
}

/// Like ScriptPathOk, for READING a prefab or scene a script names (spawn, shoot, spawn_wave, action-list spawns, spawn nodes).
/// Relative names pass exactly as in ScriptPathOk. An ABSOLUTE path is also allowed when it is a plain POSIX or drive-letter path
/// (never UNC, "\\?\\" or "\\.\\") whose parts pass the same rules (no "..", device names, hidden streams, trailing dots) and that,
/// after links are resolved, lies inside the game folder, the save folder or the system temp folder: the editor's Play and the
/// tests hand prefabs over by such paths, and reading a prefab there was always allowed. Writes never use this.
inline bool ScriptAssetPathOk(const std::string& name, const char* api) {
    namespace fs = std::filesystem;
    if (pathsafety::Check(name) == pathsafety::Why::Ok) return ScriptPathOk(name, api);
    std::string why = "not allowed";
    bool ok = false;
    const bool unc = name.size() >= 2 && (name[0] == '/' || name[0] == '\\') && (name[1] == '/' || name[1] == '\\');
    const bool drive = name.size() >= 3 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':' && (name[2] == '/' || name[2] == '\\');
    const bool posixAbs = !name.empty() && name[0] == '/' && !unc;
    if (!unc && (posixAbs || (drive && fs::u8path(name).is_absolute()))) {
        const std::string tail = name.substr(posixAbs ? 1 : 3);
        if (const pathsafety::Why w = pathsafety::Check(tail); w != pathsafety::Why::Ok) {
            why = pathsafety::Describe(w);
        } else {
            std::error_code ec;
            const fs::path full = fs::weakly_canonical(fs::u8path(name), ec);
            std::vector<fs::path> roots{fs::current_path(ec), fs::temp_directory_path(ec)};
            if (!Save::BaseDir().empty()) roots.push_back(fs::path(Save::BaseDir()));
            why = "an absolute path outside the game, save and temp folders";
            for (const fs::path& r : roots) {
                const fs::path base = fs::weakly_canonical(r, ec);
                if (base.empty()) continue;
                auto b = base.begin(), f = full.begin();
                for (; b != base.end() && f != full.end() && *b == *f; ++b, ++f) {}
                if ((b == base.end() || (b->empty() && std::next(b) == base.end())) && f != full.end()) { ok = true; break; }
            }
        }
    } else if (unc) {
        why = "a network or device path";
    }
    if (ok) return true;
    OKAY_ERROR("[script] ", api, ": refusing file '", detail::LogName(name), "': ", why);
    return false;
}

}  // namespace okay
