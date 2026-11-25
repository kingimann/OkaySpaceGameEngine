#pragma once
// The C++ half of the web player's page glue (platform/web/okay_web.js), kept free of Emscripten so the Linux tests
// can check it (tests/test_web_player.cpp). player/main.cpp uses it only in the WebAssembly build.
//
//   SaveDirFromEnv   the save folder okay_web.js mounted on IndexedDB (OKAY_WEB_SAVE_DIR), or "" when it is missing
//                    or not one of ours (then the game keeps its usual save folder, which a reload forgets).
//   PendingWrite     "write this soon": Prefs changes are many and small (a slider moves every frame), so the player
//                    writes the prefs file at most every `delay` seconds after the last change instead of each time.
//                    The page then stores the closed file in IndexedDB.
#include <string>

namespace okay {
namespace web {

/// The folder okay_web.js mounts: /okaysave/<game id>/ (one IndexedDB database per game, so games on one site do not
/// share saves). Accepts only that shape, plain letters, digits and '_' in the id, and always ends in '/'.
inline std::string SaveDirFromEnv(const char* value) {
    if (!value) return std::string();
    std::string v(value);
    const std::string root = "/okaysave/";
    if (v.compare(0, root.size(), root) != 0) return std::string();
    std::string id = v.substr(root.size());
    while (!id.empty() && id.back() == '/') id.pop_back();
    if (id.empty() || id.size() > 60) return std::string();
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return std::string();
    }
    return root + id + "/";
}

/// A write that should happen `delay` seconds after the last change (and at once when asked with Flush).
class PendingWrite {
public:
    explicit PendingWrite(double delaySeconds = 0.5) : m_delay(delaySeconds) {}
    /// Something changed at time `now` (seconds, any clock that only goes forward).
    void Touch(double now) { m_pending = true; m_last = now; }
    bool Pending() const { return m_pending; }
    /// True when a change is waiting and nothing changed for `delay` seconds.
    bool Due(double now) const { return m_pending && now - m_last >= m_delay; }
    /// The write happened.
    void Done() { m_pending = false; }
private:
    double m_delay;
    double m_last = 0.0;
    bool m_pending = false;
};

} // namespace web
} // namespace okay
