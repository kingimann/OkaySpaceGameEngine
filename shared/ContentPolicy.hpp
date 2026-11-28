#pragma once
// Live Updates rules the running game asks about. Design note: docs/design/live_updates.md. Pure functions: no files, no network, no
// clock (the caller passes what it knows), so every rule is tested on its own (tests/test_content_delivery.cpp).
//
//   CheckCompat  can this client play on this server? Game version, content version and network protocol, with plain words for the
//                person and which side has to update. For the multiplayer join step (the server answers a joining client with it).
//   DecideApply  may a downloaded, verified content version (Sync with stageOnly, ContentSync.hpp) be made visible now? The publisher's
//                wish (the manifest's `apply` line) or the game's default, against what the game is doing at this moment.
//
// Written for this project from the behaviour the owner asked for (2026-11-28); versions are compared with okayver (ProjectVersion.hpp),
// the same rules as the rest of the engine. Header only, no threads.
#include <cstdint>
#include <string>
#include <vector>

#include "ContentManifest.hpp"
#include "ProjectVersion.hpp"

namespace okaycontent {

// ---- Version compatibility ------------------------------------------------------------------------------------------------------

/// What one side runs. `protocol` is the newest network protocol it speaks, `minProtocol` the oldest (0 = only `protocol`).
struct PeerVersions {
    std::string game;                ///< the exported game's version ("1.4.2"; "" = unknown)
    std::uint64_t content = 0;       ///< the visible content version (0 = only the installed files)
    std::uint32_t protocol = 0;
    std::uint32_t minProtocol = 0;
};

/// How strict a game is. The defaults are the safe ones: same game version, same content version.
struct CompatRules {
    enum class Game { Exact, SameMajorMinor, Any };
    Game game = Game::Exact;
    /// How many content versions a client may be BEHIND the server and still join (0 = must be equal). A client AHEAD of the server is
    /// refused unless `allowClientAhead` (its newer data could disagree with the server's).
    std::uint64_t contentBehind = 0;
    bool allowClientAhead = false;
};

enum class CompatVerdict { Accept, ProtocolMismatch, GameMismatch, ClientContentOld, ClientContentNew };
enum class UpdateSide { None, Client, Server };

struct Compat {
    CompatVerdict verdict = CompatVerdict::Accept;
    UpdateSide update = UpdateSide::None;   ///< who has to update to make it work
    std::string message;                    ///< plain words ("" when accepted)
    bool Accepts() const { return verdict == CompatVerdict::Accept; }
};

namespace detail {
// "1.4.2" -> {1,4,2}; false for anything that is not plain numbers and dots.
inline bool VersionParts(const std::string& v, std::vector<std::uint64_t>& out) {
    out.clear();
    if (!VersionOk(v)) return false;
    for (const std::string& p : Split(v, '.')) { std::uint64_t n = 0; if (!Digits(p, n, ~0ull >> 1)) return false; out.push_back(n); }
    return true;
}
}  // namespace detail

inline Compat CheckCompat(const PeerVersions& server, const PeerVersions& client, const CompatRules& rules = CompatRules()) {
    Compat c;
    auto refuse = [&](CompatVerdict v, UpdateSide side, std::string msg) { c.verdict = v; c.update = side; c.message = std::move(msg); return c; };
    // 1. protocol: the two ranges [minProtocol, protocol] must meet
    const std::uint32_t sLow = server.minProtocol ? server.minProtocol : server.protocol, cLow = client.minProtocol ? client.minProtocol : client.protocol;
    if (client.protocol < sLow)
        return refuse(CompatVerdict::ProtocolMismatch, UpdateSide::Client, "This game is too old for the server (network protocol " + std::to_string(client.protocol) +
                                                                                ", the server needs " + std::to_string(sLow) + " or newer). Update the game.");
    if (server.protocol < cLow)
        return refuse(CompatVerdict::ProtocolMismatch, UpdateSide::Server, "The server is too old for this game (network protocol " + std::to_string(server.protocol) +
                                                                                ", this game needs " + std::to_string(cLow) + " or newer).");
    // 2. game version
    if (rules.game != CompatRules::Game::Any) {
        std::vector<std::uint64_t> s, k;
        const bool known = detail::VersionParts(server.game, s) && detail::VersionParts(client.game, k);
        bool same = known;
        if (known && rules.game == CompatRules::Game::Exact) same = okayver::CompareVersions(server.game, client.game) == 0;
        if (known && rules.game == CompatRules::Game::SameMajorMinor) same = s[0] == k[0] && s.size() > 1 && k.size() > 1 && s[1] == k[1];
        if (!same) {
            const UpdateSide side = !known ? UpdateSide::None : (okayver::CompareVersions(client.game, server.game) < 0 ? UpdateSide::Client : UpdateSide::Server);
            const std::string sv = server.game.empty() ? "unknown" : server.game, cv = client.game.empty() ? "unknown" : client.game;
            return refuse(CompatVerdict::GameMismatch, side, "The server runs game version " + sv + " and this game is version " + cv + "." +
                                                                 (side == UpdateSide::Client ? " Update the game." : side == UpdateSide::Server ? " The server must be updated." : ""));
        }
    }
    // 3. content version
    if (client.content > server.content && !rules.allowClientAhead)
        return refuse(CompatVerdict::ClientContentNew, UpdateSide::Server, "This game has newer content (version " + std::to_string(client.content) +
                                                                               ") than the server (version " + std::to_string(server.content) + ").");
    if (client.content < server.content && server.content - client.content > rules.contentBehind)
        return refuse(CompatVerdict::ClientContentOld, UpdateSide::Client, "The server has newer content (version " + std::to_string(server.content) +
                                                                               ") than this game (version " + std::to_string(client.content) + "). Update the content and join again.");
    return c;
}

// ---- When a downloaded version may become visible --------------------------------------------------------------------------------

/// What the game is doing right now (the game fills this in each time it asks).
struct GameMoment {
    bool startingUp = false;      ///< before any content is loaded (the "next launch" moment)
    bool loadingScreen = false;   ///< a loading screen is up (no scene is running)
    bool inMatch = false;         ///< a match / round / level is being played
};

struct ApplyDecision {
    bool applyNow = false;
    ApplyWhen policy = ApplyWhen::NextLaunch;   ///< the policy that decided
    std::string reason;                         ///< plain words for the log
};

/// `published` is the manifest's `apply` (GameDefault = none given); `gameDefault` is the game's own setting (GameDefault there means
/// NextLaunch, the safest). `staged` is whether a verified version is waiting (Sync with stageOnly returned Status::Staged).
/// Start-up applies every policy: nothing is loaded yet, so it is always safe.
inline ApplyDecision DecideApply(ApplyWhen published, ApplyWhen gameDefault, bool staged, const GameMoment& now) {
    ApplyDecision d;
    d.policy = published != ApplyWhen::GameDefault ? published : (gameDefault != ApplyWhen::GameDefault ? gameDefault : ApplyWhen::NextLaunch);
    auto say = [&](bool now_, const char* why) { d.applyNow = now_; d.reason = why; return d; };
    if (!staged) return say(false, "no content update is waiting");
    if (now.startingUp) return say(true, "the game is starting: nothing is loaded yet");
    switch (d.policy) {
        case ApplyWhen::Immediately: return say(true, "the update is marked to apply immediately");
        case ApplyWhen::AtLoadingScreen:
            return now.loadingScreen ? say(true, "a loading screen is up") : say(false, "waiting for the next loading screen");
        case ApplyWhen::BetweenMatches:
            return !now.inMatch ? say(true, "no match is being played") : say(false, "waiting for the match to end");
        case ApplyWhen::NextLaunch:
        case ApplyWhen::GameDefault: break;
    }
    return say(false, "waiting for the next start of the game");
}

}  // namespace okaycontent
