#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Math/Vec3.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace okay {

class Transform;
class GameObject;

/// Authoritative-relay multiplayer over UDP. One peer runs as the server; the
/// rest connect as clients. Each peer owns a "local avatar" Transform whose
/// position it broadcasts; the manager mirrors every other peer into the scene
/// via a spawn factory and keeps their Transforms in sync each frame.
///
/// This is intentionally small but real: it opens sockets, performs a join
/// handshake, serializes state, and reconciles a snapshot — the backbone every
/// networked game needs.
class NetworkManager : public Behaviour {
public:
    enum class Mode { Offline, Server, Client };

    /// Begin listening for clients on the given UDP port.
    bool StartServer(std::uint16_t port);
    /// Connect to a server at host:port (host may be a name or dotted IP).
    bool StartClient(const std::string& host, std::uint16_t port);
    void Stop();

    Mode mode() const { return m_mode; }
    bool IsServer() const { return m_mode == Mode::Server; }
    bool IsClient() const { return m_mode == Mode::Client; }
    /// This peer's network id (0 for the server, >0 for clients once joined).
    std::uint32_t LocalId() const { return m_localId; }
    /// The UDP port the server is bound to (0 if not a server).
    std::uint16_t ServerPort() const { return m_socket.LocalPort(); }
    /// True once a client has completed the join handshake.
    bool Joined() const { return m_joined; }
    /// Number of remote peers currently mirrored into the scene.
    std::size_t PeerCount() const { return m_remotes.size(); }

    /// The Transform whose state this peer broadcasts (e.g. its player).
    void SetLocalAvatar(Transform* t, char glyph = '@') {
        m_localAvatar = t; m_localGlyph = glyph;
    }
    /// Called when a new remote peer appears; return a GameObject to represent
    /// it (its Transform will be driven by incoming snapshots).
    void SetRemoteFactory(std::function<GameObject*(std::uint32_t id, char glyph)> f) {
        m_spawnRemote = std::move(f);
    }

    void Update(float dt) override;
    void OnDestroy() override { Stop(); }

private:
    struct PeerState { std::uint32_t id; float x, y, z; char glyph; };
    struct Client {
        net::Endpoint endpoint;
        std::uint32_t id;
        PeerState state;
        float lastSeen;
    };

    void ServerTick(float dt);
    void ClientTick(float dt);
    GameObject* EnsureRemote(std::uint32_t id, char glyph);
    void ApplyPeer(const PeerState& s);

    Mode m_mode = Mode::Offline;
    net::UdpSocket m_socket;
    bool m_netStarted = false;

    // Local peer
    Transform* m_localAvatar = nullptr;
    char       m_localGlyph  = '@';
    std::uint32_t m_localId  = 0;

    // Client-only
    net::Endpoint m_serverEp{};
    bool     m_joined = false;
    float    m_joinTimer = 0.0f;

    // Server-only
    std::unordered_map<net::Endpoint, Client, net::EndpointHash> m_clients;
    std::uint32_t m_nextId = 1;
    float m_snapshotTimer = 0.0f;
    float m_snapshotInterval = 0.05f; // 20 Hz

    // Remotes mirrored into the scene
    std::function<GameObject*(std::uint32_t, char)> m_spawnRemote;
    std::unordered_map<std::uint32_t, GameObject*>  m_remotes;
};

} // namespace okay
