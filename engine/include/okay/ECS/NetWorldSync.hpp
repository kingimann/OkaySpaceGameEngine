#pragma once
// ---------------------------------------------------------------------------
// okay::ecs::WorldReplicator — drive NetWorld replication over the engine's real
// NetworkManager, so an ECS game goes online with two calls. The server snapshots
// its authoritative world and broadcasts it as a named reliable RPC each tick;
// every client applies incoming snapshots to its mirror world. Uses the RPC
// channel (named, so it never collides with the game's other messages).
//
//   // server (authority)
//   ecs::WorldReplicator rep(server.netWorld, nm);
//   rep.broadcast();                 // each tick after your systems run
//
//   // client (mirror)
//   ecs::WorldReplicator rep(client.netWorld, nm);
//   rep.listen();                    // once at startup; the world updates itself
// ---------------------------------------------------------------------------
#include "okay/ECS/NetWorld.hpp"
#include "okay/Net/NetworkManager.hpp"
#include <string>

namespace okay {
namespace ecs {

class WorldReplicator {
public:
    WorldReplicator(NetWorld& world, NetworkManager& nm, std::string rpcName = "ecs.snapshot")
        : m_world(world), m_nm(nm), m_rpc(std::move(rpcName)) {}

    /// Client: start applying snapshots the server broadcasts (call once).
    void listen() {
        NetWorld* w = &m_world;
        m_nm.OnRpc(m_rpc, [w](std::uint32_t /*from*/, const std::string& data) {
            w->applyStr(data);
        });
    }

    /// Server: broadcast the current world to all clients (call each tick).
    void broadcast() {
        m_nm.RpcReliable(m_rpc, m_world.snapshotStr());
    }

    /// Stop applying snapshots (client).
    void stop() { m_nm.ClearRpc(m_rpc); }

private:
    NetWorld&       m_world;
    NetworkManager& m_nm;
    std::string     m_rpc;
};

} // namespace ecs
} // namespace okay
