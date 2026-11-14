#include "okay/Components/NetInventory.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/NetworkObject.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/InventoryPersistence.hpp"
#include "okay/Net/AntiCheat.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace {

int g_apply = 0;
float g_clock = 0.0f;
NetInventory* g_ticker = nullptr;
std::uint32_t g_nextReq = 1;
NetworkManager* g_bound = nullptr;
std::unordered_map<std::uint32_t, int> g_strikes;
std::unordered_map<std::uint32_t, std::uint32_t> g_lastReq;
std::unordered_map<std::uint32_t, std::pair<float, int>> g_rate;
std::unordered_map<std::string, std::unordered_set<std::uint32_t>> g_looking;
std::unordered_map<std::string, int> g_seen;
std::unordered_map<std::string, std::string> g_stash;   // verified user id -> okayinv text
std::vector<std::string> g_grants;
std::function<void(const std::string&, const std::string&)> g_backend;

struct ApplyGuard {
    ApplyGuard() { ++g_apply; }
    ~ApplyGuard() { --g_apply; }
};

NetworkManager* Manager(const Scene* s) {
    if (!s) return nullptr;
    for (const auto& up : s->Objects())
        if (up) if (auto* nm = up->GetComponent<NetworkManager>()) return nm;
    return nullptr;
}
bool LiveClient(const Scene* s) {
    NetworkManager* nm = Manager(s);
    return nm && nm->IsConnected() && !nm->IsServer();
}
std::string ObjectId(const GameObject* go) {
    if (!go) return {};
    if (auto* n = go->GetComponent<NetworkObject>()) {
        const std::string id = n->Id();
        if (!id.empty()) return id;
    }
    return go->name;
}
bool IsWorld(const NetInventory* ni) {
    return ni && ni->gameObject && ni->gameObject->GetComponent<ChestInventory>();
}
NetInventory* FindNi(Scene* s, const std::string& id) {
    if (!s || id.empty()) return nullptr;
    for (auto* ni : s->FindObjectsOfType<NetInventory>())
        if (ni && ObjectId(ni->gameObject) == id) return ni;
    return nullptr;
}
int OwnerPeer(const NetInventory* ni, const NetworkManager* nm) {
    if (!ni || !ni->gameObject || !nm) return -2;
    if (IsWorld(ni)) return -1;
    const std::string id = ObjectId(ni->gameObject);
    if (id.rfind("sp/", 0) == 0) {
        char* end = nullptr;
        const long v = std::strtol(id.c_str() + 3, &end, 10);
        if (end && end != id.c_str() + 3 && *end == '/') return (int)v;
    }
    if (ni->gameObject->name == "LocalPlayer" || ni->gameObject->tag == "Player")
        return (int)nm->LocalId();
    return -2;
}
GameObject* Body(Scene* s, std::uint32_t peer, NetworkManager* nm) {
    if (!s || !nm) return nullptr;
    if (peer == nm->LocalId()) return FindInventoryPlayer(s);
    if (GameObject* g = s->Find("Peer" + std::to_string(peer))) return g;
    const std::string prefix = "sp/" + std::to_string(peer) + "/";
    for (const auto& up : s->Objects()) {
        if (!up) continue;
        if (auto* o = up->GetComponent<NetworkObject>())
            if (o->Id().rfind(prefix, 0) == 0) return up.get();
    }
    return nullptr;
}
bool InRange(const NetInventory* ni, std::uint32_t peer, NetworkManager* nm) {
    if (!ni || ni->range <= 0.0f) return true;
    if (!ni->gameObject || !ni->gameObject->transform) return false;
    GameObject* body = Body(ni->gameObject->scene(), peer, nm);
    if (!body || !body->transform) return peer == nm->LocalId();
    const Vec3 d = body->transform->Position() - ni->gameObject->transform->Position();
    return d.x * d.x + d.y * d.y + d.z * d.z <= ni->range * ni->range;
}
bool Allowed(const NetInventory* ni, std::uint32_t peer, NetworkManager* nm) {
    const int owner = OwnerPeer(ni, nm);
    if (owner == -1) return InRange(ni, peer, nm);
    if (owner >= 0) return (std::uint32_t)owner == peer;
    return false;
}
std::vector<std::string> SplitBar(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '|') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
int TokI(const std::vector<std::string>& t, std::size_t i) {
    if (i >= t.size() || t[i].empty()) return -1;
    char* end = nullptr;
    const long v = std::strtol(t[i].c_str(), &end, 10);
    if (!end || *end) return -1;
    return (int)v;
}
std::string SafeFile(const std::string& s) {
    std::string o;
    for (unsigned char c : s)
        if (std::isalnum(c) || c == '-' || c == '_') o += (char)c;
    if (o.size() > 64) o.resize(64);
    return o;
}
void Remember(NetInventory* ni, NetworkManager* nm, std::uint32_t peer) {
    if (!ni || IsWorld(ni) || !nm) return;
    const std::string uid = peer == nm->LocalId() ? nm->LocalUserId() : nm->PeerUserId(peer);
    if (uid.empty() || !ni->gameObject) return;
    const std::string blob = InventoryPersistence::SerializeObject(*ni->gameObject);
    g_stash[uid] = blob;
    if (g_backend) g_backend(uid, blob);
    const std::string file = SafeFile(uid);
    if (!ni->saveDir.empty() && !file.empty())
        InventoryPersistence::SaveObject(*ni->gameObject, ni->saveDir + "/" + file + ".okinv");
}
void LogGrant(std::uint32_t peer, const char* op, const std::string& from, int n) {
    if (n <= 0) return;
    std::ostringstream o;
    o << "peer " << peer << " " << op << " +" << n << " from " << from;
    g_grants.push_back(o.str());
    if (g_grants.size() > 200) g_grants.erase(g_grants.begin(), g_grants.begin() + (g_grants.size() - 200));
}
void Strike(NetworkManager* nm, const NetInventory* rules, std::uint32_t peer) {
    if (!nm || peer == nm->LocalId()) return;
    const int need = rules && rules->cheatThreshold > 0 ? rules->cheatThreshold : 8;
    if (++g_strikes[peer] < need) return;
    g_strikes[peer] = 0;
    nm->anticheat.Report(peer, CheatKind::RpcRejected, "inventory");
}
bool RateOk(std::uint32_t peer, int maxPerSecond) {
    if (maxPerSecond <= 0) return true;
    auto& w = g_rate[peer];
    if (g_clock - w.first >= 1.0f) { w.first = g_clock; w.second = 0; }
    return ++w.second <= maxPerSecond;
}
bool ShouldSee(const NetInventory* ni, std::uint32_t peer, NetworkManager* nm) {
    if (!IsWorld(ni)) return OwnerPeer(ni, nm) == (int)peer;
    if (!g_looking[ObjectId(ni->gameObject)].count(peer)) return false;
    return InRange(ni, peer, nm);
}
void SendState(NetworkManager* nm, NetInventory* ni, std::uint32_t peer, bool full) {
    if (!nm || !ni || !ni->gameObject) return;
    std::ostringstream o;
    o << ni->Version() << "|" << ObjectId(ni->gameObject) << "|" << (full ? "full" : "hidden") << "\n";
    if (full) o << InventoryPersistence::SerializeObject(*ni->gameObject);
    nm->SendReliableTo(peer, "__rpc/inv/state", o.str());
}
void Publish(NetworkManager* nm, NetInventory* ni, std::uint32_t only = 0xffffffffu) {
    if (!nm || !ni || !nm->IsServer()) return;
    ni->BumpVersion();
    auto sendOne = [&](std::uint32_t peer) {
        if (peer == nm->LocalId()) return;
        const bool see = ShouldSee(ni, peer, nm);
        if (!see && !(IsWorld(ni) && ni->hideUntilOpen)) return;
        SendState(nm, ni, peer, see);
    };
    if (only != 0xffffffffu) { sendOne(only); return; }
    // Peer ids are not listed publicly. Anyone currently looking, plus the owner, is enough;
    // a hello from a new peer asks again.
    std::unordered_set<std::uint32_t> peers = g_looking[ObjectId(ni->gameObject)];
    const int owner = OwnerPeer(ni, nm);
    if (owner > 0) peers.insert((std::uint32_t)owner);
    for (std::uint32_t p : peers) sendOne(p);
}
void ApplyState(Scene* s, const std::string& data) {
    const auto nl = data.find('\n');
    const std::string head = nl == std::string::npos ? data : data.substr(0, nl);
    const std::string body = nl == std::string::npos ? std::string() : data.substr(nl + 1);
    const auto tok = SplitBar(head);
    if (tok.size() < 3) return;
    const int ver = TokI(tok, 0);
    NetInventory* ni = FindNi(s, tok[1]);
    if (!ni || !ni->gameObject || ver < 0) return;
    if (g_seen[tok[1]] > ver) return;
    g_seen[tok[1]] = ver;
    ni->SetVersion(ver);
    ApplyGuard guard;
    if (tok[2] == "hidden") {
        if (auto* inv = ni->gameObject->GetComponent<Inventory>()) { inv->slots.clear(); inv->NotifyChanged(); }
        if (auto* g = ni->gameObject->GetComponent<GridInventory>()) { g->items.clear(); g->overflow.clear(); }
        return;
    }
    if (tok[2] == "full") InventoryPersistence::LoadObjectText(*ni->gameObject, body);
}
void DestroyPickup(Scene* s, const std::string& id) {
    if (!s || id.empty()) return;
    std::vector<GameObject*> gone;
    for (const auto& up : s->Objects()) {
        if (!up || !up->GetComponent<ItemPickup>()) continue;
        if (ObjectId(up.get()) == id) gone.push_back(up.get());
    }
    for (GameObject* g : gone) s->Destroy(g);
}
NetInventory* Rules(Scene* s) {
    if (!s) return nullptr;
    for (auto* ni : s->FindObjectsOfType<NetInventory>()) if (ni) return ni;
    return nullptr;
}
bool Send(GameObject* go, const char* rpc, const std::string& body) {
    if (g_apply || !go) return false;
    NetworkManager* nm = Manager(go->scene());
    if (!nm || !nm->IsConnected()) return false;
    if (nm->IsServer()) return false;   // the host already applied it; Publish follows from the caller when needed
    NetInventory* rules = Rules(go->scene());
    if (!rules) return false;
    const std::string payload = std::to_string(g_nextReq++) + "|" + body;
    if (rules->maxRequestBytes > 0 && payload.size() > (std::size_t)rules->maxRequestBytes) return false;
    nm->SendReliableTo(0, std::string("__rpc/") + rpc, payload);
    return true;
}
void HostPublishTouched(GameObject* a, GameObject* b) {
    if (g_apply) return;
    Scene* s = a ? a->scene() : (b ? b->scene() : nullptr);
    NetworkManager* nm = Manager(s);
    if (!nm || !nm->IsServer()) return;
    if (a) if (auto* ni = a->GetComponent<NetInventory>()) Publish(nm, ni);
    if (b && b != a) if (auto* ni = b->GetComponent<NetInventory>()) Publish(nm, ni);
}
bool BadCount(int n) { return n < 0 || n > 1000000; }

void OnRequest(Scene* s, std::uint32_t peer, const std::string& rpc, const std::string& data) {
    NetworkManager* nm = Manager(s);
    NetInventory* rules = Rules(s);
    if (!nm || !nm->IsServer() || !rules) return;
    if (peer == nm->LocalId()) return;
    if (rules->maxRequestBytes > 0 && data.size() > (std::size_t)rules->maxRequestBytes) { Strike(nm, rules, peer); return; }
    if (!RateOk(peer, rules->maxPerSecond)) { Strike(nm, rules, peer); return; }
    if (rpc == "inv/add") { Strike(nm, rules, peer); return; }   // no client-authored grant

    const auto tok = SplitBar(data);
    if (tok.size() < 2) { Strike(nm, rules, peer); return; }
    const int req = TokI(tok, 0);
    if (req < 0) { Strike(nm, rules, peer); return; }
    if (g_lastReq[peer] == (std::uint32_t)req) {
        if (NetInventory* again = FindNi(s, tok.size() > 1 ? tok[1] : std::string()))
            Publish(nm, again, peer);
        return;
    }
    g_lastReq[peer] = (std::uint32_t)req;

    auto fail = [&](NetInventory* ni, bool cheat) {
        if (cheat) Strike(nm, rules, peer);
        if (ni) Publish(nm, ni, peer);   // roll the client back
    };

    if (rpc == "inv/hello" || rpc == "inv/look") {
        NetInventory* ni = FindNi(s, tok[1]);
        if (!ni) { Strike(nm, rules, peer); return; }
        if (rpc == "inv/look") {
            const int on = tok.size() > 2 ? TokI(tok, 2) : 0;
            if (on == 1 && InRange(ni, peer, nm)) g_looking[tok[1]].insert(peer);
            else g_looking[tok[1]].erase(peer);
        }
        if (!IsWorld(ni)) {
            const std::string uid = nm->PeerUserId(peer);
            if (!uid.empty()) {
                auto it = g_stash.find(uid);
                if (it == g_stash.end() && !ni->saveDir.empty()) {
                    const std::string file = ni->saveDir + "/" + SafeFile(uid) + ".okinv";
                    if (ni->gameObject && InventoryPersistence::LoadObject(*ni->gameObject, file))
                        g_stash[uid] = InventoryPersistence::SerializeObject(*ni->gameObject);
                } else if (it != g_stash.end() && ni->gameObject) {
                    ApplyGuard guard;
                    InventoryPersistence::LoadObjectText(*ni->gameObject, it->second);
                }
            }
        }
        const bool see = ShouldSee(ni, peer, nm);
        SendState(nm, ni, peer, see);   // version unchanged: a hello is not a mutation
        return;
    }
    if (rpc == "inv/pickup") {
        if (tok.size() < 3) { Strike(nm, rules, peer); return; }
        GameObject* who = nullptr;
        if (NetInventory* bag = FindNi(s, tok[2])) {
            if (!Allowed(bag, peer, nm)) { fail(bag, true); return; }
            who = bag->gameObject;
        }
        if (!who) who = Body(s, peer, nm);
        GameObject* pkGo = nullptr;
        for (const auto& up : s->Objects())
            if (up && up->GetComponent<ItemPickup>() && ObjectId(up.get()) == tok[1]) { pkGo = up.get(); break; }
        auto* pk = pkGo ? pkGo->GetComponent<ItemPickup>() : nullptr;
        if (!pk || !who) return;   // lost the race; not a cheat
        if (pkGo->transform && who->transform && rules->range > 0.0f) {
            const Vec3 d = pkGo->transform->Position() - who->transform->Position();
            if (d.x * d.x + d.y * d.y + d.z * d.z > rules->range * rules->range) { Strike(nm, rules, peer); return; }
        }
        const std::string item = pk->item;
        const int before = pk->count;
        const int n = pk->GiveTo(who);   // destroys the pickup once it's empty
        if (n > 0) LogGrant(peer, "pickup", item, n);
        if (n > 0 && n >= before) nm->SendReliable("__rpc/inv/gone", tok[1]);
        if (NetInventory* bag = FindNi(s, tok[2])) Publish(nm, bag, peer);
        return;
    }
    if (rpc == "inv/craft") {
        NetInventory* st = FindNi(s, tok.size() > 1 ? tok[1] : std::string());
        const int idx = TokI(tok, 2), times = TokI(tok, 3);
        if (!st || !st->gameObject || idx < 0 || times < 1 || times > 999) { fail(st, true); return; }
        if (!InRange(st, peer, nm)) { fail(st, true); return; }
        auto* cs = st->gameObject->GetComponent<CraftingStation>();
        if (!cs) { fail(st, true); return; }
        ApplyGuard guard;
        const bool ok = cs->Craft(idx, times);
        if (ok) LogGrant(peer, "craft", tok[1], 1);
        if (Inventory* inv = cs->Inv()) if (auto* bag = inv->gameObject ? inv->gameObject->GetComponent<NetInventory>() : nullptr) {
            Remember(bag, nm, peer);
            Publish(nm, bag, peer);
        }
        Publish(nm, st, peer);
        return;
    }

    NetInventory* a = FindNi(s, tok.size() > 1 ? tok[1] : std::string());
    if (!a || !a->gameObject) { Strike(nm, rules, peer); return; }
    if (!Allowed(a, peer, nm)) { fail(a, true); return; }
    Inventory* ia = a->gameObject->GetComponent<Inventory>();
    GridInventory* ga = a->gameObject->GetComponent<GridInventory>();
    ApplyGuard guard;
    bool ok = false;
    NetInventory* b = nullptr;
    if (rpc == "inv/move") {
        const int si = TokI(tok, 2);
        b = FindNi(s, tok.size() > 3 ? tok[3] : std::string());
        const int di = TokI(tok, 4), n = TokI(tok, 5);
        if (!b || !b->gameObject || si < 0 || di < 0 || BadCount(n) || !Allowed(b, peer, nm)) { fail(a, true); return; }
        ok = InventoryOps::Move(ia, si, b->gameObject->GetComponent<Inventory>(), di, n).Ok();
    } else if (rpc == "inv/split") {
        const int slot = TokI(tok, 2), n = TokI(tok, 3);
        if (slot < 0 || n < 1 || BadCount(n)) { fail(a, true); return; }
        ok = InventoryOps::Split(ia, slot, n).Ok();
    } else if (rpc == "inv/quick") {
        const int from = TokI(tok, 2), hotbar = TokI(tok, 3);
        if (from < 0 || hotbar < 1 || hotbar > 20) { fail(a, true); return; }
        ok = InventoryOps::QuickMove(ia, from, hotbar).Ok();
    } else if (rpc == "inv/take") {
        const int slot = TokI(tok, 2);
        b = FindNi(s, tok.size() > 3 ? tok[3] : std::string());
        if (!b || !b->gameObject || !ia || slot < 0 || slot >= (int)ia->slots.size() || !Allowed(b, peer, nm)) {
            fail(a, true); return;
        }
        Inventory* ib = b->gameObject->GetComponent<Inventory>();
        const std::string taken = (ia && slot < (int)ia->slots.size()) ? ia->slots[slot].item : std::string();
        const InvResult moved = InventoryOps::Transfer(ia, slot, ib);
        ok = moved.Ok();
        if (ok && IsWorld(a)) LogGrant(peer, "take", taken, moved.moved);
    } else if (rpc == "inv/drop") {
        const int slot = TokI(tok, 2), n = TokI(tok, 3);
        if (!ia || slot < 0 || slot >= (int)ia->slots.size() || n < 1 || BadCount(n)) { fail(a, true); return; }
        Inventory::Slot dropped = ia->slots[slot];
        if (n < dropped.count) dropped.count = n;
        ok = InventoryOps::Drop(ia, slot, n).Ok();
        if (ok && a->gameObject->scene() && a->gameObject->transform)
            ItemPickup::Spawn(*a->gameObject->scene(), dropped.item, dropped.count, dropped.data,
                              a->gameObject->transform->Position());
    } else if (rpc == "inv/use") {
        const int slot = TokI(tok, 2);
        if (slot < 0) { fail(a, true); return; }
        ok = InventoryOps::Use(ia, slot, a->gameObject).Ok();
    } else if (rpc == "inv/equip") {
        const int from = TokI(tok, 2), to = TokI(tok, 3);
        Equipment* eq = a->gameObject->GetComponent<Equipment>();
        if (!eq) eq = Equipment::Of(a->gameObject);
        if (!ia || !eq || from < 0 || to < -1) { fail(a, true); return; }
        ok = eq->EquipFrom(*ia, from, to).Ok();
    } else if (rpc == "inv/unequip") {
        const int slot = TokI(tok, 2);
        Equipment* eq = a->gameObject->GetComponent<Equipment>();
        if (!eq) eq = Equipment::Of(a->gameObject);
        if (!ia || !eq || slot < 0) { fail(a, true); return; }
        ok = eq->Unequip(slot, *ia).Ok();
    } else if (rpc == "inv/gmove") {
        const int index = TokI(tok, 2);
        b = FindNi(s, tok.size() > 3 ? tok[3] : std::string());
        const int x = TokI(tok, 4), y = TokI(tok, 5), rot = TokI(tok, 6);
        if (!ga || !b || !b->gameObject || index < 0 || x < 0 || y < 0 || !Allowed(b, peer, nm)) { fail(a, true); return; }
        ok = InventoryOps::Move(ga, index, b->gameObject->GetComponent<GridInventory>(), x, y, rot != 0).Ok();
    } else if (rpc == "inv/gsplit") {
        const int index = TokI(tok, 2), n = TokI(tok, 3);
        if (!ga || index < 0 || n < 1 || BadCount(n)) { fail(a, true); return; }
        ok = InventoryOps::Split(ga, index, n).Ok();
    } else {
        Strike(nm, rules, peer);
        return;
    }
    if (!ok) fail(a, false);   // a full stack is a rollback, not a cheat
    else {
        Remember(a, nm, peer);
        if (b) Remember(b, nm, peer);
    }
    Publish(nm, a);
    if (b) Publish(nm, b);
}

void Bind(Scene* s, NetworkManager* nm) {
    if (!s || !nm || g_bound == nm) return;
    g_bound = nm;
    auto wrap = [s, nm](const char* name) {
        nm->OnRpc(name, [s, name](std::uint32_t from, const std::string& data) {
            if (!s) return;
            NetworkManager* live = Manager(s);
            if (live && live->IsServer()) OnRequest(s, from, name, data);
            else if (std::string(name) == "inv/state") ApplyState(s, data);
            else if (std::string(name) == "inv/gone") DestroyPickup(s, data);
        });
    };
    const char* names[] = {"inv/move", "inv/split", "inv/quick", "inv/take", "inv/drop", "inv/use",
                           "inv/equip", "inv/unequip", "inv/craft", "inv/pickup", "inv/look", "inv/hello",
                           "inv/add", "inv/state", "inv/gone"};
    for (const char* n : names) wrap(n);
}

} // namespace

void NetInventory::SetBackendSave(std::function<void(const std::string&, const std::string&)> f) { g_backend = std::move(f); }
const std::vector<std::string>& NetInventory::GrantLog() { return g_grants; }

void NetInventory::Start() { Update(0.0f); }

void NetInventory::OnDestroy() {
    if (g_ticker == this) g_ticker = nullptr;
    // OnRpc replaces, so the next bag in a rebuilt scene registers against the live manager
    // instead of keeping handlers that captured the old scene.
    g_bound = nullptr;
}

void NetInventory::Update(float dt) {
    if (!g_ticker || g_ticker == this) { g_ticker = this; g_clock += dt; }
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    NetworkManager* nm = Manager(s);
    if (nm) Bind(s, nm);
    if (!LiveClient(s)) return;
    if (IsWorld(this) && !m_cleared) {
        m_cleared = true;
        if (auto* inv = gameObject->GetComponent<Inventory>()) { inv->slots.clear(); inv->NotifyChanged(); }
        if (auto* g = gameObject->GetComponent<GridInventory>()) { g->items.clear(); g->overflow.clear(); }
    }
    if (!m_saidHello) {
        m_saidHello = true;
        Send(gameObject, "inv/hello", ObjectId(gameObject));
    }
    bool look = false;
    if (auto* ch = gameObject->GetComponent<ChestInventory>()) look = ch->open;
    if (auto* ui = gameObject->GetComponent<InventoryUI>()) look = look || ui->open;
    if (look != m_looking) {
        m_looking = look;
        Send(gameObject, "inv/look", ObjectId(gameObject) + "|" + (look ? "1" : "0"));
    }
}

void NetInventory::NoticeMove(Inventory* src, int si, Inventory* dst, int di, int amount, bool ok) {
    if (!ok || !src || !dst) return;
    Send(src->gameObject, "inv/move", ObjectId(src->gameObject) + "|" + std::to_string(si) + "|" + ObjectId(dst->gameObject) + "|" + std::to_string(di) + "|" + std::to_string(amount));
    HostPublishTouched(src->gameObject, dst->gameObject);
}
void NetInventory::NoticeSplit(Inventory* inv, int slot, int amount, bool ok) {
    if (!ok || !inv) return;
    Send(inv->gameObject, "inv/split", ObjectId(inv->gameObject) + "|" + std::to_string(slot) + "|" + std::to_string(amount));
    HostPublishTouched(inv->gameObject, nullptr);
}
void NetInventory::NoticeQuick(Inventory* inv, int from, int hotbar, bool ok) {
    if (!ok || !inv) return;
    Send(inv->gameObject, "inv/quick", ObjectId(inv->gameObject) + "|" + std::to_string(from) + "|" + std::to_string(hotbar));
    HostPublishTouched(inv->gameObject, nullptr);
}
void NetInventory::NoticeTransfer(Inventory* from, int slot, Inventory* to, bool ok) {
    if (!ok || !from || !to) return;
    Send(from->gameObject, "inv/take", ObjectId(from->gameObject) + "|" + std::to_string(slot) + "|" + ObjectId(to->gameObject));
    HostPublishTouched(from->gameObject, to->gameObject);
}
void NetInventory::NoticeDrop(Inventory* inv, int slot, int n, bool ok) {
    if (!ok || !inv) return;
    Send(inv->gameObject, "inv/drop", ObjectId(inv->gameObject) + "|" + std::to_string(slot) + "|" + std::to_string(n));
    HostPublishTouched(inv->gameObject, nullptr);
}
void NetInventory::NoticeUse(Inventory* inv, int slot, bool ok) {
    if (!ok || !inv) return;
    Send(inv->gameObject, "inv/use", ObjectId(inv->gameObject) + "|" + std::to_string(slot));
    HostPublishTouched(inv->gameObject, nullptr);
}
void NetInventory::NoticeGridMove(GridInventory* src, int index, GridInventory* dst, int x, int y, bool rotated, bool ok) {
    if (!ok || !src || !dst) return;
    Send(src->gameObject, "inv/gmove", ObjectId(src->gameObject) + "|" + std::to_string(index) + "|" + ObjectId(dst->gameObject)
         + "|" + std::to_string(x) + "|" + std::to_string(y) + "|" + (rotated ? "1" : "0"));
    HostPublishTouched(src->gameObject, dst->gameObject);
}
void NetInventory::NoticeGridSplit(GridInventory* inv, int index, int n, bool ok) {
    if (!ok || !inv) return;
    Send(inv->gameObject, "inv/gsplit", ObjectId(inv->gameObject) + "|" + std::to_string(index) + "|" + std::to_string(n));
    HostPublishTouched(inv->gameObject, nullptr);
}
void NetInventory::NoticeEquip(Inventory* inv, int from, int equipSlot, bool ok) {
    if (!ok || !inv) return;
    Send(inv->gameObject, "inv/equip", ObjectId(inv->gameObject) + "|" + std::to_string(from) + "|" + std::to_string(equipSlot));
    HostPublishTouched(inv->gameObject, nullptr);
}
void NetInventory::NoticeUnequip(Inventory* inv, int equipSlot, bool ok) {
    if (!ok || !inv) return;
    Send(inv->gameObject, "inv/unequip", ObjectId(inv->gameObject) + "|" + std::to_string(equipSlot));
    HostPublishTouched(inv->gameObject, nullptr);
}
void NetInventory::NoticeCraft(GameObject* station, int index, int times) {
    if (!station || index < 0 || times < 1) return;
    Send(station, "inv/craft", ObjectId(station) + "|" + std::to_string(index) + "|" + std::to_string(times));
    GameObject* bag = nullptr;
    if (auto* cs = station->GetComponent<CraftingStation>())
        if (Inventory* inv = cs->Inv()) bag = inv->gameObject;
    HostPublishTouched(station, bag);
}

bool NetInventory::DeferPickup(ItemPickup* pk, GameObject* who) {
    if (g_apply || !pk || !pk->gameObject || !who) return false;
    Scene* s = pk->gameObject->scene();
    if (!LiveClient(s) || !Rules(s)) return false;
    const std::string body = ObjectId(pk->gameObject) + "|" + ObjectId(who);
    if (Send(who, "inv/pickup", body)) return true;
    return Send(pk->gameObject, "inv/pickup", body);
}

bool NetInventory::LocalWorldDrops(const GameObject* carrier) {
    if (!carrier) return true;
    if (!carrier->GetComponent<NetInventory>()) return true;
    return !LiveClient(carrier->scene());
}

} // namespace okay
