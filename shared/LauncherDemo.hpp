#pragma once
// Is the launcher running a developer demo / screenshot mode (no network, no start-up screens)? One rule for every screen that must
// stay out of those modes (the start-up update screen, the sign-in gate): ANY environment variable named OKAY_UI_DEMO_* or OKAY_*_DEMO*
// (OKAY_CHAT_DEMO, OKAY_MP_DEMO_TAB, OKAY_VOICE_DEMO_SERVER, ...) is set. Tests: tests/test_startup_update.cpp.
// (The Windows branch reads the C runtime's environment table; it has not been compiled or run on Windows.)
#include <cstdlib>
#include <string>
#include <vector>

#if !defined(_WIN32)
extern "C" char** environ;
#endif

namespace okay {

// Is `name` the name of a demo / screenshot variable?
inline bool IsDemoEnvName(const std::string& name) {
    if (name.rfind("OKAY_UI_DEMO_", 0) == 0) return true;
    return name.rfind("OKAY_", 0) == 0 && name.find("_DEMO", 5) != std::string::npos;
}

// Names of every environment variable of this process.
inline std::vector<std::string> EnvironmentNames() {
    std::vector<std::string> out;
#if defined(_WIN32)
    char** env = _environ;
#else
    char** env = environ;
#endif
    for (; env && *env; ++env) {
        const std::string e = *env;
        const std::size_t eq = e.find('=', 1);   // (a leading '=' is a Windows drive entry)
        out.push_back(e.substr(0, eq));
    }
    return out;
}

inline bool IsLauncherDemoMode(const std::vector<std::string>& envNames) {
    for (const std::string& n : envNames) if (IsDemoEnvName(n)) return true;
    return false;
}
inline bool IsLauncherDemoMode() { return IsLauncherDemoMode(EnvironmentNames()); }

} // namespace okay
