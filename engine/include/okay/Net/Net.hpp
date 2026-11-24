#pragma once
// ---------------------------------------------------------------------------
// okay::Net — the front door to multiplayer: one line per action.
//
//   Net::Use(scene);                      // once: the scene the game plays in
//   Net::Host();                          // or Net::Join("192.168.1.20"), or Net::HostOrJoin()
//   Net::OnPlayerJoined([](uint32_t id, const std::string& name) { ... });
//   Net::On("hit", [](uint32_t from, const std::string& data) { ... });
//   Net::Send("hit", "10");               // runs on every player, this one too
//   Net::Spawn("prefabs/ball.okayprefab", {0, 1, 0});
//
// Everything here is a call onto the scene's NetworkManager (the one system;
// Net.hpp only finds it): what Net cannot do, the manager can (Net::Manager()).
// Mark an object as networked with a NetworkSync component (the Inspector's
// "Networked" tick) and its transform follows on every peer, smoothed.
// Two peers in one process (tests): give each its own scene and call the
// NetworkManager members directly, or Net::Use() before each call.
// ---------------------------------------------------------------------------
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/ServerQuery.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;

class Net {
public:
    using Player = NetworkManager::Player;

    /// The scene Host / Join / Spawn work in: its Network Manager is found, or
    /// added. Without it, Net uses the most recently started Network Manager.
    static NetworkManager* Use(Scene& scene);
    /// The Network Manager Net talks to (nullptr when there is none yet).
    static NetworkManager* Manager();

    /// Host a game (default port 45000). False, with LastError() in plain words, if it could not.
    static bool Host(std::uint16_t port = NetworkManager::kDefaultPort);
    /// Join a hosted game: "192.168.1.20", "192.168.1.20:7777" or a computer name.
    /// It returns at once; Joined() turns true when the host answers, or LastError()
    /// says why it never did (no answer after the join timeout, refused, ...).
    static bool Join(const std::string& address);
    /// Host if the port is free on this computer, else join the game hosted on it.
    static bool HostOrJoin(std::uint16_t port = NetworkManager::kDefaultPort);
    /// Leave the game (the host leaving ends it for everyone).
    static void Leave();

    static bool IsHost();
    /// In a game: hosting, or joined and answered by the host.
    static bool IsConnected();
    /// This player's id (0 = the host).
    static std::uint32_t MyId();
    /// Everyone in the game, you included.
    static std::vector<Player> Players();

    /// A named event to everyone (and here). Reliable: it arrives once, or the game ends.
    static bool Send(const std::string& event, const std::string& data = "");
    /// A named event to one player.
    static bool SendTo(std::uint32_t player, const std::string& event, const std::string& data = "");
    /// Listen for a named event. Returns an id for Off().
    static int On(const std::string& event, std::function<void(std::uint32_t from, const std::string& data)> f);
    static int OnPlayerJoined(std::function<void(std::uint32_t id, const std::string& name)> f);
    static int OnPlayerLeft(std::function<void(std::uint32_t id)> f);
    static void Off(int listener);

    /// Spawn a prefab on every peer, owned by this player (its transform follows this player's copy).
    static GameObject* Spawn(const std::string& prefab, const Vec3& position = Vec3::Zero);
    /// A shared variable everyone sees (the host decides; a client's write goes through it).
    static void SetVar(const std::string& key, const std::string& value);
    static std::string GetVar(const std::string& key);

    /// Network time in seconds: the same on every peer (the host's clock).
    static double Time();
    /// Round trip to the host in milliseconds (0 on the host).
    static float Ping();
    /// What last went wrong, in plain words ("" = nothing).
    static std::string LastError();

    /// Games hosted on the local network (a broadcast on the usual ports; waits up to `timeoutMs`).
    static std::vector<ServerInfo> FindLanGames(int timeoutMs = 1000);
    /// Join the first game FindLanGames sees. False (and LastError) if there is none.
    static bool JoinLanGame(int timeoutMs = 1000);
};

} // namespace okay
