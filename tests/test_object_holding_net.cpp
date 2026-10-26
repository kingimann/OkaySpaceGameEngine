#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ProceduralObjectHolding.hpp"

#include <chrono>
#include <thread>

using namespace okay;

// Holding online: the server validates a pickup and owns the decision; every peer
// holds the same object on its copy of the character (only the holding state is
// sent); refused, forged and out-of-reach pickups don't stick.
namespace {

GameObject* Player(Scene& s, const char* name, const std::string& netId, NetworkSync::Authority auth, bool owned, Vec3 at) {
    GameObject* p = Templates::BuildPlayerBody(s, at, name);
    p->GetComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
    p->AddComponent<ProceduralObjectHolding>();
    auto* ns = p->AddComponent<NetworkSync>();
    ns->netId = netId; ns->authority = auth; ns->owned = owned;
    return p;
}

GameObject* Crate(Scene& s, const char* name, Vec3 at, bool holdable = true) {
    GameObject* c = s.CreateGameObject(name);
    c->transform->localPosition = at;
    c->transform->localScale = {0.5f, 0.4f, 0.4f};
    c->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f);
    c->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Kinematic;
    if (holdable) c->AddComponent<HoldableObject>();
    return c;
}

ProceduralObjectHolding* H(GameObject* g) { return g->GetComponent<ProceduralObjectHolding>(); }

} // namespace

int main() {
    RUN_SUITE("object_holding_net");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    Scene ss("Server");
    auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
    CHECK(srv->StartServer(0));
    Scene cs("Client");
    auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
    CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
    auto pump = [&](int n) {
        for (int i = 0; i < n; ++i) {
            ss.Update(0.02f); cs.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    ss.Start(); cs.Start();
    for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
    CHECK(cli->Joined());

    // The client's player ("sp/<its id>/1", driven by the client) and the host's,
    // plus the same crates in both scenes.
    const std::string heroId = "sp/" + std::to_string(cli->LocalId()) + "/1";
    GameObject* sHero = Player(ss, "Hero", heroId, NetworkSync::Authority::Manual, false, {0, 0, 0});
    GameObject* cHero = Player(cs, "Hero", heroId, NetworkSync::Authority::Manual, true, {0, 0, 0});
    GameObject* sHost = Player(ss, "Host", "host", NetworkSync::Authority::Host, true, {4, 0, 0});
    GameObject* cHost = Player(cs, "Host", "host", NetworkSync::Authority::Host, false, {4, 0, 0});
    GameObject* sCrate = Crate(ss, "Crate", {0, 0.2f, -0.8f});
    GameObject* cCrate = Crate(cs, "Crate", {0, 0.2f, -0.8f});
    Crate(ss, "Far", {0, 0.2f, -9.0f});
    GameObject* cFar = Crate(cs, "Far", {0, 0.2f, -9.0f});
    GameObject* sBox = Crate(ss, "Box", {4, 0.2f, -0.8f});
    GameObject* cBox = Crate(cs, "Box", {4, 0.2f, -0.8f});
    Crate(ss, "Plain", {0.5f, 0.2f, -0.5f}, false);
    GameObject* cPlain = Crate(cs, "Plain", {0.5f, 0.2f, -0.5f}, false);
    pump(30);

    // The client picks up the crate: held at once, and the server agrees.
    CHECK(H(cHero)->Pickup(cCrate));
    CHECK(H(cHero)->Held() == cCrate);
    pump(40);
    CHECK(H(sHero)->Held() == sCrate);
    CHECK(srv->GetVar("srv.hold/" + heroId) == "Crate|1|0");
    CHECK(H(cHero)->Held() == cCrate);
    // The server's copy is really held: hands on, the crate in front of the hero.
    CHECK(H(sHero)->HandInUse(true) && H(sHero)->HandInUse(false));
    CHECK(sCrate->transform->Position().y > 0.6f);

    // The host picks up its box: the client sees the host's character holding it.
    CHECK(H(sHost)->Pickup(sBox));
    pump(40);
    CHECK(H(cHost)->Held() == cBox);
    CHECK(cBox->transform->Position().y > 0.6f);
    CHECK(H(cHost)->HandInUse(true));
    // Nobody but its owner can pick up for a character.
    CHECK(!H(cHost)->Pickup(cFar));

    // Out of reach: the client's optimistic pickup is refused and undone.
    CHECK(H(cHero)->Pickup(cFar));
    pump(40);
    CHECK(H(sHero)->Held() == sCrate);           // the server never let go of the crate...
    CHECK(H(cHero)->Held() == cCrate);           // ...and the client is back to holding it
    // Not holdable: refused before anything is sent.
    CHECK(!H(cHero)->Pickup(cPlain));

    // A forged request for the host's character is refused.
    GameObject* sSpare = Crate(ss, "Spare", {4, 0.2f, 0.8f});
    Crate(cs, "Spare", {4, 0.2f, 0.8f});
    pump(5);
    cli->RpcTo(0, "hold", "host|Spare|1");
    pump(20);
    CHECK(H(sHost)->Held() == sBox);
    CHECK(sSpare->GetComponent<HoldableObject>()->Holder() == nullptr);

    // A style change goes through the server and reaches the other peers.
    CHECK(H(cHost)->CarryStyle() == HoldableObject::Style::Box);
    CHECK(H(sHost)->SetStyle(HoldableObject::Style::Heavy));
    pump(40);
    CHECK(cBox->GetComponent<HoldableObject>()->style == HoldableObject::Style::Heavy);
    CHECK(H(cHost)->CarryStyle() == HoldableObject::Style::Heavy);
    CHECK(H(cHero)->SetStyle(HoldableObject::Style::Rifle));
    pump(40);
    CHECK(sCrate->GetComponent<HoldableObject>()->style == HoldableObject::Style::Rifle);
    CHECK(srv->GetVar("srv.hold/" + heroId) == "Crate|1|3");
    CHECK(!H(cHost)->SetStyle(HoldableObject::Style::Box));   // not ours

    // A late joiner sees both holds.
    Scene ls("Late");
    auto* late = ls.CreateGameObject("Net")->AddComponent<NetworkManager>();
    CHECK(late->StartClient("127.0.0.1", srv->ServerPort()));
    GameObject* lHero = Player(ls, "Hero", heroId, NetworkSync::Authority::Manual, false, {0, 0, 0});
    GameObject* lHost = Player(ls, "Host", "host", NetworkSync::Authority::Host, false, {4, 0, 0});
    GameObject* lCrate = Crate(ls, "Crate", {0, 0.2f, -0.8f});
    GameObject* lBox = Crate(ls, "Box", {4, 0.2f, -0.8f});
    ls.Start();
    auto pump3 = [&](int n) {
        for (int i = 0; i < n; ++i) {
            ss.Update(0.02f); cs.Update(0.02f); ls.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    for (int i = 0; i < 300 && !late->Joined(); ++i) pump3(1);
    pump3(40);
    CHECK(H(lHero)->Held() == lCrate);
    CHECK(H(lHost)->Held() == lBox);

    // The client drops: the server and the other client let go too.
    H(cHero)->Drop();
    CHECK(!H(cHero)->IsHolding());
    pump3(40);
    CHECK(!H(sHero)->IsHolding());
    CHECK(!H(lHero)->IsHolding());
    CHECK(srv->GetVar("srv.hold/" + heroId).empty());
    CHECK(sCrate->GetComponent<HoldableObject>()->Holder() == nullptr);

    // A local hold (an equipped item each peer builds itself) sends nothing and
    // isn't undone by the shared state.
    GameObject* sTorch = Crate(ss, "Torch", {0, 0.2f, 0.6f});
    GameObject* cTorch = Crate(cs, "Torch", {0, 0.2f, 0.6f});
    CHECK(H(cHero)->HoldLocal(cTorch));
    CHECK(H(sHero)->HoldLocal(sTorch));
    pump3(40);
    CHECK(H(cHero)->Held() == cTorch && H(sHero)->Held() == sTorch);
    CHECK(srv->GetVar("srv.hold/" + heroId).empty());
    H(cHero)->ReleaseLocal(); H(sHero)->ReleaseLocal();
    pump3(5);
    CHECK(!H(cHero)->IsHolding() && !H(sHero)->IsHolding());

    // An RPC the anti-cheat refuses never reaches the server: the client's
    // pickup times out and is undone.
    srv->anticheat.settings.enabled = true;
    srv->anticheat.validateRpc = [](std::uint32_t, const std::string& name, const std::string&) { return name != "hold"; };
    CHECK(H(cHero)->Pickup(cCrate));
    pump3(150);
    CHECK(!H(sHero)->IsHolding());
    CHECK(!H(cHero)->IsHolding());

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
