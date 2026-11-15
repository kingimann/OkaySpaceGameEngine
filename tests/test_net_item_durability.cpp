// Item durability in multiplayer (okay/Inventory/Durability.hpp, NetInventory): a host and a client in one process,
// joined over the loopback network. The host decides: on the client, wear and repair are refused with "the host
// decides" and change nothing; the host's wear and repair (in the bag and on a worn piece) reach the client's copy of
// the bag through the inventory snapshot. Written for this engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Equipment.hpp"
#include "okay/Components/NetInventory.hpp"
#include "okay/Components/NetworkObject.hpp"
#include "okay/Inventory/Durability.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Net/NetworkManager.hpp"
#include <chrono>
#include <functional>
#include <string>

using namespace okay;

namespace {
void Def(const char* id, const char* maxDur, EquipSlot equip = EquipSlot::None) {
    ItemDefinition d;
    d.id = id; d.name = id; d.equip = equip;
    if (maxDur) d.custom.push_back({"maxDurability", maxDur});
    ItemDatabase::Get().Put(d);
}

struct Side {
    Scene scene;
    NetworkManager* nm = nullptr;
    Inventory* bag = nullptr;
    Equipment* eq = nullptr;
    explicit Side(const char* name) : scene(name) {
        nm = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->interpolationRate = 0.0f;
    }
    // The client's player bag: the same id on both peers ("sp/<client>/1" names the client as its owner).
    void MakeBag(const std::string& id) {
        GameObject* me = scene.CreateGameObject("Me");
        me->AddComponent<NetworkObject>()->netId = id;
        bag = me->AddComponent<Inventory>();
        bag->capacity = 4;
        bag->slots.assign(4, Inventory::Slot{});
        bag->slots[0] = {"pick", 1, {}};
        eq = me->AddComponent<Equipment>();
        eq->Set(eq->Find("Head"), "helmet");
        me->AddComponent<NetInventory>();
    }
};

// Step both scenes until `done` or about 3 seconds pass (loopback packets arrive between steps).
bool Until(Side& a, Side& b, const std::function<bool()>& done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done() && std::chrono::steady_clock::now() < end) {
        a.scene.Update(0.02f);
        b.scene.Update(0.02f);
    }
    return done();
}
} // namespace

int main() {
    RUN_SUITE("net_item_durability");
    ItemDatabase::Get().Clear();
    Def("pick", "100");
    Def("helmet", "50", EquipSlot::Head);
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    Side host("Host"), cli("Client");
    CHECK(host.nm->StartServer(0));
    host.scene.Start();
    CHECK(cli.nm->StartClient("127.0.0.1", host.nm->ServerPort()));
    cli.scene.Start();
    CHECK(Until(host, cli, [&] { return cli.nm->Joined(); }));
    const std::string id = "sp/" + std::to_string(cli.nm->LocalId()) + "/1";
    host.MakeBag(id);
    cli.MakeBag(id);
    // a few updates: both bags start, the client says hello and the host answers with its snapshot
    for (int i = 0; i < 20; ++i) { host.scene.Update(0.02f); cli.scene.Update(0.02f); }
    CHECK(cli.bag->slots.size() == 4 && cli.bag->slots[0].item == "pick");

    {   // the client may not decide: wear and repair are refused there and nothing changes
        CHECK(NetInventory::HostDecides(cli.bag->gameObject));
        CHECK(!NetInventory::HostDecides(host.bag->gameObject));
        const InvResult w = durability::Wear(cli.bag, 0, 10.0f);
        CHECK(!w.Ok() && w.reason == "the host decides");
        CHECK(cli.bag->slots[0].data.durability < 0.0f);             // still never worn (full)
        const InvResult we = durability::WearEquipped(cli.eq, cli.eq->Find("Head"), 5.0f);
        CHECK(!we.Ok() && we.reason == "the host decides");
        CHECK(durability::Repair(cli.bag, 0).reason == "the host decides");
    }
    {   // the host wears the pick: the client's copy follows after a few steps
        CHECK(durability::Wear(host.bag, 0, 30.0f).Ok());
        CHECK(host.bag->slots[0].data.durability == 70.0f);
        CHECK(Until(host, cli, [&] { return cli.bag->slots[0].data.durability == 70.0f; }));
        CHECK(cli.bag->slots[0].item == "pick");
    }
    {   // the host wears the worn helmet: the client's Equipment follows
        const int head = host.eq->Find("Head");
        CHECK(durability::WearEquipped(host.eq, head, 20.0f).Ok());
        const int chead = cli.eq->Find("Head");
        CHECK(Until(host, cli, [&] { return cli.eq->slots[chead].data.durability == 30.0f; }));
    }
    {   // the host repairs the pick to full: the client sees 100
        CHECK(durability::Repair(host.bag, 0).Ok());
        CHECK(Until(host, cli, [&] { return cli.bag->slots[0].data.durability == 100.0f; }));
    }

    cli.nm->Stop();
    host.nm->Stop();
    Log::sink = sink;
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
