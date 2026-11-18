#pragma once
// Reading an exported game's settings (package/Data/game.okayconfig) for the Xbox app: pure text parsing, no engine link, tested on Linux.
// Written from the key=value format the engine's exporter writes (the same keys player/main.cpp reads: title, width, height, startup,
// scene, physics_hz, physics_smooth, volume, fps_cap, vsync, development) - only the ones a UWP run uses; everything else is ignored.
// The file may be obfuscated at build time (DataPack); the caller unpacks it first (XboxGameRuntime.hpp does).
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace okay::xbox {

struct GameConfig {
    std::string title = "OkaySpace Game";
    int width = 1280, height = 720;
    std::string startup;                 // the first scene, relative to Data/
    std::vector<std::string> scenes;     // every scene of the build (SceneManager list)
    float physicsHz = 50.0f; bool smoothPhysics = true, sawPhysics = false;
    float volume = 1.0f;
    int fpsCap = 0; bool vsync = true;
    bool development = false;
    std::vector<std::string> ignored;    // keys we saw and do not use on the console (reported in the log)
};

inline GameConfig ParseGameConfig(const std::string& text) {
    GameConfig c; std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto num = [&](int lo, int hi, int dflt) { const int n = std::atoi(v.c_str()); return n >= lo && n <= hi ? n : dflt; };
        if (k == "title") c.title = v;
        else if (k == "width") c.width = num(320, 7680, 1280);
        else if (k == "height") c.height = num(240, 4320, 720);
        else if (k == "startup") c.startup = v;
        else if (k == "scene") c.scenes.push_back(v);
        else if (k == "physics_hz") { const float h = (float)std::atof(v.c_str()); if (h >= 10.0f && h <= 1000.0f) { c.physicsHz = h; c.sawPhysics = true; } }
        else if (k == "physics_smooth") { c.smoothPhysics = std::atoi(v.c_str()) != 0; c.sawPhysics = true; }
        else if (k == "volume") { const float f = (float)std::atof(v.c_str()); c.volume = f < 0 ? 0 : f > 1 ? 1 : f; }
        else if (k == "fps_cap") c.fpsCap = num(0, 1000, 0);
        else if (k == "vsync") c.vsync = std::atoi(v.c_str()) != 0;
        else if (k == "development") c.development = std::atoi(v.c_str()) != 0;
        else c.ignored.push_back(k);
    }
    return c;
}

/// The scene to start: `startup` from the config, else game.okayscene. A name with '..', a drive or a leading '/' is refused (then the
/// default is tried). `exists(relativePath)` tells whether Data/<relative> is there. Empty = nothing to run.
inline std::string ResolveStartupScene(const GameConfig& c, const std::function<bool(const std::string&)>& exists) {
    auto safe = [](const std::string& s) { return !s.empty() && s[0] != '/' && s[0] != '\\' && s.find("..") == std::string::npos && s.find(':') == std::string::npos; };
    if (safe(c.startup) && exists(c.startup)) return c.startup;
    if (exists("game.okayscene")) return "game.okayscene";
    for (const std::string& s : c.scenes) if (safe(s) && exists(s)) return s;
    return std::string();
}

} // namespace okay::xbox
