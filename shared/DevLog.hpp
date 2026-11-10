#pragma once
// Development-build logging for the player (Build Settings > Development build).
//
// A release build writes no log file. A development build (game.okayconfig has
// development=1, or OKAY_DEV is set) writes <game>.log next to the game exe, one
// timestamped line per event:
//   startup (title, version, scene), every scene load and its result, script and
//   engine warnings/errors (everything the engine logs at Warning and above, plus
//   script print/log_info), assets that failed to load, and frame-time hitches.
// If the folder is not writable the log goes to the per-user save folder instead.
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace okay { namespace devlog {

/// "<dir of exe>/<exe name without extension>.log". `exePath` is argv[0] (or any
/// path to the game exe); `baseDir` is used when it has no folder part.
inline std::string LogPathFor(const std::string& exePath, const std::string& baseDir) {
    std::size_t slash = exePath.find_last_of("/\\");
    std::string dir  = slash == std::string::npos ? baseDir : exePath.substr(0, slash + 1);
    std::string name = slash == std::string::npos ? exePath : exePath.substr(slash + 1);
    std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name.erase(dot);
    if (name.empty()) name = "game";
    return dir + name + ".log";
}

inline bool& Active() { static bool on = false; return on; }

/// Turn the dev log on. Does nothing (and returns false) unless `development`.
/// Tries `path`, then `fallbackPath`. Lines get HH:MM:SS.mmm timestamps.
inline bool Start(bool development, const std::string& path, const std::string& fallbackPath,
                  const std::string& title, const std::string& version, const std::string& scene) {
    if (!development) return false;
    Log::showTimestamps = true;
    if (Log::minLevel > Log::Level::Info) Log::minLevel = Log::Level::Info;
    if (!Log::OpenFile(path, 2u * 1024u * 1024u, 1) &&
        !(!fallbackPath.empty() && Log::OpenFile(fallbackPath, 2u * 1024u * 1024u, 1)))
        return false;
    Active() = true;
    Log::Info("[dev] ---- development build started: ", title, version.empty() ? "" : " ", version, " ----");
    Log::Info("[dev] first scene: ", scene.empty() ? std::string("(none)") : scene);
    return true;
}

inline void Stop() {
    if (!Active()) return;
    Log::Info("[dev] ---- shutting down ----");
    Log::CloseFile();
    Active() = false;
}

/// Frame-time hitch detector. Feed it each frame's seconds. A frame longer than
/// `minMs` AND `factor` times the running average is logged as a warning (at most
/// `maxLines` per run; the first `warmup` frames are ignored, a scene load is slow).
class HitchWatch {
public:
    double minMs = 100.0, factor = 3.0;
    int warmup = 60, maxLines = 200;
    /// True if this frame was logged as a hitch.
    bool Frame(double seconds) {
        const double ms = seconds * 1000.0;
        ++m_frames;
        bool hitch = false;
        if (m_frames > warmup && m_avg > 0.0 && ms > minMs && ms > m_avg * factor && m_lines < maxLines) {
            ++m_lines;
            Log::Warning("[perf] frame hitch: ", (int)(ms + 0.5), " ms (average ", (int)(m_avg + 0.5), " ms)");
            hitch = true;
        }
        m_avg = m_avg <= 0.0 ? ms : m_avg + (ms - m_avg) * 0.05;
        return hitch;
    }
    int Hitches() const { return m_lines; }
private:
    double m_avg = 0.0;
    int m_frames = 0, m_lines = 0;
};

}} // namespace okay::devlog
