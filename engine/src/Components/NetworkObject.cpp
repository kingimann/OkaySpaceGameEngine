#include "okay/Components/NetworkObject.hpp"

#include "okay/Components/NetworkSync.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {
namespace {

NetworkManager* FindManager(const GameObject* go) {
    Scene* sc = go ? go->scene() : nullptr;
    if (!sc) return nullptr;
    for (const auto& up : sc->Objects())
        if (GameObject* g = up.get())
            if (auto* nm = g->GetComponent<NetworkManager>()) return nm;
    return nullptr;
}

std::string RpcToken(std::string name) {
    for (char& c : name)
        if (c == ' ' || c == '\t' || c == ':' || c == '/') c = '_';
    return name;
}

bool NetworkSyncOwns(const GameObject* go, const std::string& id) {
    if (!go) return false;
    auto* ns = go->GetComponent<NetworkSync>();
    if (!ns) return false;
    const std::string nid = ns->netId.empty() ? go->name : ns->netId;
    return nid == id;
}

} // namespace

std::string NetworkObject::Id() const {
    if (!netId.empty()) return netId;
    return gameObject ? gameObject->name : std::string{};
}

NetworkManager* NetworkObject::Manager() const { return FindManager(gameObject); }

bool NetworkObject::IsLocallyOwned() const {
    NetworkManager* nm = Manager();
    if (!nm) return true;
    const std::string id = Id();
    if (!id.empty()) {
        const int a = nm->MovementAuthority(id);
        if (a >= 0) return a != 0;
    }
    switch (authority) {
        case Authority::Host:   return nm->IsServer();
        case Authority::Mine:   return true;
        case Authority::Manual: return owned;
    }
    return owned;
}

void NetworkObject::SetOwned(bool o) {
    owned = o;
    NetworkManager* nm = Manager();
    const std::string id = Id();
    if (nm && !id.empty() && nm->HasSync(id)) nm->SetSyncOwned(id, IsLocallyOwned());
}

void NetworkObject::EnsureHook() const {
    NetworkManager* nm = Manager();
    if (!nm || nm->HasRpcObserver()) return;
    nm->SetRpcObserver([nm](const std::string& full, std::uint32_t from, const std::string& data) {
        const auto slash = full.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 >= full.size()) return;
        if (!nm->gameObject || !nm->gameObject->scene()) return;
        const std::string id = full.substr(0, slash);
        const std::string rpc = full.substr(slash + 1);
        std::vector<NetworkObject*> found;
        for (const auto& up : nm->gameObject->scene()->Objects()) {
            if (!up) continue;
            if (auto* o = up->GetComponent<NetworkObject>())
                if (o->Id() == id) found.push_back(o);
        }
        for (NetworkObject* o : found) o->DeliverRpc(rpc, from, data);
    });
}

void NetworkObject::Rpc(const std::string& name, const std::string& data) {
    if (name.empty() || Id().empty()) return;
    EnsureHook();
    if (NetworkManager* nm = Manager()) nm->Rpc(Id() + "/" + name, data);
}

void NetworkObject::RpcTo(std::uint32_t targetId, const std::string& name, const std::string& data) {
    if (name.empty() || Id().empty()) return;
    EnsureHook();
    if (NetworkManager* nm = Manager()) nm->RpcTo(targetId, Id() + "/" + name, data);
}

void NetworkObject::RpcReliable(const std::string& name, const std::string& data) {
    if (name.empty() || Id().empty()) return;
    EnsureHook();
    if (NetworkManager* nm = Manager()) nm->RpcReliable(Id() + "/" + name, data);
}

void NetworkObject::OnRpc(const std::string& name, std::function<void(std::uint32_t, const std::string&)> f) {
    if (name.empty()) return;
    m_handlers[name] = std::move(f);
}

bool NetworkObject::PollRpc(std::uint32_t& from, std::string& name, std::string& data) {
    if (m_inbox.empty()) return false;
    from = m_inbox.front().from;
    name = std::move(m_inbox.front().name);
    data = std::move(m_inbox.front().data);
    m_inbox.erase(m_inbox.begin());
    return true;
}

void NetworkObject::DeliverRpc(const std::string& name, std::uint32_t from, const std::string& data) {
    if (m_inbox.size() >= 32) m_inbox.erase(m_inbox.begin());
    m_inbox.push_back(Queued{from, name, data});
    auto it = m_handlers.find(name);
    if (it != m_handlers.end() && it->second) it->second(from, data);
    if (!gameObject) return;
    const std::string token = RpcToken(name);
    const std::string entry = "OnNetRpc:" + token;
    for (auto* vc : gameObject->GetComponents<VisualScriptComponent>())
        vc->RaiseEvent(entry.c_str(), nullptr, name, vs::VsValue(data));
    for (auto* sc : gameObject->GetComponents<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventWith("OnNetRpc_" + token, vs::VsValue(data));
}

void NetworkObject::Start() { EnsureHook(); }
void NetworkObject::Update(float) { EnsureHook(); }

void NetworkTransform::Bind() {
    if (!gameObject || !gameObject->transform) return;
    NetworkManager* nm = FindManager(gameObject);
    if (!nm) return;
    auto* obj = gameObject->GetComponent<NetworkObject>();
    const std::string id = obj ? obj->Id() : gameObject->name;
    if (id.empty()) return;
    if (NetworkSyncOwns(gameObject, id)) {
        m_bound = false;
        return;
    }
    const bool mine = obj ? obj->IsLocallyOwned() : nm->IsServer();
    if (m_bound && m_id != id) {
        nm->UnregisterSync(m_id);
        m_bound = false;
    }
    if (m_bound) {
        nm->SetSyncOwned(id, mine);
        return;
    }
    if (nm->HasSync(id)) return;
    nm->RegisterSync(id, gameObject->transform, mine);
    m_bound = true;
    m_id = id;
}

void NetworkTransform::OnDestroy() {
    if (!m_bound) return;
    if (NetworkManager* nm = FindManager(gameObject)) nm->UnregisterSync(m_id);
    m_bound = false;
}

NetworkManager* SyncVar::Manager() const { return FindManager(gameObject); }

std::string SyncVar::ObjectId() const {
    if (!gameObject) return {};
    if (auto* obj = gameObject->GetComponent<NetworkObject>()) return obj->Id();
    return gameObject->name;
}

std::string SyncVar::Key() const {
    const std::string id = ObjectId();
    if (id.empty()) return name;
    return id + "." + name;
}

bool SyncVar::Owned() const {
    if (auto* obj = gameObject ? gameObject->GetComponent<NetworkObject>() : nullptr)
        return obj->IsLocallyOwned();
    NetworkManager* nm = Manager();
    return !nm || nm->IsServer();
}

void SyncVar::Set(const std::string& v) {
    // A remote copy follows the owner. Setting it here would be overwritten
    // on the next pull, so ignore the write once a manager exists.
    if (!Owned() && Manager()) return;
    value = v;
    m_haveSent = false;
    Replicate();
}

std::string SyncVar::Get() {
    if (!Owned()) Replicate();
    return value;
}

void SyncVar::Replicate() {
    if (name.empty()) return;
    NetworkManager* nm = Manager();
    if (!nm) return;
    const std::string key = Key();
    if (key.empty()) return;
    if (Owned()) {
        if (m_haveSent && value == m_sent) return;
        nm->SetVar(key, value);
        m_sent = value;
        m_haveSent = true;
        return;
    }
    value = nm->GetVar(key);
    m_sent = value;
    m_haveSent = true;
}

} // namespace okay
