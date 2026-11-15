#pragma once
// Is the launcher running a developer demo / screenshot mode (no network, no start-up screens)? One list for every screen that must
// stay out of those modes (the start-up update screen, the sign-in gate). Tests: tests/test_startup_update.cpp.
#include <cstdlib>
#include <functional>

namespace okay {

inline constexpr const char* kLauncherDemoEnv[] = {
    "OKAY_UI_DEMO_USER", "OKAY_UI_DEMO_TAB", "OKAY_UI_DEMO_UPDATE", "OKAY_CHAT_DEMO", "OKAY_MP_DEMO", "OKAY_VOICE_DEMO", "OKAY_VOICE_DEMO_SERVER",
};

// `isSet(name)` says whether an environment variable is set (injectable for tests).
inline bool IsLauncherDemoMode(const std::function<bool(const char*)>& isSet) {
    for (const char* n : kLauncherDemoEnv) if (isSet(n)) return true;
    return false;
}
inline bool IsLauncherDemoMode() {
    return IsLauncherDemoMode([](const char* n) { return std::getenv(n) != nullptr; });
}

} // namespace okay
