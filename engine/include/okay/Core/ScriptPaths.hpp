#pragma once
// The gate every script-facing file API goes through (OkayScript builtins, visual-script nodes, action lists, plugins).
// Docs: docs/security.md "Script file paths". The rules live in PathSafety.hpp; this adds the symlink check against the
// two folders a script may touch (the game folder and the save folder) and the log line that tells the author why.
#include "okay/Core/Log.hpp"
#include "okay/Core/PathSafety.hpp"
#include "okay/Core/SaveData.hpp"
#include <chrono>
#include <string>
#include <system_error>
#include <unordered_map>

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

}  // namespace okay
