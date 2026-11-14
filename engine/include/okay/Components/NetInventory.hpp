#pragma once
// Host-owned inventory. Clients send inv/* requests; the host checks them, applies
// them through InventoryOps, and sends back a versioned snapshot. There is no
// SyncVar for contents, and a client cannot grant itself an item.
//
// Put this on a player bag or a world container (a chest). A world container's
// scene-file loot is kept on the host only: a client clears it until the host
// says what is inside, and only while that client is in range and looking.
#include "okay/Scene/Component.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

class Inventory;
class GridInventory;
class ItemPickup;
class GameObject;

class NetInventory : public Behaviour {
public:
    float range = 3.0f;            ///< world containers: how close a requester must be (0 = any)
    int   maxPerSecond = 20;       ///< requests one client may make (0 = no extra limit)
    int   cheatThreshold = 8;      ///< invalid requests before On Cheat Detected, detail "inventory"
    int   maxRequestBytes = 480;   ///< client payloads longer than this are refused
    std::string saveDir;           ///< host writes <userId>.okinv here when a player bag changes
    bool  hideUntilOpen = true;    ///< world containers: don't send contents to clients who aren't looking

    void Start() override;
    void Update(float dt) override;
    void OnDestroy() override;

    int Version() const { return m_version; }
    void SetVersion(int v) { m_version = v; }
    int BumpVersion() { return ++m_version; }

    static void NoticeMove(Inventory* src, int si, Inventory* dst, int di, int amount, bool ok);
    static void NoticeSplit(Inventory* inv, int slot, int amount, bool ok);
    static void NoticeQuick(Inventory* inv, int from, int hotbar, bool ok);
    static void NoticeTransfer(Inventory* from, int slot, Inventory* to, bool ok);
    static void NoticeDrop(Inventory* inv, int slot, int n, bool ok);
    static void NoticeUse(Inventory* inv, int slot, bool ok);
    static void NoticeGridMove(GridInventory* src, int index, GridInventory* dst, int x, int y, bool rotated, bool ok);
    static void NoticeGridSplit(GridInventory* inv, int index, int n, bool ok);
    static void NoticeEquip(Inventory* inv, int from, int equipSlot, bool ok);
    static void NoticeUnequip(Inventory* inv, int equipSlot, bool ok);
    static void NoticeCraft(GameObject* station, int index, int times);
    /// Client: ask the host to pick this up and do nothing locally. False = collect as usual.
    static bool DeferPickup(ItemPickup* pk, GameObject* who);
    /// False on a connected client whose bag is networked: the host spawns the world drop.
    static bool LocalWorldDrops(const GameObject* carrier);

    /// Optional host hook when a signed-in player's bag changes. No SQL.
    static void SetBackendSave(std::function<void(const std::string& userId, const std::string& blob)> f);
    static const std::vector<std::string>& GrantLog();

private:
    bool m_cleared = false;
    bool m_saidHello = false;
    bool m_looking = false;
    int  m_version = 0;
};

} // namespace okay
