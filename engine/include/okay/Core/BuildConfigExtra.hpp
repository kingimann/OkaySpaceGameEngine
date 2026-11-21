#pragma once
// The extra game settings the Build Settings window writes into the built game's game.okayconfig, and the
// reader the player uses for them (docs/editor/build_settings_upgrade.md). Header-only, no engine state, so the editor,
// the player and the tests share exactly one definition. Written from our own design; standard library only.
//
// A setting at its default writes NO line, so a project that never touched them builds a game.okayconfig that is
// byte for byte what it was before these settings existed, and an old game.okayconfig reads as the defaults.
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay::buildpipe {

struct ExtraConfig {
    float timeScale = 1.0f;          // game speed (1 = normal, 0.5 = half speed); the player sets Time::SetTimeScale
    int   maxFixedSteps = 0;         // most fixed (physics) steps in one frame; 0 = the engine's own limit
    float musicVolume = 1.0f;        // starting music volume 0..1 (the player's own settings menu can change it later)
    float sfxVolume = 1.0f;          // starting sound-effects volume 0..1
    std::string defaultLanguage;     // language code the game starts in the first time ("" = the computer's language)
    std::string logLevel;            // "", "trace", "info", "warn", "error"
    std::string logFile;             // "" = no log file, else a file name inside the game's save folder
    std::string description;         // one line about the game (logged at start)
    std::vector<std::pair<std::string, std::string>> env;   // environment variables set before the game starts
    // ---- round 2 ----
    int   renderer = 0;              // 3D drawing: 0 automatic, 1 Direct3D 11 only, 2 OpenGL only, 3 software only (each falls back to software)
    int   particleCap = 0;           // most particles any one particle system keeps alive; 0 = no extra limit
    float gamepadDeadzone = 0.18f;   // the part of a stick's throw that reads 0 (0..0.95); 0.18 is the engine's own default
    int   sampleRate = 0;            // sound output rate in Hz (22050, 44100, 48000); 0 = 44100
};

inline bool IsValidEnvName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    if (!(std::isalpha((unsigned char)n[0]) || n[0] == '_')) return false;
    for (char c : n) if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}

/// Variables a game may not set: they change which programs or libraries the system loads.
inline bool IsBlockedEnvName(const std::string& n) {
    std::string u;
    for (char c : n) u.push_back((char)std::toupper((unsigned char)c));
    return u == "PATH" || u == "PATHEXT" || u == "COMSPEC" || u == "LD_PRELOAD" || u == "LD_LIBRARY_PATH" ||
           u.rfind("DYLD_", 0) == 0 || u == "SYSTEMROOT" || u == "WINDIR";
}

/// Read "KEY=VALUE;KEY2=VALUE2" (what the window stores). Bad or blocked names are skipped; `bad` (optional)
/// receives the names that were skipped.
inline std::vector<std::pair<std::string, std::string>> ParseEnvList(const std::string& text, std::vector<std::string>* bad = nullptr) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find(';', pos);
        if (end == std::string::npos) end = text.size();
        std::string item = text.substr(pos, end - pos);
        pos = end + 1;
        std::size_t a = item.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        item = item.substr(a);
        const std::size_t eq = item.find('=');
        const std::string name = item.substr(0, eq == std::string::npos ? item.size() : eq);
        std::string nameTrim = name;
        while (!nameTrim.empty() && (nameTrim.back() == ' ' || nameTrim.back() == '\t')) nameTrim.pop_back();
        if (eq == std::string::npos || !IsValidEnvName(nameTrim) || IsBlockedEnvName(nameTrim)) { if (bad) bad->push_back(nameTrim); continue; }
        out.emplace_back(nameTrim, item.substr(eq + 1));
    }
    return out;
}

/// "warn" / "warning" / "info" / "trace" / "error" in any case -> the word the engine's Log::Configure reads; "" if not a level.
inline std::string NormalizeLogLevel(const std::string& s) {
    std::string l;
    for (char c : s) l.push_back((char)std::tolower((unsigned char)c));
    if (l == "trace" || l == "info" || l == "error") return l;
    if (l == "warn" || l == "warning") return "warn";
    return "";
}

/// A log file name inside the save folder: no folders, no "..", at most 64 characters, else "".
inline std::string CleanLogFileName(const std::string& s) {
    if (s.empty() || s.size() > 64 || s == "." || s == "..") return "";
    for (char c : s) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) return "";
    return s;
}

/// One key=value line of game.okayconfig. Returns true if the key is one of ours.
inline bool ApplyExtraKey(ExtraConfig& c, const std::string& k, const std::string& v) {
    auto F = [&](float lo, float hi) { float x = (float)std::atof(v.c_str()); if (!(x >= lo)) x = lo; if (x > hi) x = hi; return x; };
    if      (k == "time_scale")        c.timeScale = F(0.0f, 100.0f);
    else if (k == "max_fixed_steps")   c.maxFixedSteps = std::atoi(v.c_str()) < 0 ? 0 : std::atoi(v.c_str());
    else if (k == "music_volume")      c.musicVolume = F(0.0f, 1.0f);
    else if (k == "sfx_volume")        c.sfxVolume = F(0.0f, 1.0f);
    else if (k == "default_language")  c.defaultLanguage = v.size() <= 16 ? v : std::string();
    else if (k == "log_level")         c.logLevel = NormalizeLogLevel(v);
    else if (k == "log_file")          c.logFile = CleanLogFileName(v);
    else if (k == "description")       c.description = v;
    else if (k == "renderer")          { const int n = std::atoi(v.c_str()); c.renderer = n < 0 || n > 3 ? 0 : n; }
    else if (k == "particle_cap")      { const int n = std::atoi(v.c_str()); c.particleCap = n < 0 ? 0 : (n > 100000 ? 100000 : n); }
    else if (k == "gamepad_deadzone")  c.gamepadDeadzone = F(0.0f, 0.95f);
    else if (k == "sample_rate")       { const int n = std::atoi(v.c_str()); c.sampleRate = (n == 22050 || n == 44100 || n == 48000) ? n : 0; }
    else if (k == "env") {
        const auto items = ParseEnvList(v);
        for (const auto& kv : items) c.env.push_back(kv);
    }
    else return false;
    return true;
}

/// The game.okayconfig lines for these settings: only the ones that differ from the defaults.
inline std::string WriteExtraConfig(const ExtraConfig& c) {
    std::ostringstream o;
    const ExtraConfig d;
    if (c.timeScale != d.timeScale)         o << "time_scale=" << c.timeScale << "\n";
    if (c.maxFixedSteps != d.maxFixedSteps) o << "max_fixed_steps=" << c.maxFixedSteps << "\n";
    if (c.musicVolume != d.musicVolume)     o << "music_volume=" << c.musicVolume << "\n";
    if (c.sfxVolume != d.sfxVolume)         o << "sfx_volume=" << c.sfxVolume << "\n";
    if (!c.defaultLanguage.empty())         o << "default_language=" << c.defaultLanguage << "\n";
    if (!c.logLevel.empty())                o << "log_level=" << c.logLevel << "\n";
    if (!c.logFile.empty())                 o << "log_file=" << c.logFile << "\n";
    if (!c.description.empty())             o << "description=" << c.description << "\n";
    if (c.renderer != d.renderer)           o << "renderer=" << c.renderer << "\n";
    if (c.particleCap != d.particleCap)     o << "particle_cap=" << c.particleCap << "\n";
    if (c.gamepadDeadzone != d.gamepadDeadzone) o << "gamepad_deadzone=" << c.gamepadDeadzone << "\n";
    if (c.sampleRate != d.sampleRate)       o << "sample_rate=" << c.sampleRate << "\n";
    for (const auto& kv : c.env)            o << "env=" << kv.first << "=" << kv.second << "\n";
    return o.str();
}

}  // namespace okay::buildpipe
