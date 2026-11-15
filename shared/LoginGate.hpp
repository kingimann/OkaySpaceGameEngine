// The launcher's sign-in gate: the pure decision "may this person use the launcher right now?".
// Header only, no window and no network, so tests/test_login_gate.cpp can run it.
//
// What it is NOT: a lock on the engine. It is a client-side check in the launcher. Anyone can still run OkayEngine.exe or
// OkaySpacePlayer.exe directly, and a modified launcher can skip it. See docs/UNFINISHED.md ("Launcher sign-in gate").
#pragma once

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>

namespace okay::logingate {

// How long an EXPIRED access token that cannot be renewed (network down) keeps working, counted from the last time the server
// confirmed the session. A renewal the server REFUSES does not wait for this: it signs the person out at once.
constexpr long kOfflineGraceSeconds = 7L * 24 * 60 * 60;
// A last-check time this far in the future is a clock that moved (or a hand-edited file); it is not trusted.
constexpr long kClockSkewSeconds = 5L * 60;

enum class Verdict {
    Allowed,         // gate off, or signed in with a valid token (or the local backend)
    NeedSignIn,      // nobody is signed in (also: the server rejected the session)
    GraceExpired,    // signed in, token expired, and the last server confirmation is too old (or unknown / untrustworthy)
};

struct Inputs {
    bool required = true;       // the gate is on (see Required())
    bool signedIn = false;      // a saved session is loaded
    bool rejected = false;      // the server explicitly refused the session / refresh token
    bool onlineBackend = true;  // false: the local (offline) account backend, which has no server to ask
    long tokenExpiresAt = 0;    // unix seconds, 0 = unknown
    long lastOnlineCheck = 0;   // unix seconds of the last time the server confirmed the session, 0 = never
    long now = 0;               // unix seconds
};

inline Verdict Decide(const Inputs& in) {
    if (!in.required) return Verdict::Allowed;
    if (!in.signedIn || in.rejected) return Verdict::NeedSignIn;
    if (!in.onlineBackend) return Verdict::Allowed;
    if (in.tokenExpiresAt > in.now) return Verdict::Allowed;   // a token the server issued and that has not lapsed
    // Expired or unknown expiry: only the grace period keeps it working.
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
inline bool WriteLastCheck(const std::string& cfgDir, long when) {
    std::ofstream f(LastCheckFile(cfgDir), std::ios::trunc);
    f << (long long)when << "\n";
    return (bool)f;
}

}  // namespace okay::logingate
