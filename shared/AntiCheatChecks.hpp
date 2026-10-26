#pragma once
// Anti-cheat, runtime checks (opt-in; docs/anticheat.md): debugger detection, a
// loaded-module allowlist and a clock-drift heartbeat with challenge-response.
//
// The probes (IsDebuggerPresent, the module list, the two clocks) are Windows-
// only and compiled out elsewhere; what their answers mean is plain logic that
// builds and is tested everywhere (tests/test_anticheat_checks.cpp). Nothing
// here runs unless the game's anti-cheat mode is report or enforce, and only
// enforce ever ends the game.
//
// These are client-side checks: a determined cheater can patch them out. They
// catch the common off-the-shelf tools and give the backend (ac_reports) a
// signal; the server staying authoritative is what actually stops cheating.
#include <cmath>
#include <cstdio>
#include <random>

#include "AntiCheatIntegrity.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace okayac {

struct Finding {
    std::string check;    // "debugger", "module", "clock"
    std::string detail;
};

// ---- Debugger ----------------------------------------------------------------
// An empty probe = not checked on this platform.
struct DebuggerProbes {
    std::function<bool()> local;    // a debugger attached to this process (IsDebuggerPresent)
    std::function<bool()> remote;   // ...as another process sees it (CheckRemoteDebuggerPresent)
};

inline std::vector<Finding> CheckDebugger(const DebuggerProbes& p) {
    std::vector<Finding> out;
    if (p.local && p.local()) out.push_back({"debugger", "a debugger is attached to the game"});
    if (p.remote && p.remote()) out.push_back({"debugger", "a debugger process is attached"});
    return out;
}

inline DebuggerProbes SystemDebuggerProbes() {
    DebuggerProbes p;
#if defined(_WIN32)
    p.local = [] { return IsDebuggerPresent() != FALSE; };
    p.remote = [] {
        BOOL b = FALSE;
        return CheckRemoteDebuggerPresent(GetCurrentProcess(), &b) && b;
    };
#endif
    return p;
}

// ---- Loaded modules ------------------------------------------------------------
// Paths are compared lower-cased with '/' separators (Windows paths are
// case-insensitive). Allowed: anything under a trusted folder (the Windows
// folder: system DLLs, drivers' user-mode parts, WinSxS), the game's own code
// files as its verified manifest lists them, and a few known overlays by name.
struct ModuleAllowlist {
    std::vector<std::string> trustedDirs;   // normalised, ending in '/'
    std::set<std::string> trustedFiles;     // normalised full paths
    std::set<std::string> names;            // lower-case file names
};

inline std::string NormalizeModulePath(const std::string& p) {
    std::string s = p;
    if (s.rfind("\\\\?\\", 0) == 0) s = s.substr(4);
    for (char& c : s) {
        if (c == '\\') c = '/';
        else c = (char)std::tolower((unsigned char)c);
    }
    return s;
}
inline void AllowDir(ModuleAllowlist& a, const std::string& dir) {
    std::string d = NormalizeModulePath(dir);
    if (d.empty()) return;
    if (d.back() != '/') d += '/';
    a.trustedDirs.push_back(d);
}
inline void AllowFile(ModuleAllowlist& a, const std::string& path) { a.trustedFiles.insert(NormalizeModulePath(path)); }
inline void AllowName(ModuleAllowlist& a, const std::string& name) { a.names.insert(og::Lower(name)); }

inline bool ModuleAllowed(const ModuleAllowlist& a, const std::string& path) {
    const std::string n = NormalizeModulePath(path);
    if (n.empty()) return false;
    if (a.trustedFiles.count(n)) return true;
    for (const std::string& d : a.trustedDirs)
        if (n.size() > d.size() && n.compare(0, d.size(), d) == 0) return true;
    const std::size_t slash = n.find_last_of('/');
    return a.names.count(slash == std::string::npos ? n : n.substr(slash + 1)) != 0;
}

inline std::vector<Finding> CheckModules(const std::vector<std::string>& loaded, const ModuleAllowlist& a) {
    std::vector<Finding> out;
    for (const std::string& m : loaded)
        if (!ModuleAllowed(a, m)) out.push_back({"module", "unexpected module loaded: " + m});
    return out;
}

// Overlays players commonly run (Steam, Discord). Matched by name only, so this
// list is a convenience for report mode, not a security boundary.
inline const std::vector<std::string>& KnownOverlayModules() {
    static const std::vector<std::string> v = {"gameoverlayrenderer64.dll", "gameoverlayrenderer.dll", "discordhook64.dll",
                                               "discordhook.dll"};
    return v;
}

inline ModuleAllowlist DefaultModuleAllowlist(const std::string& gameRoot, const std::string& windowsDir, const Integrity* manifest) {
    ModuleAllowlist a;
    if (!windowsDir.empty()) AllowDir(a, windowsDir);
    std::string root = NormalizeModulePath(gameRoot);
    if (!root.empty() && root.back() != '/') root += '/';
    if (manifest) {
        for (const og::File& f : manifest->files)
            if (detail::IsCodeFile(f.path)) AllowFile(a, root + f.path);
    } else if (!root.empty()) {
        AllowDir(a, root);   // nothing better to go on (the integrity check reports the missing manifest)
    }
    for (const std::string& n : KnownOverlayModules()) AllowName(a, n);
    return a;
}

inline std::vector<std::string> LoadedModules() {
    std::vector<std::string> out;
#if defined(_WIN32)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return out;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            char buf[MAX_PATH * 4];
            const int n = WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, buf, (int)sizeof(buf), nullptr, nullptr);
            if (n > 1) out.emplace_back(buf, (std::size_t)(n - 1));
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
#endif
    return out;
}

// ---- Clock-drift heartbeat -------------------------------------------------------
// A verifier with a clock it trusts sends challenges; the client answers each
// with its own clock reading, tagged with a session key (HMAC-SHA256), so an
// answer can't be forged, altered or replayed. For each window of windowMs the
// verifier compares how far the client's clock moved with how far its own did:
// a speedhack moves the game's clock faster (or slower). A window more than
// `tolerance` (5%) off, after allowing for the round trips, is suspicious; only
// several such windows in a row (sustain) are flagged, so a one-off step of
// either clock is never mistaken for a speedhack.
//
// In the player the verifier's clock is a monotonic one NTP never steps
// (QueryUnbiasedInterruptTime, else GetTickCount64) and the client's is the
// performance counter the game runs on (RuntimeGuard below). A multiplayer
// server can be the verifier instead, carrying ClockChallenge / ClockResponse in
// its own messages; that wiring belongs to the network layer.
constexpr double kClockTolerance = 0.05;

struct ClockChallenge {
    std::uint64_t nonce = 0;
    std::uint32_t seq = 0;
};
struct ClockResponse {
    std::uint64_t nonce = 0;
    std::uint32_t seq = 0;
    std::uint64_t clientMs = 0;
    std::string tag;   // hex HMAC-SHA256
};

namespace detail {
inline std::array<std::uint8_t, 32> HmacSha256(const std::string& key, const std::string& msg) {
    using okay::account::detail::Sha256;
    std::array<std::uint8_t, 64> k{};
    if (key.size() > 64) {
        Sha256 h; h.Update(key);
        const auto d = h.Digest();
        std::copy(d.begin(), d.end(), k.begin());
    } else {
        std::copy(key.begin(), key.end(), k.begin());
    }
    std::array<std::uint8_t, 64> ipad{}, opad{};
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 in; in.Update(ipad.data(), ipad.size()); in.Update(msg);
    const auto inner = in.Digest();
    Sha256 out; out.Update(opad.data(), opad.size()); out.Update(inner.data(), inner.size());
    Wipe(k.data(), k.size());
    return out.Digest();
}
inline std::string ClockTag(const std::string& key, std::uint64_t nonce, std::uint32_t seq, std::uint64_t clientMs) {
    const auto d = HmacSha256(key, "okayac-clock|" + std::to_string(nonce) + "|" + std::to_string(seq) + "|" + std::to_string(clientMs));
    return ToHex(d.data(), d.size());
}
} // namespace detail

inline ClockResponse AnswerChallenge(const ClockChallenge& c, std::uint64_t clientMs, const std::string& key) {
    ClockResponse r;
    r.nonce = c.nonce; r.seq = c.seq; r.clientMs = clientMs;
    r.tag = detail::ClockTag(key, c.nonce, c.seq, clientMs);
    return r;
}

class ClockHeartbeat {
public:
    enum class Verdict { Pending, Ok, Drift, BadResponse };
    static constexpr std::uint64_t kDefaultWindowMs = 2000;   // one rate measurement per window
    static constexpr int kDefaultSustain = 3;                 // windows in a row before it's drift

    explicit ClockHeartbeat(std::string key, double tolerance = kClockTolerance, std::uint64_t windowMs = kDefaultWindowMs,
                            int sustain = kDefaultSustain)
        : key_(std::move(key)), tol_(tolerance), win_(windowMs), sustain_(sustain < 1 ? 1 : sustain) {
        std::random_device rd;
        rng_.seed(((std::uint64_t)rd() << 32) ^ rd());
    }

    ClockChallenge Issue(std::uint64_t nowMs) {
        // Unanswered challenges expire (30 s) and never pile up.
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [&](const Out& o) { return nowMs > o.issuedAt + 30000; }),
                       pending_.end());
        if (pending_.size() >= kMaxOutstanding) pending_.erase(pending_.begin());
        ClockChallenge c;
        c.nonce = rng_();
        c.seq = ++seq_;
        pending_.push_back({c, nowMs});
        return c;
    }

    // Each window of at least windowMs (verifier time) gives one rate: how far the
    // client's clock moved against the verifier's, less 1. A window is "off" when
    // that's beyond the tolerance plus what the round trips can explain. Only
    // `sustain` off windows in a row, all off the same way, are drift: a one-off
    // step of either clock (an NTP correction, a resume from sleep) lands in a
    // single window and is never flagged.
    Verdict Accept(const ClockResponse& r, std::uint64_t nowMs) {
        auto it = std::find_if(pending_.begin(), pending_.end(),
                               [&](const Out& o) { return o.c.nonce == r.nonce && o.c.seq == r.seq; });
        if (it == pending_.end() || nowMs < it->issuedAt) { ++bad_; return Verdict::BadResponse; }
        if (!okay::account::detail::SecureEquals(r.tag, detail::ClockTag(key_, r.nonce, r.seq, r.clientMs))) {
            ++bad_;   // forged / altered: the real answer may still come
            return Verdict::BadResponse;
        }
        Sample s;
        s.server = (double)(it->issuedAt + nowMs) / 2.0;   // the client read its clock somewhere in between
        s.halfRtt = (double)(nowMs - it->issuedAt) / 2.0;
        s.client = (double)r.clientMs;
        s.set = true;
        pending_.erase(it);
        if (!anchor_.set) { anchor_ = s; return Verdict::Pending; }
        const double dS = s.server - anchor_.server;
        if (dS < (double)win_) return run_ >= sustain_ ? Verdict::Drift : Verdict::Pending;
        last_ = (s.client - anchor_.client) / dS - 1.0;
        const double allowance = (s.halfRtt + anchor_.halfRtt) / dS;
        anchor_ = s;   // the next window starts here
        const int sign = std::fabs(last_) <= tol_ + allowance ? 0 : (last_ > 0 ? 1 : -1);
        if (sign != 0 && sign == runSign_) ++run_;
        else { run_ = sign != 0 ? 1 : 0; runSign_ = sign; }
        if (run_ >= sustain_) {
            if (run_ == sustain_) ++flags_;   // one flag per sustained episode
            return Verdict::Drift;
        }
        return Verdict::Ok;
    }

    double LastDrift() const { return last_; }       // the last window: client rate / verifier rate - 1 (0.10 = 10% fast)
    int Flags() const { return flags_; }
    int BadResponses() const { return bad_; }
    std::size_t Outstanding() const { return pending_.size(); }
    void Reset() { anchor_ = Sample{}; pending_.clear(); run_ = 0; runSign_ = 0; }

private:
    static constexpr std::size_t kMaxOutstanding = 16;
    struct Out { ClockChallenge c; std::uint64_t issuedAt; };
    struct Sample { double server = 0, client = 0, halfRtt = 0; bool set = false; };
    std::string key_;
    double tol_;
    std::uint64_t win_;
    int sustain_;
    std::mt19937_64 rng_;
    std::uint32_t seq_ = 0;
    std::vector<Out> pending_;
    Sample anchor_;
    double last_ = 0;
    int run_ = 0, runSign_ = 0;   // off windows in a row, and which way
    int flags_ = 0, bad_ = 0;
};

// ---- The guard the player runs ----------------------------------------------------
struct GuardProbes {
    DebuggerProbes debugger;
    std::function<std::vector<std::string>()> modules;   // empty = not checked
    std::function<std::uint64_t()> trustedMs;           // the verifier's clock (empty = no checks at all)
    std::function<std::uint64_t()> gameMs;              // the clock the game runs on (empty = no heartbeat)
    ModuleAllowlist allowlist;
};

// The real probes: Windows only (elsewhere every probe is empty and the guard idles).
inline GuardProbes SystemGuardProbes(const std::string& gameRoot, const Integrity* manifest) {
    GuardProbes p;
#if defined(_WIN32)
    p.debugger = SystemDebuggerProbes();
    p.modules = [] { return LoadedModules(); };
    // The reference: monotonic and never stepped by NTP or the user (unlike the
    // wall clock). QueryUnbiasedInterruptTime, then GetTickCount64, are looked up
    // at run time: headers built for an older _WIN32_WINNT hide both.
    p.trustedMs = [] {
        typedef BOOL(WINAPI * QuitFn)(PULONGLONG);
        typedef ULONGLONG(WINAPI * Tick64Fn)();
        static const HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        static const QuitFn quit = reinterpret_cast<QuitFn>(
            reinterpret_cast<void*>(GetProcAddress(k32, "QueryUnbiasedInterruptTime")));
        static const Tick64Fn tick64 = reinterpret_cast<Tick64Fn>(
            reinterpret_cast<void*>(GetProcAddress(k32, "GetTickCount64")));
        ULONGLONG t = 0;
        if (quit && quit(&t)) return (std::uint64_t)(t / 10000ull);   // 100 ns units
        if (tick64) return (std::uint64_t)tick64();
        return (std::uint64_t)GetTickCount();   // pre-Vista only (wraps after 49 days)
    };
    p.gameMs = [] {
        LARGE_INTEGER c, f;
        QueryPerformanceCounter(&c);
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? (std::uint64_t)((double)c.QuadPart * 1000.0 / (double)f.QuadPart) : 0ull;
    };
    wchar_t wdir[MAX_PATH] = L"";
    std::string windowsDir;
    const UINT n = GetWindowsDirectoryW(wdir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        char buf[MAX_PATH * 4];
        const int m = WideCharToMultiByte(CP_UTF8, 0, wdir, -1, buf, (int)sizeof(buf), nullptr, nullptr);
        if (m > 1) windowsDir.assign(buf, (std::size_t)(m - 1));
    }
    p.allowlist = DefaultModuleAllowlist(gameRoot, windowsDir, manifest);
#else
    (void)gameRoot; (void)manifest;
#endif
    return p;
}

// Call Tick() once a frame; it does real work only every second or so. Each new
// finding is logged once. Tick() returns true when the mode is enforce and
// something was found: the game should end.
class RuntimeGuard {
public:
    using LogFn = std::function<void(const std::string&)>;
    RuntimeGuard() = default;
    RuntimeGuard(Mode mode, GuardProbes probes, LogFn log = nullptr)
        : mode_(mode), p_(std::move(probes)), log_(std::move(log)) {}

    bool Tick() {
        if (mode_ == Mode::Off || !p_.trustedMs || quit_) return quit_;
        const std::uint64_t now = p_.trustedMs();
        if (!started_) { started_ = true; nextDbg_ = nextMod_ = nextClock_ = now; }
        if (now >= nextDbg_) {
            nextDbg_ = now + 5000;
            for (const Finding& f : CheckDebugger(p_.debugger)) Add(f);
        }
        if (p_.modules && now >= nextMod_) {
            nextMod_ = now + 10000;
            for (const Finding& f : CheckModules(p_.modules(), p_.allowlist)) Add(f);
        }
        if (p_.gameMs && now >= nextClock_) {
            nextClock_ = now + 1000;
            const ClockChallenge c = hb_.Issue(now);
            const ClockResponse r = AnswerChallenge(c, p_.gameMs(), hbKey());
            const auto v = hb_.Accept(r, p_.trustedMs());
            if (v == ClockHeartbeat::Verdict::Drift && !clockFlagged_) {
                clockFlagged_ = true;
                char b[96];
                std::snprintf(b, sizeof(b), "the game clock runs %.1f%% %s than the system clock",
                              std::fabs(hb_.LastDrift()) * 100.0, hb_.LastDrift() > 0 ? "faster" : "slower");
                Add({"clock", b}, false);
            } else if (v == ClockHeartbeat::Verdict::Ok) {
                clockFlagged_ = false;
            }
        }
        return quit_;
    }

    const std::vector<Finding>& Findings() const { return findings_; }
    Mode mode() const { return mode_; }

private:
    static std::string RandomKey() { return okay::account::detail::RandomHex(32); }
    const std::string& hbKey() const { return hbKeyStr_; }

    void Add(const Finding& f, bool dedupe = true) {
        const std::string id = f.check + "|" + f.detail;
        if (dedupe && !seen_.insert(id).second) return;
        findings_.push_back(f);
        if (log_) log_("[anti-cheat] " + f.check + ": " + f.detail);
        if (mode_ == Mode::Enforce) quit_ = true;
    }

    Mode mode_ = Mode::Off;
    GuardProbes p_;
    LogFn log_;
    std::string hbKeyStr_ = RandomKey();   // the in-process heartbeat's session key
    // A challenge a second, 5 s windows, 3 windows in a row (15 s) of drift
    // before it counts: one sample never ends a game.
    ClockHeartbeat hb_{hbKeyStr_, kClockTolerance, 5000, 3};
    bool started_ = false, quit_ = false, clockFlagged_ = false;
    std::uint64_t nextDbg_ = 0, nextMod_ = 0, nextClock_ = 0;
    std::set<std::string> seen_;
    std::vector<Finding> findings_;
};

} // namespace okayac
