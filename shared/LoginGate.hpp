// The launcher's sign-in gate: the pure decision "may this person use the launcher right now?".
// Header only, no window and no network, so tests/test_login_gate.cpp can run it.
//
// What it is NOT: a lock on the engine. It is a client-side check in the launcher. Anyone can still run OkayEngine.exe or
// OkaySpacePlayer.exe directly, and a modified launcher can skip it. See docs/UNFINISHED.md ("Launcher sign-in gate").
#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace okay::logingate {

// How long an EXPIRED access token that cannot be renewed (network down) keeps working, counted from the last time the server
// confirmed the session (a sign-in, a renewal or a /verify, or an unexpired token seen). A renewal the auth server clearly REFUSES does
// not wait for this: the refresh worker signs the person out as soon as it sees it (after the access token expires, up to 30 s later).
constexpr long kOfflineGraceSeconds = 7L * 24 * 60 * 60;
// A last-check time this far in the future is a clock that moved (or a hand-edited file); it is not trusted.
constexpr long kClockSkewSeconds = 5L * 60;

enum class Verdict {
    Allowed,         // gate off, or signed in with a valid token (or the local backend)
    NeedSignIn,      // nobody is signed in (after a clear refusal the launcher has already signed the person out, so it lands here too)
    GraceExpired,    // signed in, token expired, and the last server confirmation is too old (or unknown / untrustworthy)
};

struct Inputs {
    bool required = true;       // the gate is on (see Required())
    bool signedIn = false;      // a saved session is loaded
    bool onlineBackend = true;  // false: the local (offline) account backend, which has no server to ask
    long tokenExpiresAt = 0;    // unix seconds, 0 = unknown
    long lastOnlineCheck = 0;   // unix seconds of the last time the server confirmed the session, 0 = never
    long now = 0;               // unix seconds
};

inline Verdict Decide(const Inputs& in) {
    if (!in.required) return Verdict::Allowed;
    if (!in.signedIn) return Verdict::NeedSignIn;
    if (!in.onlineBackend) return Verdict::Allowed;
    if (in.tokenExpiresAt > in.now) return Verdict::Allowed;   // a token the server issued and that has not lapsed
    // Expired or unknown expiry (a custom server's opaque token): only the grace period keeps it working, counted from the last time
    // the server confirmed the session (a sign-in, a renewal or a /verify), which the launcher re-checks every 30 minutes in that case.
    if (in.lastOnlineCheck <= 0) return Verdict::GraceExpired;                       // never confirmed: no grace
    if (in.lastOnlineCheck > in.now + kClockSkewSeconds) return Verdict::GraceExpired;   // from the future: not trusted
    const long age = in.now > in.lastOnlineCheck ? in.now - in.lastOnlineCheck : 0;   // small backwards skew counts as "just now"
    return age <= kOfflineGraceSeconds ? Verdict::Allowed : Verdict::GraceExpired;
}

// Is the gate on? `envValue` is OKAY_REQUIRE_LOGIN (nullptr when unset), `setting` the saved launcher setting (default true) and
// `demoMode` true when a UI-demo / screenshot hook is active (those run without an account). "0", "false", "off" and "no" turn it
// off; "1", "true", "on" and "yes" turn it on even in a demo mode; anything else is ignored.
inline bool Required(const char* envValue, bool setting, bool demoMode) {
    if (envValue && *envValue) {
        auto is = [&](const char* w) {
            const char* a = envValue; const char* b = w;
            for (; *a && *b; ++a, ++b) if ((*a | 0x20) != *b) return false;
            return !*a && !*b;
        };
        if (is("0") || is("false") || is("off") || is("no")) return false;
        if (is("1") || is("true") || is("on") || is("yes")) return true;
    }
    if (demoMode) return false;
    return setting;
}

// The rule for what to store as the last-confirmed time. `serverConfirmedAt` is the last time THIS process saw the server accept the session
// (sign-in, renewal, /verify), 0 = never. A confirmation counts whatever the clock says, so a computer whose clock is hours fast (every
// token looks expired) still keeps its grace. An unexpired token is also taken as "the server issued it within the hour" (refreshed at
// most every 10 minutes, and when the stored time is in the future, e.g. after the clock was corrected). Returns the new value
// (== `lastCheck` when nothing should be written).
inline long NextLastCheck(long lastCheck, long now, long serverConfirmedAt, bool tokenUnexpired) {
    if (serverConfirmedAt > 0 && serverConfirmedAt > lastCheck) return serverConfirmedAt;
    if (tokenUnexpired && (lastCheck <= 0 || now - lastCheck > 600 || lastCheck > now)) return now;
    return lastCheck;
}

// ---- The last-confirmed time, kept in a small file beside the saved session ----
inline std::string LastCheckFile(const std::string& cfgDir) {
    return cfgDir + (cfgDir.empty() || cfgDir.back() == '/' || cfgDir.back() == '\\' ? "" : "/") + "login_lastcheck.txt";
}
inline long ReadLastCheck(const std::string& cfgDir) {
    std::ifstream f(LastCheckFile(cfgDir));
    long long v = 0;
    if (!(f >> v) || v < 0) return 0;
    return (long)v;
}
// First run after an upgrade (no file yet): the saved session file's modification time is the best stand-in for the last sign-in. 0 = unknown.
inline long SeedLastCheckFromSession(const std::string& sessionFile) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(sessionFile, ec);
    if (ec) return 0;
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return (long)std::chrono::system_clock::to_time_t(sys);
}
// Written to a temp file and renamed over the real one, so a reader on another thread (worker and main thread both write) never sees a
// half-written file.
inline bool WriteLastCheck(const std::string& cfgDir, long when) {
    namespace fs = std::filesystem;
    static std::atomic<unsigned> counter{0};
    const std::string target = LastCheckFile(cfgDir);
    const std::string tmp = target + ".tmp" + std::to_string(counter.fetch_add(1)) + "_" +
                            std::to_string((unsigned long long)std::hash<std::thread::id>()(std::this_thread::get_id()));
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << (long long)when << "\n";
        f.flush();
        if (!f) { std::error_code e; fs::remove(tmp, e); return false; }
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) { fs::remove(target, ec); ec.clear(); fs::rename(tmp, target, ec); }   // a platform whose rename will not replace
    if (ec) { std::error_code e2; fs::remove(tmp, e2); return false; }
    return true;
}

}  // namespace okay::logingate
