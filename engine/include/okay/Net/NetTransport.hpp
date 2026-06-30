#pragma once
// ---------------------------------------------------------------------------
// INetTransport — the seam for the realtime *transport* (host/join, send, RPC,
// peers). It exists so the engine isn't married to its homegrown UDP+relay stack:
// a Photon (or other) transport can be dropped in later by implementing this
// interface, with no changes to game code.
//
// The default provider (Native) wraps the existing NetworkManager and forwards to
// it, so behavior is unchanged. Photon/Custom ship as stubs until an adapter is
// compiled in — note that a real Photon transport needs its (binary, per-platform)
// SDK, which is why it's an opt-in extension point rather than bundled: it would
// otherwise break the engine's self-contained, builds-everywhere model.
// ---------------------------------------------------------------------------
#include "okay/Net/NetworkManager.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace okay {

/// Which realtime transport backs multiplayer. Native = the built-in UDP/relay
/// NetworkManager. Photon/Custom are extension points — stubs until built.
enum class NetTransportProvider { Native, Photon, Custom };

/// Realtime transport interface — the verbs a game needs to go online, decoupled
/// from any one implementation. Calls degrade safely when no transport is bound.
class INetTransport {
public:
    virtual ~INetTransport() = default;

    virtual const char* BackendName() const = 0;
    /// True when this transport is usable (e.g. its SDK/socket layer is present).
    virtual bool Available() const = 0;

    // ---- Connection ----
    virtual bool StartServer(std::uint16_t port) = 0;
    virtual bool StartClient(const std::string& host, std::uint16_t port) = 0;
    virtual bool HostViaRelay(const std::string& relayHost, std::uint16_t relayPort, const std::string& code) = 0;
    virtual bool JoinViaRelay(const std::string& relayHost, std::uint16_t relayPort, const std::string& code) = 0;
    virtual void Stop() = 0;

    // ---- Status ----
    virtual bool IsServer() const = 0;
    virtual bool IsClient() const = 0;
    virtual bool IsConnected() const = 0;
    virtual std::uint32_t LocalId() const = 0;
    virtual std::size_t PeerCount() const = 0;

    // ---- Messaging ----
    virtual void Send(const std::string& channel, const std::string& data) = 0;
    virtual void SendReliable(const std::string& channel, const std::string& data) = 0;
    virtual void Rpc(const std::string& name, const std::string& data = "") = 0;
    virtual void OnRpc(const std::string& name, std::function<void(std::uint32_t, const std::string&)> f) = 0;
};

/// Build a transport for the given provider. For Native, pass the NetworkManager
/// it should drive (the one in the scene). Other providers return a stub until
/// their adapter is compiled in.
std::unique_ptr<INetTransport> CreateNetTransport(NetTransportProvider provider = NetTransportProvider::Native,
                                                  NetworkManager* native = nullptr);

} // namespace okay
