#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Net/NetBuildSync.hpp"
#include "okay/Net/NetQuestSync.hpp"
#include "okay/Net/NetSim.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// The messages round 4 added (build requests and their answers, quest and story state, life events)
// over a link that drops, delays, reorders and duplicates datagrams (NetSim): every client ends up with
// the server's state, nothing is applied twice, a state never goes backwards, and a join in the middle
// of it all still converges.

namespace {

const char* kBlock = "test_nbl_block.okayprefab";

struct Peer {
    Scene scene;
    NetworkManager* nm = nullptr;
    BuildMode* bm = nullptr;
    QuestLog* log = nullptr;
    StoryState* story = nullptr;
    GameObject* hero = nullptr;
    HealthStat* hs = nullptr;
    explicit Peer(const char* name, bool server) : scene(name) {
        nm = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->interpolationRate = 0.0f;
        GameObject* ground = scene.CreateGameObject("Ground");
        ground->transform->localPosition = {0, -0.5f, 0};
        ground->transform->localScale = {400, 1, 400};
        ground->AddComponent<BoxCollider3D>();
        ground->isStatic = true;
        bm = scene.CreateGameObject("Builder")->AddComponent<BuildMode>();
        bm->AddToPalette("Block", kBlock);
        log = QuestLog::Get(&scene, true);
        log->showNotifications = false; log->drawUI = false; log->allowJournal = false;
        QuestDef q; q.id = "q"; q.title = "q";
        QuestObjective o; o.kind = QuestObjective::Kind::Custom; o.count = 1000; q.objectives.push_back(o);
        log->quests = {q};
        story = StoryState::Get(&scene, true);
        hero = scene.CreateGameObject("hero");
        auto* ns = hero->AddComponent<NetworkSync>();
        ns->netId = "hero"; ns->authority = NetworkSync::Authority::Host;
        hs = hero->AddComponent<HealthStat>();
        hs->maxHealth = hs->health = 100.0f;
        hs->respawn = server; hs->respawnDelay = 3.0f; hs->playDeathAnim = false;
    }
};

struct Rig {
    std::unique_ptr<Peer> srv;
    std::vector<std::unique_ptr<Peer>> cli;
    Rig() {
        srv = std::make_unique<Peer>("Server", true);
        srv->nm->Build().maxRequestsPerSecond = 1000;
        srv->nm->Build().checkBlocked = false;
        srv->nm->Build().maxReach = 0.0f;   // pieces are spread over a wide area
        CHECK(srv->nm->StartServer(0));
        srv->scene.Start();
    }
    Peer& Add(bool wait = true) {
        cli.push_back(std::make_unique<Peer>("Client", false));
        Peer& p = *cli.back();
        CHECK(p.nm->StartClient("127.0.0.1", srv->nm->ServerPort()));
        p.scene.Start();
        if (wait) CHECK(Until([&] { return p.nm->Joined(); }, 3000));
        return p;
    }
    void Step(float dt = 0.02f) {
        srv->scene.Update(dt);
        for (auto& c : cli) c->scene.Update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    bool Until(const std::function<bool()>& done, int steps = 1500) {
        for (int i = 0; i < steps && !done(); ++i) Step();
        return done();
    }
    void Pump(int n) { for (int i = 0; i < n; ++i) Step(); }
};

// What the placed objects are, as id -> position text, so two scenes can be compared.
std::map<int, std::string> Built(Scene& s) {
    std::map<int, std::string> m;
    for (GameObject* r : BuildCreation::PlacedRoots(s)) {
        auto* b = r->GetComponent<Buildable>();
        char buf[100];
        const Vec3 p = r->transform->Position();
        std::snprintf(buf, sizeof buf, "%.2f,%.2f,%.2f", p.x, p.y, p.z);
        m[b->buildId] = std::string(buf) + "/" + std::to_string(BuildMode::LinksOf(r).size());
    }
    return m;
}
std::string PlaceLine(float x, float y, float z) {
    char b[160]; std::snprintf(b, sizeof b, "P|%s|%g|%g|%g|0|0|0|1", kBlock, x, y, z); return b;
}

} // namespace

int main() {
    RUN_SUITE("net_build_quest_lossy");
    {
        Scene mk("mk");
        GameObject* g = mk.CreateGameObject("Block");
        g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        g->AddComponent<BoxCollider3D>();
        SceneSerializer::SaveObjectToFile(*g, kBlock);
    }
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    auto& sim = net::Simulator();
    sim.enabled = true; sim.latencyMs = 30.0f; sim.jitterMs = 25.0f; sim.loss = 0.2f; sim.duplicate = 0.15f;

    // ---- Build: many requests through loss, reorder and duplicates; every client equals the server ----
    {
        Rig w;
        Peer& a = w.Add();
        Peer& b = w.Add();
        const int kPieces = 24;
        for (int i = 0; i < kPieces; ++i) a.nm->SendOrdered("__req/build", PlaceLine((float)(i % 8) * 3 - 12, 0.5f, (float)(i / 8) * 3));
        CHECK(w.Until([&] { return (int)Built(w.srv->scene).size() == kPieces && Built(a.scene) == Built(w.srv->scene) && Built(b.scene) == Built(w.srv->scene); }, 3000));
        CHECK((int)Built(w.srv->scene).size() == kPieces);        // none twice (duplicated datagrams), none missing
        std::set<int> ids;
        for (auto& kv : Built(w.srv->scene)) ids.insert(kv.first);
        CHECK((int)ids.size() == kPieces);
        // Moves and removals in order: the last word wins, and a removed piece stays removed.
        const std::vector<int> v(ids.begin(), ids.end());
        for (int round = 0; round < 6; ++round)
            for (int i = 0; i < 4; ++i)
                a.nm->SendOrdered("__req/build", "M|" + std::to_string(v[i]) + "|" + std::to_string(round) + "|0.5|" + std::to_string(40 + i) + "|0|0|0|1");
        a.nm->SendOrdered("__req/build", "R|" + std::to_string(v[5]));
        a.nm->SendOrdered("__req/build", "M|" + std::to_string(v[5]) + "|1|0.5|60|0|0|0|1");   // after its removal: refused, not resurrected
        CHECK(w.Until([&] { return (int)Built(w.srv->scene).size() == kPieces - 1 && Built(a.scene) == Built(w.srv->scene) && Built(b.scene) == Built(w.srv->scene); }, 3000));
        CHECK(Built(b.scene).count(v[5]) == 0);
        CHECK(Built(b.scene)[v[0]].rfind("5.00,0.50,40.00", 0) == 0);   // the last move of the six
        // A join in the middle of changes ends with the same state.
        Peer& c = w.Add(/*wait*/ false);
        for (int i = 0; i < 10; ++i) a.nm->SendOrdered("__req/build", PlaceLine((float)i * 3 - 12, 0.5f, 30));
        CHECK(w.Until([&] { return c.nm->Joined() && (int)Built(w.srv->scene).size() == kPieces - 1 + 10 && Built(c.scene) == Built(w.srv->scene) && Built(a.scene) == Built(w.srv->scene); }, 4000));
        CHECK(w.srv->nm->Build().Malformed() == 0);
    }

    // ---- Quests and story: a state never goes backwards and the end state matches ----
    {
        Rig w;
        auto& qs = w.srv->nm->Quests();
        qs.perPlayerVars = {"dlg."};
        qs.perPlayerQuests = {};
        Peer& a = w.Add();
        Peer& b = w.Add();
        CHECK(w.srv->log->StartQuest("q"));
        int lastA = 0, lastB = 0;
        bool monotonic = true;
        for (int i = 1; i <= 40; ++i) {
            CHECK(w.srv->log->AddProgress("q", 0, 1));
            w.srv->story->Set("counter", vs::VsValue((float)i));
            a.nm->SendOrdered("__req/quest", "V|dlg.mine|n" + std::to_string(i));
            w.Step(); w.Step();
            const int pa = a.log->Progress("q", 0), pb = b.log->Progress("q", 0);
            monotonic &= pa >= lastA && pb >= lastB;
            lastA = pa; lastB = pb;
            const float ca = a.story->Get("counter").AsFloat(), cb = b.story->Get("counter").AsFloat();
            monotonic &= ca >= 0.0f && cb >= 0.0f;
        }
        CHECK(monotonic);
        CHECK(w.Until([&] { return a.log->Progress("q", 0) == 40 && b.log->Progress("q", 0) == 40 &&
                                   a.story->Get("counter").AsFloat() == 40.0f && b.story->Get("counter").AsFloat() == 40.0f; }, 3000));
        CHECK(w.Until([&] { return a.story->Get("dlg.mine").AsFloat() == 40.0f; }, 3000));   // its own variable: last write wins
        CHECK(!b.story->Has("dlg.mine"));
        // Complete it, flip a flag on and off: the last state arrives; a joiner mid-flight converges.
        Peer& c = w.Add(false);
        for (int i = 0; i < 30; ++i) { w.srv->story->SetFlag("blink", i % 2 == 0); w.Step(); }
        w.srv->story->SetFlag("blink", true);
        CHECK(w.srv->log->CompleteObjective("q", 0));
        const bool converged = w.Until([&] { return c.nm->Joined() && c.log->Status("q") == QuestStatus::Completed && c.story->HasFlag("blink") &&
                                   a.log->Status("q") == QuestStatus::Completed && b.story->HasFlag("blink"); }, 4000);
        CHECK(converged);
        if (!converged)
            std::fprintf(stderr, "  (c joined %d, c quest %d, c blink %d, a quest %d, b blink %d, c snapshots %d, c malformed %d, srv blink %d)\n", c.nm->Joined(),
                         (int)c.log->Status("q"), c.story->HasFlag("blink"), (int)a.log->Status("q"), b.story->HasFlag("blink"), c.nm->Quests().SnapshotsApplied(),
                         c.nm->Quests().Malformed(), w.srv->story->HasFlag("blink"));
        CHECK(c.story->Get("counter").AsFloat() == 40.0f);
        CHECK(qs.Malformed() == 0 && c.nm->Quests().Malformed() == 0);
    }

    // ---- Life: death and respawn in quick succession and a join between them end consistent ----
    {
        Rig w;
        Peer& a = w.Add();
        for (int round = 0; round < 4; ++round) {
            w.srv->hs->KillFromAction(false, Vec3{});
            CHECK(w.Until([&] { return a.hs->IsDead(); }, 3000));
            CHECK(w.Until([&] { return !w.srv->hs->IsDead() && !a.hs->IsDead(); }, 3000));   // the server's respawn follows
        }
        CHECK(a.hs->health > 0.0f);
        // A joiner while it is dead, through the lossy link: it must not stay alive, and must come back with it.
        w.srv->hs->KillFromAction(false, Vec3{});
        Peer& b = w.Add();
        CHECK(w.Until([&] { return b.hs->IsDead() || !w.srv->hs->IsDead(); }, 3000));
        CHECK(w.Until([&] { return !w.srv->hs->IsDead() && !b.hs->IsDead() && !a.hs->IsDead() && b.hs->health > 0.0f; }, 3000));
    }

    sim.enabled = false; sim.loss = 0.0f; sim.duplicate = 0.0f;
    std::remove(kBlock);
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
