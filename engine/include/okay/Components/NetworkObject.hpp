#pragma once
// ---------------------------------------------------------------------------
// NetworkObject, NetworkTransform, SyncVar — a small component API on top of
// the existing NetworkManager (RegisterSync / SetVar / Rpc). No second
// replication system.
//
//   NetworkObject     shared id + who is allowed to drive this object, and
//                     named RPCs ("<netId>/<name>") that land on this object.
//   NetworkTransform  replicates this transform through RegisterSync. If a
//                     NetworkSync already owns the same id, this component
//                     leaves that registration alone.
//   SyncVar           one string field. The owner writes it (SetVar) only when
//                     it changes; everyone else copies GetVar. Key is
//                     "<netId>.<name>".
//
// A net id must not contain '/'. An RPC name must not contain '/' either:
// the first '/' splits id from name.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Net/NetworkManager.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class NetworkObject : public Behaviour {
public:
    enum class Authority { Host, Mine, Manual };

    std::string netId;                       ///< blank = the object's name
    Authority   authority = Authority::Host;
    bool        owned = true;               ///< used when authority == Manual

    std::string Id() const;
    /// True when this peer should drive the object. No NetworkManager (single
    /// player) is always true. Authoritative movement overrides the enum.
    bool IsLocallyOwned() const;
    NetworkManager* Manager() const;
    void SetOwned(bool o);

    /// Broadcast / direct / reliable RPC. The name is local to this object
    /// ("Open", not a global channel). Other peers with the same net id receive
    /// it; the sender does not run its own handler.
    void Rpc(const std::string& name, const std::string& data = "");
    void RpcTo(std::uint32_t targetId, const std::string& name, const std::string& data = "");
    void RpcReliable(const std::string& name, const std::string& data = "");
    /// Local handler for an RPC named `name` (sender id, payload).
    void OnRpc(const std::string& name, std::function<void(std::uint32_t, const std::string&)> f);

    /// Pop one received RPC. False when the inbox is empty.
    bool PollRpc(std::uint32_t& from, std::string& name, std::string& data);

    void Start() override;
    void Update(float) override;

    /// Manager observer entry. Games use OnRpc / PollRpc.
    void DeliverRpc(const std::string& name, std::uint32_t from, const std::string& data);

private:
    void EnsureHook() const;
    struct Queued {
        std::uint32_t from = 0;
        std::string name;
        std::string data;
    };
    std::vector<Queued> m_inbox;
    std::unordered_map<std::string, std::function<void(std::uint32_t, const std::string&)>> m_handlers;
};

/// Replicates this object's transform. Same RegisterSync path as NetworkSync.
class NetworkTransform : public Behaviour {
public:
    void Start() override { Bind(); }
    void Update(float) override { Bind(); }
    void OnDestroy() override;

private:
    void Bind();
    bool m_bound = false;
    std::string m_id;
};

/// One replicated string on this object.
class SyncVar : public Behaviour {
public:
    std::string name = "var";
    std::string value;

    void Set(const std::string& v);
    /// The current value. A remote pulls the latest networked copy first.
    std::string Get();
    /// "<netId>.<name>" (or just the name when the object has no id).
    std::string Key() const;

    void Start() override { Replicate(); }
    void Update(float) override { Replicate(); }

private:
    void Replicate();
    bool Owned() const;
    NetworkManager* Manager() const;
    std::string ObjectId() const;
    std::string m_sent;
    bool m_haveSent = false;
};

} // namespace okay
