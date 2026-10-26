#pragma once
// ---------------------------------------------------------------------------
// Dedicated (headless) game server: run a game scene with no window, renderer
// or audio, as the authoritative server players connect to. The shipped player
// does this with `okay-player --server ...`; the same code drives tests.
//
// What it does on top of NetworkManager:
//   - loads/uses the scene's NetworkManager (adds one if the scene has none),
//     puts it in dedicated mode (no host avatar, hears every room) and hosts;
//   - steps the scene at a fixed tick rate (simulation + networking);
//   - optionally only lets signed-in OkaySpace (Supabase) accounts join, with the
//     token checks running off the tick so a slow web request never stalls play;
//   - logs joins/leaves and a periodic status line, and stops cleanly on
//     Ctrl+C / SIGTERM (what container hosts send).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "okay/Net/AntiCheat.hpp"

namespace okay {

class Scene;

struct DedicatedServerOptions {
    std::uint16_t port = 45000;      ///< UDP port to listen on
    std::string scenePath;           ///< scene to run ("" = the game's startup scene)
    int   maxPlayers = 16;           ///< joins beyond this are refused ("server is full")
    float tickRate = 30.0f;          ///< simulation + snapshot ticks per second
    std::string name = "OkaySpace Server";   ///< shown to clients on join
    std::string password;            ///< "" = open
    bool  requireAuth = false;       ///< only signed-in OkaySpace accounts may join
    bool  encryption = true;         ///< encrypt traffic when both ends support it
    float statusEvery = 60.0f;       ///< seconds between status lines (0 = never)
    float maxRuntime = 0.0f;         ///< stop after this many seconds (0 = run until stopped)
    std::string logPath;             ///< --log: also write everything printed to this file ("" = no)
    // Server browser: list this server in the launcher's Internet tab (needs the
    // OkaySpace account signed in on this machine, and an address players reach).
    bool  advertise = false;
    std::string publicAddr;          ///< address players connect to (IP or hostname)
    std::string region;              ///< e.g. "EU", "US East"
    std::string game;                ///< game title shown in the browser (the player fills it in)
    // Anti-cheat basics (NetworkManager::maxMoveSpeed / maxMessagesPerSecond / kickAfterViolations).
    float maxSpeed = 0.0f;           ///< --max-speed: units per second (0 = off)
    int   rateLimit = 0;             ///< --rate-limit: custom messages per second per player (0 = off)
    int   kickAfter = 0;             ///< --kick-after: rejected moves in a minute before a kick (0 = never)
    /// --anticheat FILE / anticheat = FILE: the .okayac rules to enforce (loaded
    /// while reading the options, so a bad file stops startup with its error).
    std::string anticheatFile;
    AntiCheatSettings anticheat;     ///< what anticheatFile holds (enabled only when one was given)
};

/// Read `--server` and its flags (plus OKAY_SERVER_* environment variables, which
/// container hosts use) into `out`. Returns true when `--server` was given.
/// Flags: --port N, --scene PATH, --max-players N, --tick HZ, --name TEXT,
/// --password TEXT, --auth, --no-encryption, --status SECONDS, --run-for SECONDS, --log FILE,
/// --advertise, --public-addr HOST, --region TEXT.
/// Environment: OKAY_SERVER_PORT, OKAY_SERVER_MAX_PLAYERS, OKAY_SERVER_TICK,
/// OKAY_SERVER_NAME, OKAY_SERVER_PASSWORD, OKAY_SERVER_AUTH=1,
/// OKAY_SERVER_ADVERTISE=1, OKAY_SERVER_PUBLIC_ADDR, OKAY_SERVER_REGION. Flags win.
/// On a bad value, returns true with `error` set.
bool ParseDedicatedServerArgs(int argc, char** argv, DedicatedServerOptions& out, std::string* error = nullptr);
/// Read a server.cfg ("key = value" lines, # comments) into `out`: port, name,
/// max_players, tick, password, auth, encryption, status, scene, advertise,
/// public_addr, region, max_speed, rate_limit, kick_after. False with `error`
/// set on a bad value. ParseDedicatedServerArgs loads one automatically: the
/// file given with --config, else server.cfg in the working folder or beside the
/// executable. Precedence: config file < environment < flags.
bool LoadDedicatedServerConfig(const std::string& path, DedicatedServerOptions& out, std::string* error = nullptr);
/// Where the config came from ("" = none) after ParseDedicatedServerArgs.
const std::string& DedicatedServerConfigPath();
/// The usage text for --help.
const char* DedicatedServerUsage();

/// Token checks that never block the caller: the first Check() for a token
/// queues it for a background worker and returns -1 (pending); later calls
/// return the cached answer (1 accept with the user id, 0 reject). Answers are
/// cached for `cacheSeconds`. Plugs straight into NetworkManager::SetJoinCheck.
class AsyncTokenCheck {
public:
    using Verify = std::function<bool(const std::string& token, std::string& outUserId)>;
    explicit AsyncTokenCheck(Verify verify, float cacheSeconds = 300.0f);
    ~AsyncTokenCheck();
    AsyncTokenCheck(const AsyncTokenCheck&) = delete;
    AsyncTokenCheck& operator=(const AsyncTokenCheck&) = delete;

    /// 1 = valid (outUserId set), 0 = invalid / empty token, -1 = still checking.
    int Check(const std::string& token, std::string& outUserId);
    /// Number of checks waiting for the worker (diagnostics / tests).
    std::size_t Pending() const;

private:
    struct Impl;   // the worker thread + cache (native threads on Windows, std::thread elsewhere)
    std::unique_ptr<Impl> m;
};

/// Run `scene` as a dedicated server until stopped. Returns a process exit code
/// (0 = clean stop, 2 = couldn't open the port).
int RunDedicatedServer(Scene& scene, const DedicatedServerOptions& opts);
/// Ask a running RunDedicatedServer loop to stop (also what SIGINT/SIGTERM do).
void RequestDedicatedServerStop();

} // namespace okay
