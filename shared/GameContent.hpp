#pragma once
// Game-side wiring of content delivery: read the optional configuration, run one sync at start-up, load the overlay. Docs:
// docs/release/content_delivery.md "In the game". OFF unless a content URL is configured: with no URL nothing here does anything (no folder is
// made, no request is sent, the overlay stays empty), so a game that does not use it behaves exactly as before.
//
// Configuration (all optional; the environment wins over the file, except keys: see below):
//   content.okayconfig  beside the game files, lines "key=value" ('#' starts a comment):
//       url=https://host/path/        where content.manifest lives (http, https, or file:///folder for a local test)
//       game=mygame                   the game id the manifest must carry
//       keys=<64 hex>[,<64 hex>]      ed25519 public keys the manifest must be signed by (rotation: two)
//       groups=hd,voice-fr            only these groups (empty = all)
//       budget-mb=512                 most megabytes of content this game keeps
//   environment: OKAY_CONTENT_URL, OKAY_CONTENT_GAME, OKAY_CONTENT_KEYS, OKAY_CONTENT_GROUPS, OKAY_CONTENT_BUDGET_MB,
//                OKAY_CONTENT_ALLOW_UNSIGNED=1 (developer switch: skips the signature; the log says so every time)
//   A key compiled in with -DOKAY_CONTENT_TRUSTED_KEYS=<hex> beats the file and the environment: a shipped game that has its key built in
//   cannot be pointed at another publisher by a text file.
// Header only, no threads (the sync runs on the caller's thread), no engine code (validators live in GameContentValidators.hpp).
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "ContentHttpTransport.hpp"
#include "ContentOverlay.hpp"
#include "ContentSync.hpp"

#ifndef OKAY_CONTENT_TRUSTED_KEYS
#define OKAY_CONTENT_TRUSTED_KEYS ""
#endif

namespace okaycontent {

struct GameContentConfig {
    std::string url, game, keys;
    std::vector<std::string> groups;
    std::uint64_t budgetBytes = 0;
    bool allowUnsigned = false;
    bool Enabled() const { return !url.empty(); }
};

/// The environment as a function, so tests can fake it. Returns "" for unset.
using EnvFn = std::function<std::string(const char*)>;
inline EnvFn RealEnv() { return [](const char* n) { const char* v = std::getenv(n); return v ? std::string(v) : std::string(); }; }

inline std::string Trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

inline std::vector<std::string> SplitGroups(const std::string& v) {
    std::vector<std::string> out;
    for (const std::string& g : detail::Split(v, ',')) if (!Trim(g).empty()) out.push_back(Trim(g));
    return out;
}

/// Read content.okayconfig text. Unknown keys and bad numbers are reported in `why` and make it return false (a typo should not quietly
/// turn the check off); `cfg` then holds what was understood so far.
inline bool ParseConfigText(const std::string& text, GameContentConfig& cfg, std::string* why = nullptr) {
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) { if (why) *why = "content.okayconfig line " + std::to_string(n) + ": expected key=value"; return false; }
        const std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
        if (k == "url") cfg.url = v;
        else if (k == "game") cfg.game = v;
        else if (k == "keys") cfg.keys = v;
        else if (k == "groups") cfg.groups = SplitGroups(v);
        else if (k == "budget-mb") { std::uint64_t mb = 0; if (!detail::Digits(v, mb, 1u << 20)) { if (why) *why = "content.okayconfig: budget-mb is not a number"; return false; } cfg.budgetBytes = mb << 20; }
        else { if (why) *why = "content.okayconfig line " + std::to_string(n) + ": unknown key '" + k + "'"; return false; }
    }
    return true;
}

/// Build the configuration: file text first, then the environment over it, then the compiled-in key over everything.
inline GameContentConfig ResolveConfig(const std::string& fileText, const EnvFn& env, std::string* why = nullptr) {
    GameContentConfig c;
    ParseConfigText(fileText, c, why);
    if (std::string v = env("OKAY_CONTENT_URL"); !v.empty()) c.url = v;
    if (std::string v = env("OKAY_CONTENT_GAME"); !v.empty()) c.game = v;
    if (std::string v = env("OKAY_CONTENT_KEYS"); !v.empty()) c.keys = v;
    if (std::string v = env("OKAY_CONTENT_GROUPS"); !v.empty()) { c.groups = SplitGroups(v); }
    if (std::string v = env("OKAY_CONTENT_BUDGET_MB"); !v.empty()) { std::uint64_t mb = 0; if (detail::Digits(v, mb, 1u << 20)) c.budgetBytes = mb << 20; }
    c.allowUnsigned = env("OKAY_CONTENT_ALLOW_UNSIGNED") == "1";
    if (std::string(OKAY_CONTENT_TRUSTED_KEYS).size()) c.keys = OKAY_CONTENT_TRUSTED_KEYS;
    return c;
}

inline std::string SlurpFile(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct StartOutcome {
    bool enabled = false;          ///< a content URL was configured
    bool synced = false;           ///< the sync ran (it may have failed)
    SyncResult sync;
    bool overlayActive = false;    ///< downloaded files are now served ahead of the installed ones
    std::string line;              ///< one plain status line for the log / startup stack line
};

/// Start-up step. With no URL: returns at once, `enabled` false, nothing touched. With one: sync (blocking) into `contentRoot`, then load
/// the overlay from whatever is committed there (so a failed or offline sync still serves the last good content). `transport` may be
/// given (tests); otherwise one is made from the URL. `validate` is the script validator (GameContentValidators.hpp).
inline StartOutcome StartGameContent(const GameContentConfig& cfg, const std::string& contentRoot, const std::string& appVersion,
                                     std::function<std::string(const Entry&, const std::string&)> validate, std::function<void(int)> sleepMs,
                                     std::function<void(const std::string&)> log, Transport* transport = nullptr, Overlay* overlay = &Overlay::Global()) {
    StartOutcome o;
    if (!cfg.Enabled()) return o;
    o.enabled = true;
    auto say = [&](const std::string& s) { o.line = s; if (log) log(s); };
    if (cfg.game.empty()) { say("content: a content URL is set but no game id (game=...): skipped"); return o; }
    if (cfg.allowUnsigned) if (log) log("content: WARNING the signature check is OFF (OKAY_CONTENT_ALLOW_UNSIGNED=1)");
    std::unique_ptr<Transport> owned;
    if (!transport) {
        std::error_code ec;
        fs::create_directories(fs::u8path(contentRoot) / "scratch", ec);
        owned = MakeTransport(cfg.url, (fs::u8path(contentRoot) / "scratch").u8string());
        if (!owned) { say("content: the content URL is not a file://, http:// or https:// address: skipped"); return o; }
        transport = owned.get();
    }
    SyncConfig sc;
    sc.root = contentRoot; sc.game = cfg.game; sc.appVersion = appVersion; sc.trustedKeys = cfg.keys; sc.allowUnsigned = cfg.allowUnsigned;
    sc.groups = cfg.groups; sc.budgetBytes = cfg.budgetBytes; sc.validate = std::move(validate); sc.sleepMs = std::move(sleepMs); sc.log = log;
    o.sync = Sync(sc, *transport).Run();
    o.synced = true;
    std::string why;
    o.overlayActive = overlay && overlay->Load(contentRoot, cfg.game, cfg.keys, cfg.allowUnsigned, &why);
    std::string s = std::string("content: ") + (o.sync.status == Status::Updated ? "updated" : o.sync.status == Status::UpToDate ? "up to date" : "sync failed (" + std::string(ReasonName(o.sync.reason)) + ")");
    if (o.overlayActive) s += ", serving downloaded content version " + std::to_string(overlay->Version());
    else s += ", using the installed files only";
    if (o.sync.status == Status::Failed || o.sync.status == Status::Cancelled) s += ". " + o.sync.message;
    say(s);
    return o;
}

}  // namespace okaycontent
