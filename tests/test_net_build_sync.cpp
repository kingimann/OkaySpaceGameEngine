#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/NetBuildSync.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Build mode over the network (docs/multiplayer_audit.md round 4, docs/build_mode.md "Multiplayer"):
// a client's place / remove / move / link / unlink / undo are requests the server checks; what is built
// reaches every client and late joiners; hostile requests change nothing.

namespace {

const char* kBlock  = "test_nbs_block.okayprefab";
const char* kButton = "test_nbs_button.okayprefab";
const char* kLamp   = "test_nbs_lamp.okayprefab";
const char* kSecret = "test_nbs_secret.okayprefab";   // exists, but no palette lists it

template <class F>
void MakePrefab(const char* path, const char* name, const Vec3& scale, F fill) {
    Scene mk("mk");
    GameObject* g = mk.CreateGameObject(name);
    g->transform->localScale = scale;
    g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
    g->AddComponent<BoxCollider3D>();
    fill(g);
    SceneSerializer::SaveObjectToFile(*g, path);
}
void MakePrefabs() {
    MakePrefab(kBlock, "Block", {1, 1, 1}, [](GameObject*) {});
    MakePrefab(kSecret, "Secret", {1, 1, 1}, [](GameObject*) {});
    MakePrefab(kButton, "Button", {0.5f, 0.5f, 0.5f}, [](GameObject* g) { g->AddComponent<LogicSource>()->kind = LogicSource::Kind::Button; });
    MakePrefab(kLamp, "Lamp", {0.5f, 0.5f, 0.5f}, [](GameObject* g) {
        g->AddComponent<Light>()->intensity = 0.3f;
        auto* t = g->AddComponent<LogicTarget>();
        t->kind = LogicTarget::Kind::Light; t->lightIntensity = 2.0f;
    });
}
void RemovePrefabs() { for (const char* p : {kBlock, kButton, kLamp, kSecret}) std::remove(p); }

// Ground (top at y = 0) and a builder with the palette, in each peer's scene.
BuildMode* World(Scene& s) {
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {200, 1, 200};
    ground->AddComponent<BoxCollider3D>();
    ground->isStatic = true;
    auto* bm = s.CreateGameObject("Builder")->AddComponent<BuildMode>();
    bm->maxReach = 60.0f;
    bm->AddToPalette("Block", kBlock);    // 0
    bm->AddToPalette("Button", kButton);  // 1
    bm->AddToPalette("Lamp", kLamp);      // 2
    return bm;
}

struct Peer {
    Scene scene;
    NetworkManager* nm = nullptr;
    BuildMode* bm = nullptr;
    GameObject* avatar = nullptr;
    explicit Peer(const char* name) : scene(name) {
        nm = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->interpolationRate = 0.0f;
        bm = World(scene);
    }
};

struct Rig {
    std::unique_ptr<Peer> srv;
    std::vector<std::unique_ptr<Peer>> cli;
    Rig() {
        srv = std::make_unique<Peer>("Server");
        CHECK(srv->nm->StartServer(0));
        srv->scene.Start();
    }
    Peer& Add(bool avatarAtOrigin = false, const std::function<void(Peer&)>& setup = nullptr) {
        cli.push_back(std::make_unique<Peer>("Client"));
        Peer& p = *cli.back();
        if (setup) setup(p);   // the joiner's own copy of the level, before it joins
        if (avatarAtOrigin) {
            p.avatar = p.scene.CreateGameObject("Me");
            p.nm->SetLocalAvatar(p.avatar->transform);
        }
        CHECK(p.nm->StartClient("127.0.0.1", srv->nm->ServerPort()));
        p.scene.Start();
        CHECK(Until([&] { return p.nm->Joined(); }, 600));
        return p;
    }
    void Step(float dt = 0.02f) {
        srv->scene.Update(dt);
        for (auto& c : cli) c->scene.Update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    bool Until(const std::function<bool()>& done, int steps = 300) {
        for (int i = 0; i < steps && !done(); ++i) Step();
        return done();
    }
    void Pump(int n) { for (int i = 0; i < n; ++i) Step(); }
};

int Count(Scene& s) { return (int)BuildCreation::PlacedRoots(s).size(); }
GameObject* Built(Scene& s, int id) { Buildable* b = Buildable::Find(s, id); return b ? b->gameObject : nullptr; }
bool Near(const Vec3& a, const Vec3& b, float e = 1e-3f) { return Vec3::Distance(a, b) <= e; }
int FirstId(Scene& s) {
    for (GameObject* r : BuildCreation::PlacedRoots(s)) if (auto* b = r->GetComponent<Buildable>()) return b->buildId;
    return 0;
}
std::string Pose(float x, float y, float z) {
    char b[120]; std::snprintf(b, sizeof b, "%g|%g|%g|0|0|0|1", x, y, z); return b;
}
std::string PlaceLine(const std::string& prefab, float x, float y, float z) { return "P|" + prefab + "|" + Pose(x, y, z); }

} // namespace

int main() {
    RUN_SUITE("net_build_sync");
    MakePrefabs();
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Request channels: handled on the server, never relayed or queued ----
    {
        Rig w;
        Peer& a = w.Add();
        Peer& b = w.Add();
        std::vector<std::pair<std::uint32_t, std::string>> got;
        w.srv->nm->SetRequestHandler("__req/probe", [&](std::uint32_t from, const std::string& d) { got.emplace_back(from, d); });
        a.nm->Send("__req/probe", "u");
        a.nm->SendReliable("__req/probe", "r");
        a.nm->SendOrdered("__req/probe", "o");
        a.nm->SendSequenced("__req/probe", "s");
        a.nm->SendOrdered("__req/none", "x");       // nobody registered it
        a.nm->SendOrdered("__req/", "x");           // empty name
        w.Until([&] { return got.size() >= 4; }, 300);
        CHECK(got.size() == 4);
        bool fromA = true;
        for (auto& g : got) fromA &= g.first == a.nm->LocalId();
        CHECK(fromA);
        w.Pump(30);
        CHECK(!b.nm->HasMessages());                       // not relayed to the other client
        CHECK(!w.srv->nm->HasMessages());                  // not in the host's inbox either
        // The handler is the server's: registering needs the right prefix, and a client cannot reach __bld.
        w.srv->nm->SetRequestHandler("notreq", [&](std::uint32_t, const std::string&) { got.emplace_back(0, "bad"); });
        a.nm->SendOrdered("notreq", "x");
        a.nm->SendOrdered("__bld", "R|1");
        w.Pump(30);
        CHECK(got.size() == 4);
        got.clear();
        w.srv->nm->ClearRequestHandler("__req/probe");
        a.nm->SendOrdered("__req/probe", "late");
        w.Pump(30);
        CHECK(got.empty());
    }

    // ---- A client's placement is the server's, shows on every client, and a late joiner gets it ----
    {
        Rig w;
        Peer& a = w.Add();
        Peer& b = w.Add();
        const std::uint32_t aid = a.nm->LocalId();
        // Locally nothing appears: the request goes out and the server's answer builds it.
        CHECK(a.bm->PlaceItem(0, {3, 0.5f, 3}, Quat::Identity) == nullptr);
        CHECK(Count(a.scene) == 0);
        CHECK(w.Until([&] { return Count(w.srv->scene) == 1 && Count(a.scene) == 1 && Count(b.scene) == 1; }));
        const int id = FirstId(w.srv->scene);
        CHECK(id > 0);
        CHECK(FirstId(a.scene) == id && FirstId(b.scene) == id);
        CHECK(w.srv->nm->Build().OwnerOf(id) == (long long)aid);
        CHECK(Near(Built(b.scene, id)->transform->Position(), {3, 0.5f, 3}));
        CHECK(Built(b.scene, id)->GetComponent<Buildable>()->prefab == kBlock);
        CHECK(Built(b.scene, id)->GetComponent<Buildable>()->placed);

        // More: a button and a lamp, linked.
        a.bm->PlaceItem(1, {6, 0.25f, 3}, Quat::Identity);
        a.bm->PlaceItem(2, {8, 0.25f, 3}, Quat::Identity);
        CHECK(w.Until([&] { return Count(b.scene) == 3; }));
        int btn = 0, lamp = 0;
        for (GameObject* r : BuildCreation::PlacedRoots(w.srv->scene)) {
            auto* bl = r->GetComponent<Buildable>();
            if (bl->prefab == kButton) btn = bl->buildId; else if (bl->prefab == kLamp) lamp = bl->buildId;
        }
        CHECK(btn > 0 && lamp > 0);
        CHECK(a.bm->Link(Built(a.scene, btn), Built(a.scene, lamp)));   // a request
        CHECK(w.Until([&] { return !BuildMode::LinksOf(Built(b.scene, btn)).empty(); }));
        CHECK(BuildMode::LinksOf(Built(b.scene, btn)) == std::vector<int>{lamp});
        CHECK(BuildMode::LinksOf(Built(w.srv->scene, btn)) == std::vector<int>{lamp});

        // Move, remove: only its owner may.
        CHECK(a.bm->SetPose(Built(a.scene, id), {-4, 0.5f, 2}, Quat::Identity));
        CHECK(w.Until([&] { return Near(Built(b.scene, id)->transform->Position(), {-4, 0.5f, 2}); }));
        CHECK(Near(Built(w.srv->scene, id)->transform->Position(), {-4, 0.5f, 2}));

        // A late joiner is sent all of it: the three pieces, where they are, and the link.
        Peer& c = w.Add();
        CHECK(w.Until([&] { return Count(c.scene) == 3; }));
        CHECK(Near(Built(c.scene, id)->transform->Position(), {-4, 0.5f, 2}));
        CHECK(BuildMode::LinksOf(Built(c.scene, btn)) == std::vector<int>{lamp});
        CHECK(Built(c.scene, id)->GetComponent<Buildable>()->prefab == kBlock);
        CHECK(c.nm->Build().SnapshotsApplied() >= 1);

        // The other client cannot touch it.
        const int refused0 = w.srv->nm->Build().Refused();
        CHECK(b.bm->SetPose(Built(b.scene, id), {20, 0.5f, 20}, Quat::Identity));   // request sent
        CHECK(w.Until([&] { return w.srv->nm->Build().Refused() > refused0; }));
        w.Pump(20);
        CHECK(Near(Built(w.srv->scene, id)->transform->Position(), {-4, 0.5f, 2}));
        CHECK(Near(Built(b.scene, id)->transform->Position(), {-4, 0.5f, 2}));   // and its own copy went back
        CHECK(b.nm->Build().LastRefusal().find("isn't yours") != std::string::npos);
        CHECK(b.bm->Remove(Built(b.scene, id)));
        w.Pump(30);
        CHECK(Count(w.srv->scene) == 3);
        CHECK(w.srv->nm->Build().LastRefusal() == "it isn't yours");
        // The same requests succeed once the rule that stopped them is off (so each refusal above was that rule).
        w.srv->nm->Build().ownerOnly = false;
        CHECK(b.bm->SetPose(Built(b.scene, id), {12, 0.5f, 5}, Quat::Identity));
        CHECK(w.Until([&] { return Near(Built(w.srv->scene, id)->transform->Position(), {12, 0.5f, 5}); }));
        CHECK(w.Until([&] { return Near(Built(c.scene, id)->transform->Position(), {12, 0.5f, 5}); }));
        w.srv->nm->Build().ownerOnly = true;
        CHECK(a.bm->SetPose(Built(a.scene, id), {-4, 0.5f, 2}, Quat::Identity));
        CHECK(w.Until([&] { return Near(Built(c.scene, id)->transform->Position(), {-4, 0.5f, 2}); }));
        // The owner removes it.
        CHECK(a.bm->Remove(Built(a.scene, id)));
        CHECK(w.Until([&] { return Count(w.srv->scene) == 2 && Count(b.scene) == 2 && Count(c.scene) == 2; }));
        CHECK(w.srv->nm->Build().OwnerOf(id) < 0);
        // Unlink.
        CHECK(a.bm->Unlink(Built(a.scene, btn), Built(a.scene, lamp)));
        CHECK(w.Until([&] { return BuildMode::LinksOf(Built(c.scene, btn)).empty(); }));
    }

    // ---- Undo is per player and goes through the same rules ----
    {
        Rig w;
        Peer& a = w.Add();
        Peer& b = w.Add();
        a.bm->PlaceItem(0, {0, 0.5f, 0}, Quat::Identity);
        CHECK(w.Until([&] { return Count(b.scene) == 1; }));
        const int id = FirstId(w.srv->scene);
        b.bm->PlaceItem(0, {5, 0.5f, 0}, Quat::Identity);
        CHECK(w.Until([&] { return Count(a.scene) == 2; }));
        CHECK(w.srv->nm->Build().HistorySize(a.nm->LocalId()) == 1);
        // Move a's piece, then undo: it goes back. Undo again: the piece is removed. a never undoes b's.
        a.bm->SetPose(Built(a.scene, id), {0, 0.5f, 6}, Quat::Identity);
        CHECK(w.Until([&] { return Near(Built(b.scene, id)->transform->Position(), {0, 0.5f, 6}); }));
        CHECK(a.bm->Undo());
        CHECK(w.Until([&] { return Near(Built(b.scene, id)->transform->Position(), {0, 0.5f, 0}); }));
        CHECK(a.bm->Undo());
        CHECK(w.Until([&] { return Count(b.scene) == 1 && Count(w.srv->scene) == 1; }));
        CHECK(Built(w.srv->scene, id) == nullptr);
        CHECK(a.bm->Undo());               // nothing left of a's: refused, b's piece stays
        w.Pump(30);
        CHECK(Count(w.srv->scene) == 1);
        CHECK(w.srv->nm->Build().LastRefusal() == "nothing to undo");
        CHECK(a.nm->Build().LastRefusal() == "nothing to undo");
        // b removes its own piece and undoes that: it is back with its id and everyone has it.
        const int bid = FirstId(w.srv->scene);
        CHECK(b.bm->Remove(Built(b.scene, bid)));
        CHECK(w.Until([&] { return Count(w.srv->scene) == 0 && Count(a.scene) == 0; }));
        CHECK(b.bm->Undo());
        CHECK(w.Until([&] { return Count(w.srv->scene) == 1 && Count(a.scene) == 1; }));
        CHECK(FirstId(a.scene) == bid);
        CHECK(w.srv->nm->Build().OwnerOf(bid) == (long long)b.nm->LocalId());
        // Redo is not offered to a client.
        CHECK(!a.bm->Redo());
        // A client's creation load and duplicate do nothing.
        std::string err;
        CHECK(!a.bm->LoadCreation("nope", &err));
        CHECK(a.bm->Duplicate(Built(a.scene, bid)) == nullptr);
    }

    // ---- The host's own building reaches the clients, and a loaded creation resets them ----
    {
        Rig w;
        Peer& a = w.Add();
        GameObject* mine = w.srv->bm->PlaceItem(0, {1, 0.5f, 1}, Quat::Identity);
        CHECK(mine != nullptr);
        CHECK(w.Until([&] { return Count(a.scene) == 1; }));
        const int id = FirstId(a.scene);
        CHECK(w.srv->nm->Build().OwnerOf(id) == 0);
        // The host moves and links and removes freely, even what a client placed.
        a.bm->PlaceItem(1, {6, 0.25f, 3}, Quat::Identity);
        a.bm->PlaceItem(2, {8, 0.25f, 3}, Quat::Identity);
        CHECK(w.Until([&] { return Count(a.scene) == 3; }));
        CHECK(w.srv->bm->SetPose(mine, {9, 0.5f, 9}, Quat::Identity));
        CHECK(w.Until([&] { return Near(Built(a.scene, id)->transform->Position(), {9, 0.5f, 9}); }));
        // ...and a creation the host loads replaces what everyone sees.
        std::string err;
        CHECK(w.srv->bm->SaveCreation("nbs_test.okaycreation", &err));
        w.srv->bm->ClearCreation();
        CHECK(w.Until([&] { return Count(a.scene) == 0; }));
        CHECK(w.srv->bm->LoadCreation("nbs_test.okaycreation", &err));
        CHECK(w.Until([&] { return Count(a.scene) == 3; }));
        std::remove(BuildMode::CreationPath("nbs_test.okaycreation").c_str());
    }

    // ---- Hostile requests change nothing ----
    {
        Rig w;
        w.srv->nm->Build().maxRequestsPerSecond = 1000;   // the rate limit has its own case
        Peer& a = w.Add(/*avatar*/ true);
        Peer& b = w.Add();
        auto& bs = w.srv->nm->Build();
        auto send = [&](const std::string& line) { a.nm->SendOrdered("__req/build", line); };
        auto settle = [&] { w.Pump(25); };
        // Control: a valid one works, so the rest are refused for their own reasons.
        send(PlaceLine(kBlock, 3, 0.5f, 3));
        CHECK(w.Until([&] { return Count(w.srv->scene) == 1; }));
        const int id = FirstId(w.srv->scene);
        int accepted = 0;
        {   // the allowlist is what refuses the prefab no palette lists: listing it makes the same request work
            send(PlaceLine(kSecret, 5, 0.5f, 5));
            w.Pump(25);
            CHECK(Count(w.srv->scene) == 1);
            bs.extraPrefabs = {kSecret};
            send(PlaceLine(kSecret, 5, 0.5f, 5));
            CHECK(w.Until([&] { return Count(w.srv->scene) == 2; }));
            bs.extraPrefabs.clear();
            GameObject* extra = nullptr;
            for (GameObject* r : BuildCreation::PlacedRoots(w.srv->scene)) if (r->GetComponent<Buildable>()->prefab == kSecret) extra = r;
            CHECK(extra != nullptr);
            if (extra) { send("R|" + std::to_string(extra->GetComponent<Buildable>()->buildId)); CHECK(w.Until([&] { return Count(w.srv->scene) == 1; })); }
            accepted = bs.Accepted();
        }

        struct Case { const char* what; std::string line; };
        const std::vector<Case> bad = {
            {"prefab no palette lists", PlaceLine(kSecret, 5, 0.5f, 5)},
            {"path traversal", PlaceLine("../test_nbs_block.okayprefab", 5, 0.5f, 5)},
            {"absolute path", PlaceLine("/etc/passwd", 5, 0.5f, 5)},
            {"windows drive path", PlaceLine("C:\\x.okayprefab", 5, 0.5f, 5)},
            {"empty prefab", PlaceLine("", 5, 0.5f, 5)},
            {"prefab with a newline", "P|test_nbs_block.okayprefab\nR|" + std::to_string(id) + "|" + Pose(5, 0.5f, 5)},
            {"outside the bounds", PlaceLine(kBlock, 5000, 0.5f, 5)},
            {"nan position", PlaceLine(kBlock, NAN, 0.5f, 5)},
            {"inf position", "P|" + std::string(kBlock) + "|inf|0.5|5|0|0|0|1"},
            {"text for a number", "P|" + std::string(kBlock) + "|a|0.5|5|0|0|0|1"},
            {"huge number", PlaceLine(kBlock, 3e38f, 0.5f, 5)},
            {"zero rotation", "P|" + std::string(kBlock) + "|5|0.5|5|0|0|0|0"},
            {"huge rotation", "P|" + std::string(kBlock) + "|5|0.5|5|0|0|0|50"},
            {"too few fields", "P|" + std::string(kBlock) + "|5|0.5|5"},
            {"too many fields", PlaceLine(kBlock, 5, 0.5f, 5) + "|1|2"},
            {"id zero", "R|0"},
            {"negative id", "R|-4"},
            {"id with letters", "R|1x"},
            {"huge id", "R|99999999999999999999"},
            {"missing object", "R|424242"},
            {"move a missing object", "M|424242|" + Pose(1, 1, 1)},
            {"link missing objects", "L|424242|424243"},
            {"link an object to nothing", "L|" + std::to_string(id)},
            {"unknown op", "Q|1|2|3"},
            {"empty", ""},
            {"just a bar", "|"},
            {"undo with an argument", "Z|1"},
            {"oversized", std::string(5000, 'P')},
        };
        std::vector<std::pair<std::string, bool>> results;
        for (const Case& c : bad) {
            const int before = Count(w.srv->scene);
            send(c.line);
            settle();
            const bool unchanged = Count(w.srv->scene) == before && bs.Accepted() == accepted;
            if (!unchanged) std::fprintf(stderr, "  hostile request changed the world: %s\n", c.what);
            CHECK(unchanged);
        }
        CHECK(Count(w.srv->scene) == 1);
        CHECK(Near(Built(w.srv->scene, id)->transform->Position(), {3, 0.5f, 3}));
        CHECK(bs.Refused() > 0 && bs.Malformed() > 0);
        // A well-formed request still works afterwards (the server is not wedged).
        send(PlaceLine(kBlock, -3, 0.5f, 3));
        CHECK(w.Until([&] { return Count(w.srv->scene) == 2; }));

        // A client cannot say what was built: "__bld" is the server's.
        b.nm->SendOrdered("__bld", "R|" + std::to_string(id));
        b.nm->SendReliable("__bld", "R|" + std::to_string(id));
        b.nm->Send("__bld", "R|" + std::to_string(id));
        w.Pump(30);
        CHECK(Count(w.srv->scene) == 2 && Count(a.scene) == 2 && Count(b.scene) == 2);
        // Nor can it write through the unreliable paths.
        a.nm->Send("__req/build", PlaceLine(kBlock, 7, 0.5f, 7));
        a.nm->SendSequenced("__req/build", PlaceLine(kBlock, 9, 0.5f, 7));
        w.Until([&] { return Count(w.srv->scene) >= 4; }, 100);   // these are valid requests on valid channels: they work
        CHECK(Count(w.srv->scene) == 4);
        CHECK(!b.nm->HasMessages());
    }

    // ---- Caps: world, per player, bounds, reach, overlap, rate ----
    {
        Rig w;
        auto& bs = w.srv->nm->Build();
        bs.maxRequestsPerSecond = 1000;
        bs.maxObjects = 3;
        Peer& a = w.Add(/*avatar*/ true);   // at the origin
        for (int i = 0; i < 6; ++i) a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 2.0f * (float)i + 2, 0.5f, 2));
        w.Pump(60);
        CHECK(Count(w.srv->scene) == 3);                       // the cap, not a pile of six
        CHECK(bs.LastRefusal().find("limit reached") != std::string::npos);
        bs.maxObjects = 100;
        bs.maxPerPlayer = 3;
        a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 2, 0.5f, 6));
        w.Pump(30);
        CHECK(Count(w.srv->scene) == 3);
        CHECK(bs.LastRefusal().find("most you may") != std::string::npos);
        bs.maxPerPlayer = 0;
        // Reach: the avatar is at the origin; 100 units away is out of reach, 10 is in.
        a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 100, 0.5f, 0));
        w.Pump(30);
        CHECK(Count(w.srv->scene) == 3);
        CHECK(bs.LastRefusal() == "too far away");
        a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 10, 0.5f, 10));
        CHECK(w.Until([&] { return Count(w.srv->scene) == 4; }));
        // Bounds the game sets.
        bs.boundsMax = Vec3(5, 50, 5);
        a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 8, 0.5f, 8));
        w.Pump(30);
        CHECK(Count(w.srv->scene) == 4);
        CHECK(bs.LastRefusal() == "outside the build area");
        bs.boundsMax = Vec3(1000, 1000, 1000);
        // Overlap: a second block in the same spot is blocked; the check can be turned off.
        a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 10, 0.5f, 10));
        w.Pump(30);
        CHECK(Count(w.srv->scene) == 4);
        CHECK(bs.LastRefusal() == "blocked");
        bs.checkBlocked = false;
        a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 10, 0.5f, 10));
        CHECK(w.Until([&] { return Count(w.srv->scene) == 5; }));
        bs.checkBlocked = true;
        // Moving into another piece is blocked too; its own place does not count against it.
        const int id = FirstId(w.srv->scene);
        const Vec3 at = Built(w.srv->scene, id)->transform->Position();
        a.nm->SendOrdered("__req/build", "M|" + std::to_string(id) + "|" + Pose(at.x + 0.1f, at.y, at.z));
        w.Pump(25);
        CHECK(Near(Built(w.srv->scene, id)->transform->Position(), {at.x + 0.1f, at.y, at.z}));   // a small slide: nothing else in the way
    }
    {   // The request rate: a burst is cut off, then it recovers.
        Rig w;
        auto& bs = w.srv->nm->Build();
        bs.maxRequestsPerSecond = 5;      // a burst of 10
        bs.maxObjects = 1000;
        bs.checkBlocked = false;
        bs.maxReach = 0.0f;      // the pieces are spread wide: this case is about the rate
        Peer& a = w.Add();
        for (int i = 0; i < 80; ++i) a.nm->SendOrdered("__req/build", PlaceLine(kBlock, (float)(i % 40) - 20, 0.5f + (float)(i / 40) * 2, 0));
        w.Pump(40);
        CHECK(Count(w.srv->scene) <= 14);
        CHECK(Count(w.srv->scene) >= 8);
        CHECK(bs.Refused() >= 50);
        const int n = Count(w.srv->scene);
        w.Pump(120);   // 2.4 s: tokens are back
        a.nm->SendOrdered("__req/build", PlaceLine(kBlock, 25, 0.5f, 5));
        CHECK(w.Until([&] { return Count(w.srv->scene) == n + 1; }));
    }

    // ---- The level's own objects are the level's, unless the game says so ----
    {
        Rig w;
        auto addLevelProp = [](Scene& s) {
            GameObject* g = s.CreateGameObject("Crate");
            g->transform->localPosition = {0, 0.5f, 8};
            g->AddComponent<BoxCollider3D>();
            auto* bl = g->AddComponent<Buildable>();
            bl->buildId = 777;
        };
        addLevelProp(w.srv->scene);
        Peer& a = w.Add();
        addLevelProp(a.scene);
        a.nm->SendOrdered("__req/build", "R|777");
        a.nm->SendOrdered("__req/build", "M|777|" + Pose(0, 0.5f, 12));
        w.Pump(40);
        CHECK(Built(w.srv->scene, 777) != nullptr);
        CHECK(Near(Built(w.srv->scene, 777)->transform->Position(), {0, 0.5f, 8}));
        CHECK(w.srv->nm->Build().LastRefusal() == "it belongs to the level");
        w.srv->nm->Build().levelEditable = true;
        a.nm->SendOrdered("__req/build", "M|777|" + Pose(0, 0.5f, 12));
        w.Pump(40);
        CHECK(w.Until([&] { return Near(Built(w.srv->scene, 777)->transform->Position(), {0, 0.5f, 12}); }));
        CHECK(w.Until([&] { return Near(Built(a.scene, 777)->transform->Position(), {0, 0.5f, 12}); }));
        // A late joiner gets the moved level prop, and the removal of another.
        a.nm->SendOrdered("__req/build", "R|777");
        CHECK(w.Until([&] { return Built(w.srv->scene, 777) == nullptr; }));
        Peer& c = w.Add(false, [&](Peer& p) { addLevelProp(p.scene); });
        CHECK(w.Until([&] { return Built(c.scene, 777) == nullptr; }));
    }

    // ---- A client survives whatever the server sends ----
    {
        Rig w;
        Peer& a = w.Add();
        auto& cs = a.nm->Build();
        const std::uint32_t id = a.nm->LocalId();
        // One valid piece, so the client has the executor; then garbage.
        a.bm->PlaceItem(0, {0, 0.5f, 0}, Quat::Identity);
        CHECK(w.Until([&] { return Count(a.scene) == 1; }));
        const std::vector<std::string> junk = {
            "", "\n\n\n", "P", "P|1", "P|-1|0|x.okayprefab|0|0|0|0|0|0|1|", "P|5|0|../../etc/passwd|0|0|0|0|0|0|1|",
            "P|5|0|" + std::string(kSecret) + "|nan|0|0|0|0|0|1|", "P|5|0|" + std::string(kSecret) + "|0|0|0|0|0|0|0|",
            "P|5|0|" + std::string(kSecret) + "|0|0|0|0|0|0|1|1,2,x", "P|99999999999|0|a|0|0|0|0|0|0|1|",
            "R|0", "R|abc", "M|1|1", "L|1", "U|a|b", "E", "X", std::string("X|") + std::string(5000, 'x'),
            std::string(10, '\0'), "Q|1|1|1", std::string(40000, 'P'),
        };
        for (const std::string& j : junk) w.srv->nm->SendOrderedTo(id, "__bld", j);
        w.Pump(60);
        CHECK(Count(a.scene) == 1);                // nothing built, nothing lost
        CHECK(cs.Malformed() > 0);
        // And a client never takes a "__bld" from a peer other than the server.
        Peer& b = w.Add();
        b.nm->SendOrderedTo(a.nm->LocalId(), "__bld", "R|1");
        b.nm->Send("__bld", "R|1");
        w.Pump(40);
        CHECK(Count(a.scene) == 1);
        // A piece a snapshot lists is created; one it leaves out is removed (reconcile).
        w.srv->nm->SendOrderedTo(id, "__bld", "B\nE\n");
        CHECK(w.Until([&] { return Count(a.scene) == 0; }));
    }

    // ---- With the session over, build mode is local again ----
    {
        Rig w;
        Peer& a = w.Add();
        CHECK(a.bm->remoteRequest != nullptr);
        a.nm->Stop();
        w.Pump(60);
        CHECK(a.bm->remoteRequest == nullptr);
        GameObject* g = a.bm->PlaceItem(0, {0, 0.5f, 0}, Quat::Identity);
        CHECK(g != nullptr);
        CHECK(Count(a.scene) == 1);
    }

    RemovePrefabs();
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
