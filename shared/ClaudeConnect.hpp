#pragma once
// Connect to Claude: what the editor's "Connect to Claude" panel (View > Automation Link) does to make okay mcp one
// click away. One home for the editor and the launcher (CLAUDE.md rule 19c). Header only, no ImGui.
//
//   - DesktopConfigPath(): Claude Desktop's config file, at the places Claude's public documentation for MCP servers
//     names: %APPDATA%\Claude\claude_desktop_config.json on Windows, ~/Library/Application Support/Claude/ on macOS.
//     Linux has no official Claude Desktop; ~/.config/Claude/ (XDG_CONFIG_HOME) is used there and is a guess.
//   - MergeDesktopConfig(): adds or updates ONLY mcpServers.okayspace = {"command": <okay.exe>, "args": ["mcp"]};
//     every other server and setting is kept as it was (okay::json keeps the order of keys). Broken JSON is refused with
//     the reason, never overwritten. AddToDesktopConfig() writes the file only after copying the old one to
//     claude_desktop_config.json.bak; a missing file (and its folder) is created. Nothing else is touched, and nothing is
//     written until the person clicks the button.
//   - ClaudeCodeCommand(): the line for Claude Code: claude mcp add okayspace -- "<okay.exe>" mcp.
// okay mcp with no project finds the open editor by itself (the link file, okay/Automation/Link.hpp), so one entry
// serves every project. Our own code, written from Claude Desktop's documented config shape (mcpServers: name ->
// command, args).
#include "okay/Core/Json.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace okay::claudeconnect {

inline constexpr const char* kServerName = "okayspace";
inline constexpr const char* kAfterSetup = "Restart Claude Desktop, then ask Claude to list the objects in your scene.";

namespace detail {
inline std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}
inline std::string Join(const std::string& a, const char* b) { return (std::filesystem::u8path(a) / std::filesystem::u8path(b)).u8string(); }
}  // namespace detail

/// Claude Desktop's config file for this user ("" when the folder that holds it cannot be found).
inline std::string DesktopConfigPath() {
#if defined(_WIN32)
    const std::string appData = detail::Env("APPDATA");
    return appData.empty() ? std::string() : detail::Join(detail::Join(appData, "Claude"), "claude_desktop_config.json");
#elif defined(__APPLE__)
    const std::string home = detail::Env("HOME");
    return home.empty() ? std::string() : detail::Join(home, "Library/Application Support/Claude/claude_desktop_config.json");
#else
    std::string base = detail::Env("XDG_CONFIG_HOME");
    if (base.empty()) { const std::string home = detail::Env("HOME"); if (home.empty()) return {}; base = detail::Join(home, ".config"); }
    return detail::Join(detail::Join(base, "Claude"), "claude_desktop_config.json");
#endif
}

/// The okay program for a program in `exeDir`: beside it, in its Tools folder, or in the Tools folder beside its folder
/// (the installed layout: the launcher at the top, the editor and okay in Tools). "" when none is there.
inline std::string FindOkayProgram(const std::string& exeDir) {
#if defined(_WIN32)
    const char* name = "okay.exe";
#else
    const char* name = "okay";
#endif
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::u8path(exeDir);
    for (const fs::path& p : {dir / name, dir / "Tools" / name, dir.parent_path() / "Tools" / name})
        if (fs::is_regular_file(p, ec)) return fs::absolute(p, ec).u8string();
    return {};
}

/// A path as one command-line word: in double quotes when it has a space or a shell character in it (a Windows path
/// cannot hold a double quote; elsewhere one is written as \").
inline std::string QuoteArg(const std::string& s) {
    if (!s.empty() && s.find_first_of(" \t\"'&()") == std::string::npos) return s;
    std::string q = "\"";
    for (char c : s) { if (c == '"') q += '\\'; q += c; }
    return q + "\"";
}

/// The Claude Code line that adds this engine as an MCP server.
inline std::string ClaudeCodeCommand(const std::string& okayExe) {
    return std::string("claude mcp add ") + kServerName + " -- " + QuoteArg(okayExe) + " mcp";
}

/// Merge the okayspace entry into the config text `text` ("" = a new file). False with `error` when `text` is not a JSON
/// object (or mcpServers is not one): nothing must be written then.
inline bool MergeDesktopConfig(const std::string& text, const std::string& okayExe, std::string& out, std::string& error, bool* changed = nullptr) {
    json::Value root = json::Value::Obj();
    if (text.find_first_not_of(" \t\r\n") != std::string::npos) {
        std::string why;
        // A UTF-8 byte order mark some editors add is not JSON: step over it.
        const std::string body = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? text.substr(3) : text;
        if (!json::Parse(body, root, &why)) { error = "Claude Desktop's config file is not valid JSON (" + why + "). Fix or remove it, then try again; it was not changed."; return false; }
        if (!root.IsObject()) { error = "Claude Desktop's config file does not hold a JSON object; it was not changed."; return false; }
    }
    json::Value entry = json::Value::Obj();
    entry.Set("command", json::Value::Str(okayExe));
    entry.Set("args", json::Value::Arr().Push(json::Value::Str("mcp")));
    json::Value* servers = nullptr;
    for (auto& kv : root.object) if (kv.first == "mcpServers") { servers = &kv.second; break; }
    if (servers && !servers->IsObject()) { error = "mcpServers in Claude Desktop's config file is not a JSON object; it was not changed."; return false; }
    if (!servers) { root.Set("mcpServers", json::Value::Obj()); servers = &root.object.back().second; }
    bool found = false, same = false;
    for (auto& kv : servers->object) if (kv.first == kServerName) {
        same = json::Write(kv.second) == json::Write(entry);
        kv.second = entry;   // only our own entry changes
        found = true;
        break;
    }
    if (!found) servers->Set(kServerName, entry);
    if (changed) *changed = !same;
    out = json::Write(root, 2) + "\n";
    return true;
}

struct SetupResult {
    bool ok = false;
    bool created = false;   ///< there was no config file: it was made
    std::string message;    ///< what to tell the person
    std::string backup;     ///< the copy of the old file ("" when there was none)
};

/// Add or update the okayspace entry in the config file at `configPath` (DesktopConfigPath()). Writes the backup first.
inline SetupResult AddToDesktopConfig(const std::string& configPath, const std::string& okayExe) {
    namespace fs = std::filesystem;
    SetupResult r;
    if (configPath.empty()) { r.message = "Claude Desktop's settings folder was not found on this computer."; return r; }
    if (okayExe.empty()) { r.message = "The okay program (okay.exe) was not found beside the editor or in its Tools folder. Reinstall or update to get it."; return r; }
    const fs::path path = fs::u8path(configPath);
    std::error_code ec;
    std::string text;
    const bool exists = fs::exists(path, ec);
    if (exists) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { r.message = "Claude Desktop's config file could not be read: " + configPath; return r; }
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    std::string merged, error;
    if (!MergeDesktopConfig(text, okayExe, merged, error)) { r.message = error; return r; }
    if (exists) {
        r.backup = configPath + ".bak";
        std::ofstream b(fs::u8path(r.backup), std::ios::binary | std::ios::trunc);
        if (!(b << text) || !b.flush()) { r.message = "The backup copy could not be written (" + r.backup + "), so the config file was not changed."; return r; }
    } else {
        fs::create_directories(path.parent_path(), ec);
        r.created = true;
    }
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!(f << merged) || !f.flush()) { r.message = "Claude Desktop's config file could not be written: " + configPath; return r; }
    }
    r.ok = true;
    r.message = std::string(r.created ? "Made " : "Updated ") + configPath + " (the \"" + kServerName + "\" server" +
                (r.backup.empty() ? std::string()  : "; the old file is kept as " + fs::u8path(r.backup).filename().u8string()) + "). " + kAfterSetup;
    return r;
}

}  // namespace okay::claudeconnect
