#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Net/NetQuestSync.hpp"
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Quest state and story variables over the network (docs/multiplayer_audit.md round 4, item 2): the
// server is the truth, shared and per-player scope, late joiners get everything, a client's writes are
// requests the server judges, hostile input changes nothing.

namespace {

QuestDef Quest(const std::string& id, int objectives, int count, std::vector<std::string> pre = {}) {
    QuestDef q;
    q.id = id; q.title = id; q.prerequisites = std::move(pre);
    for (int i = 0; i < objectives; ++i) {
        QuestObjective o;
        o.kind = QuestObjective::Kind::Custom; o.description = "do " + std::to_string(i); o.count = count;
        q.objectives.push_back(o);
    }
    return q;
}

struct Peer {
    Scene scene;
    NetworkManager* nm = nullptr;
    QuestLog* log = nullptr;
    StoryState* story = nullptr;
    explicit Peer(const char* name) : scene(name) {
        scene.physicsEnabled = false;
        nm = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->interpolationRate = 0.0f;
        log = QuestLog::Get(&scene, true);
        log->showNotifications = false; log->drawUI = false; log->allowJournal = false;
        log->quests = {Quest("shared", 2, 1), Quest("second", 1, 3, {"shared"}), Quest("mine", 1, 3), Quest("mine2", 1, 1, {"shared"})};
        story = StoryState::Get(&scene, true);
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
    Peer& Add() {
        cli.push_back(std::make_unique<Peer>("Client"));
        Peer& p = *cli.back();
        CHECK(p.nm->StartClient("127.0.0.1", srv->nm->ServerPort()));
        p.scene.Start();
        CHECK(Until([&] { return p.nm->Joined() && p.nm->Quests().SnapshotsApplied() >= 1; }, 800));
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

} // namespace

int main() {
    RUN_SUITE("net_quest_sync");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Shared quests: the server decides, every client sees it, a late joiner gets it ----
    {
        Rig w;
        Peer& a = w.Add();
        CHECK(a.log->remoteAuthority);
        CHECK(w.srv->log->StartQuest("shared"));
        CHECK(w.Until([&] { return a.log->Status("shared") == QuestStatus::Active; }));
        CHECK(w.srv->log->AddProgress("shared", 0, 1));
        CHECK(w.Until([&] { return a.log->ObjectiveDone("shared", 0); }));
        CHECK(!a.log->ObjectiveDone("shared", 1));
        // A client's own calls change nothing, whatever source fires them.
        CHECK(!a.log->CompleteQuest("shared"));
        CHECK(!a.log->StartQuest("second"));
        CHECK(!a.log->AddProgress("shared", 1, 1));
        CHECK(!a.log->CompleteObjective("shared", 1));
        a.log->NotifyReach("anything", nullptr);
        a.log->NotifyDialogueEnd("npc", "g");
        a.log->FailQuest("shared");
        a.log->ResetQuest("shared");
        w.Pump(20);
        CHECK(a.log->Status("shared") == QuestStatus::Active);
        CHECK(a.log->ObjectiveDone("shared", 0) && !a.log->ObjectiveDone("shared", 1));
        CHECK(w.srv->log->Status("shared") == QuestStatus::Active);
        // The server finishes it.
        CHECK(w.srv->log->CompleteObjective("shared", 1));
        CHECK(w.Until([&] { return a.log->Status("shared") == QuestStatus::Completed; }));
        CHECK(a.log->Record("shared")->completions == 1);
        CHECK(w.srv->log->StartQuest("second"));
        CHECK(w.srv->log->AddProgress("second", 0, 2));
        // A joiner after the fact has all of it.
        Peer& b = w.Add();
        CHECK(b.log->Status("shared") == QuestStatus::Completed);
        CHECK(b.log->Status("second") == QuestStatus::Active);
        CHECK(b.log->Progress("second", 0) == 2);
        CHECK(b.log->remoteAuthority);
        // Progress the joiner sees goes on from there.
        CHECK(w.srv->log->AddProgress("second", 0, 1));
        CHECK(w.Until([&] { return b.log->Status("second") == QuestStatus::Completed && a.log->Status("second") == QuestStatus::Completed; }));
        // A reset on the server removes the record everywhere.
        w.srv->log->ResetQuest("second");
        CHECK(w.Until([&] { return !a.log->Record("second") && !b.log->Record("second"); }));
    }

    // ---- Story variables and flags: server writes go out, client writes are requests ----
    {
        Rig w;
        auto& qs = w.srv->nm->Quests();
        Peer& a = w.Add();
        w.srv->story->Set("score", vs::VsValue(5.0f));
        w.srv->story->Set("title", vs::VsValue("hello|%\nworld"));   // text with the separators in it
        w.srv->story->Set("open", vs::VsValue(true));
        w.srv->story->SetFlag("door");
        CHECK(w.Until([&] { return a.story->Has("score") && a.story->Has("title") && a.story->HasFlag("door") && a.story->Has("open"); }));
        CHECK(a.story->Get("score").AsFloat() == 5.0f);
        CHECK(a.story->Get("title").AsString() == "hello|%\nworld");
        CHECK(a.story->Get("open").AsBool());
        // The client cannot change the server's variables: its write is a request and the server says no.
        a.story->Set("score", vs::VsValue(99.0f));
        a.story->SetFlag("door", false);
        a.story->Erase("title");
        a.story->SetFlag("hacked");
        CHECK(a.story->Get("score").AsFloat() == 5.0f);          // not even locally, until the server says
        CHECK(a.story->HasFlag("door") && a.story->Has("title") && !a.story->HasFlag("hacked"));
        w.Pump(40);
        CHECK(w.srv->story->Get("score").AsFloat() == 5.0f);
        CHECK(w.srv->story->HasFlag("door") && w.srv->story->Has("title") && !w.srv->story->HasFlag("hacked"));
        CHECK(qs.RequestsRefused() >= 4);
        CHECK(qs.LastRefusal() == "the server owns that variable");
        CHECK(a.nm->Quests().LastRefusal() == "the server owns that variable");
        // A name the game lets clients write goes through and reaches everyone.
        qs.clientWritableVars = {"chat."};
        Peer& b = w.Add();
        a.story->Set("chat.last", vs::VsValue("hi"));
        CHECK(w.Until([&] { return w.srv->story->Has("chat.last") && b.story->Has("chat.last") && a.story->Has("chat.last"); }));
        CHECK(b.story->Get("chat.last").AsString() == "hi");
        a.story->Erase("chat.last");
        CHECK(w.Until([&] { return !w.srv->story->Has("chat.last") && !b.story->Has("chat.last"); }));
        // A late joiner has them all, and what the server later removes goes away.
        Peer& c = w.Add();
        CHECK(c.story->Get("score").AsFloat() == 5.0f && c.story->HasFlag("door"));
        w.srv->story->Erase("score");
        w.srv->story->SetFlag("door", false);
        CHECK(w.Until([&] { return !c.story->Has("score") && !c.story->HasFlag("door") && !a.story->Has("score"); }));
        // Types that do not travel stay local on a client (they are not the story's business).
        a.story->Set("pos", vs::VsValue(Vec3(1, 2, 3)));
        CHECK(a.story->Has("pos"));
        w.Pump(20);
        CHECK(!w.srv->story->Has("pos"));
    }

    // ---- Per-player: each player's own quests and variables, told to that player only ----
    {
        Rig w;
        auto& qs = w.srv->nm->Quests();
        qs.perPlayerQuests = {"mine", "mine2"};
        qs.perPlayerVars = {"dlg."};
        std::vector<std::string> events;
        qs.onPlayerQuest = [&](std::uint32_t peer, const std::string& id, QuestStatus st) {
            events.push_back(std::to_string(peer) + ":" + id + ":" + QuestStatusName(st));
        };
        Peer& a = w.Add();
        Peer& b = w.Add();
        const std::uint32_t ia = a.nm->LocalId(), ib = b.nm->LocalId();
        CHECK(ia != ib);
        // A shared prerequisite gates a per-player quest, as for the log itself.
        CHECK(!qs.StartQuest(ia, "mine2"));
        CHECK(qs.StartQuest(ia, "mine"));
        CHECK(qs.Status(ia, "mine") == QuestStatus::Active);
        CHECK(qs.Status(ib, "mine") == QuestStatus::Inactive);
        CHECK(!qs.StartQuest(ia, "mine"));                         // already active
        CHECK(w.Until([&] { return a.log->Status("mine") == QuestStatus::Active; }));
        w.Pump(20);
        CHECK(b.log->Status("mine") == QuestStatus::Inactive && !b.log->Record("mine"));   // b never hears of it
        CHECK(w.srv->log->Status("mine") == QuestStatus::Inactive);                      // nor the host's log
        CHECK(!qs.StartQuest(ia, "shared"));                       // a shared quest is the log's own
        CHECK(!qs.StartQuest(9999, "mine"));                       // nobody by that id
        CHECK(qs.AddProgress(ia, "mine", 0, 2));
        CHECK(qs.Progress(ia, "mine", 0) == 2);
        CHECK(w.Until([&] { return a.log->Progress("mine", 0) == 2; }));
        CHECK(qs.StartQuest(ib, "mine"));                          // b has its own
        CHECK(qs.AddProgress(ib, "mine", 0, 3));
        CHECK(qs.Status(ib, "mine") == QuestStatus::Completed && qs.Status(ia, "mine") == QuestStatus::Active);
        CHECK(w.Until([&] { return b.log->Status("mine") == QuestStatus::Completed; }));
        CHECK(a.log->Status("mine") == QuestStatus::Active);
        // What the game is told (to hand out rewards itself).
        CHECK(events.size() == 3 && events[0] == std::to_string(ia) + ":mine:active" && events[2] == std::to_string(ib) + ":mine:completed");
        // The host's own per-player quest is its log's.
        CHECK(qs.StartQuest(0, "mine"));
        CHECK(w.srv->log->Status("mine") == QuestStatus::Active);
        w.Pump(20);
        CHECK(a.log->Progress("mine", 0) == 2 && b.log->Status("mine") == QuestStatus::Completed);   // untouched by the host's
        // Once the shared prerequisite is met, the other per-player quest can start for a.
        CHECK(w.srv->log->StartQuest("shared"));
        CHECK(w.srv->log->CompleteQuest("shared"));
        CHECK(qs.StartQuest(ia, "mine2"));
        CHECK(w.Until([&] { return a.log->Status("shared") == QuestStatus::Completed && a.log->Status("mine2") == QuestStatus::Active; }));
        CHECK(b.log->Status("mine2") == QuestStatus::Inactive);

        // Per-player variables: a client's own write lands in its own copy only.
        a.story->Set("dlg.met", vs::VsValue(true));
        a.story->Set("dlg.count", vs::VsValue(2.0f));
        b.story->SetFlag("dlg.flag");
        CHECK(w.Until([&] { return a.story->Has("dlg.met") && a.story->Has("dlg.count") && b.story->HasFlag("dlg.flag"); }));
        w.Pump(20);
        CHECK(!b.story->Has("dlg.met") && !a.story->HasFlag("dlg.flag"));
        CHECK(!w.srv->story->Has("dlg.met"));                      // not the shared story, not the host's
        CHECK(qs.PlayerVar(ia, "dlg.count").AsFloat() == 2.0f && qs.PlayerFlag(ib, "dlg.flag"));
        CHECK(qs.SetPlayerVar(ib, "dlg.count", vs::VsValue(7.0f)));
        CHECK(!qs.SetPlayerVar(ib, "shared.thing", vs::VsValue(7.0f)));   // not a per-player name
        CHECK(w.Until([&] { return b.story->Get("dlg.count").AsFloat() == 7.0f; }));
        CHECK(a.story->Get("dlg.count").AsFloat() == 2.0f);
        // A late joiner gets the shared state and none of a's or b's own.
        Peer& c = w.Add();
        CHECK(c.log->Status("shared") == QuestStatus::Completed);
        CHECK(!c.log->Record("mine") && !c.story->Has("dlg.met") && !c.story->HasFlag("dlg.flag"));
        // A dropped player that comes back as itself keeps its own copy (resume: same id).
        a.nm->autoReconnect = true;
        a.nm->reconnectDelaySeconds = 0.2f;
        for (int i = 0; i < 200 && a.nm->IsConnected(); ++i) { a.scene.Update(0.25f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(a.nm->ConnectionLost());
        CHECK(w.Until([&] { return a.nm->Joined() && a.nm->Resumed(); }, 1500));
        CHECK(a.nm->LocalId() == ia);
        CHECK(w.Until([&] { return a.log->Progress("mine", 0) == 2 && a.story->Has("dlg.met"); }, 600));
        CHECK(a.log->Status("mine2") == QuestStatus::Active);
    }

    // ---- Hostile requests change nothing ----
    {
        Rig w;
        auto& qs = w.srv->nm->Quests();
        qs.perPlayerVars = {"dlg."};
        qs.maxRequestsPerSecond = 1000;
        qs.maxPlayerVars = 4;
        Peer& a = w.Add();
        auto send = [&](const std::string& line) { a.nm->SendOrdered("__req/quest", line); w.Pump(12); };
        w.srv->story->Set("score", vs::VsValue(1.0f));
        w.Pump(10);
        const int applied0 = qs.RequestsApplied();
        const std::vector<std::string> bad = {
            "", "|", "V", "V|", "V||n1", "V|score", "V|score|n1|extra", "V|score|n1", "v|score", "F|door", "f|door",   // shared names: refused
            "V|dlg.x|", "V|dlg.x|q5", "V|dlg.x|nnan", "V|dlg.x|ninf", "V|dlg.x|nabc", "V|dlg.x|n1e99", "V|dlg.x|b2", "V|dlg.x|s%", "V|dlg.x|s%zz", "V|dlg.x|s%2",
            "V|dlg.x|s" + std::string(2000, 'a'), "V|" + std::string(65, 'n') + "|n1", "V|dlg.a\nb|n1", "V|dlg.a|n1\nV|dlg.b|n2", "X|a|b", "Q|shared|1|0||", "S|shared|start", "start|shared",
            "B", "E", "V|dlg.\x01|n1",
        };
        for (const std::string& l : bad) {
            send(l);
            const bool ok = qs.RequestsApplied() == applied0;
            if (!ok) std::fprintf(stderr, "  hostile quest request applied: %s\n", l.c_str());
            CHECK(ok);
        }
        CHECK(w.srv->story->Get("score").AsFloat() == 1.0f && !w.srv->story->HasFlag("door"));
        CHECK(!w.srv->log->Record("shared"));
        CHECK(qs.Malformed() > 0 && qs.RequestsRefused() > 0);
        // A player's own variables are capped; flags count too.
        for (int i = 0; i < 6; ++i) send("V|dlg.v" + std::to_string(i) + "|n" + std::to_string(i));
        CHECK(qs.RequestsApplied() == applied0 + 4);
        send("F|dlg.flag");
        CHECK(qs.RequestsApplied() == applied0 + 4);
        send("V|dlg.v0|n9");                                         // overwriting one it has is fine
        CHECK(qs.RequestsApplied() == applied0 + 5);
        // Shared writes are capped by the server too.
        qs.clientWritableVars = {"w."};
        qs.maxSharedVars = 3;
        w.srv->story->Clear();
        for (int i = 0; i < 6; ++i) send("V|w.k" + std::to_string(i) + "|n1");
        CHECK(w.srv->story->vars.size() == 3);
        // A client cannot send the server's stream, and cannot start a quest by any message.
        a.nm->SendOrdered("__qst", "Q|shared|1|0||");
        a.nm->Send("__qst", "Q|shared|1|0||");
        a.nm->SendReliable("__qst", "Q|shared|1|0||");
        w.Pump(30);
        CHECK(!w.srv->log->Record("shared") && !a.log->Record("shared"));
    }
    {   // The request rate.
        Rig w;
        auto& qs = w.srv->nm->Quests();
        qs.perPlayerVars = {"dlg."};
        qs.maxRequestsPerSecond = 5;
        qs.maxPlayerVars = 1000;
        Peer& a = w.Add();
        for (int i = 0; i < 80; ++i) a.nm->SendOrdered("__req/quest", "V|dlg.k" + std::to_string(i) + "|n1");
        w.Pump(40);
        CHECK(qs.RequestsApplied() <= 14 && qs.RequestsApplied() >= 8);
        CHECK(qs.RequestsRefused() >= 50);
        w.Pump(120);
        a.nm->SendOrdered("__req/quest", "V|dlg.after|n1");
        CHECK(w.Until([&] { return qs.PlayerVar(a.nm->LocalId(), "dlg.after").AsFloat() == 1.0f; }));
    }

    // ---- A client survives whatever the server sends ----
    {
        Rig w;
        Peer& a = w.Add();
        const std::uint32_t id = a.nm->LocalId();
        w.srv->story->Set("keep", vs::VsValue(1.0f));
        CHECK(w.Until([&] { return a.story->Has("keep"); }));
        const std::vector<std::string> junk = {
            "", "\n\n", "Q", "Q|x", "Q|x|9|0||", "Q|x|1|-1||", "Q|x|1|0|a|", "Q|x|1|0|" + std::string(500, '1') + "|", "Q|x|1|0||2",
            "Q|x|1|0|1,2,3|1,0,5", "Q||1|0||", "V|n|nnan", "V|n|", "V|n|s%", "V||n1", "v||", "F|", "f|", "q|", "E", "X",
            "Z|1", std::string(40000, 'Q'), std::string("V|") + std::string(200, 'a') + "|n1",
        };
        for (const std::string& j : junk) w.srv->nm->SendOrderedTo(id, "__qst", j);
        w.Pump(60);
        CHECK(a.story->Get("keep").AsFloat() == 1.0f);
        CHECK(!a.log->Record("x"));
        CHECK(a.nm->Quests().Malformed() > 0);
        // A server that sent no snapshot is not trusted with the log: a fresh client's stays local.
        // (Covered below: before a snapshot, remoteAuthority is off.)
    }

    // ---- Not in a session: quests and the story are local again ----
    {
        Rig w;
        Peer& a = w.Add();
        CHECK(a.log->remoteAuthority && a.story->interceptWrite);
        a.nm->Stop();
        w.Pump(40);
        CHECK(!a.log->remoteAuthority && !a.story->interceptWrite);
        CHECK(a.log->StartQuest("shared"));
        a.story->Set("local", vs::VsValue(3.0f));
        CHECK(a.story->Get("local").AsFloat() == 3.0f);
    }
    {   // A client that never gets a snapshot (the server does not run this) keeps a working log.
        Peer lone("Lone");
        Peer srv("NoSync");
        srv.nm->Quests().enabled = false;
        CHECK(srv.nm->StartServer(0));
        srv.scene.Start();
        CHECK(lone.nm->StartClient("127.0.0.1", srv.nm->ServerPort()));
        lone.scene.Start();
        for (int i = 0; i < 300 && !lone.nm->Joined(); ++i) { srv.scene.Update(0.02f); lone.scene.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
        CHECK(lone.nm->Joined());
        for (int i = 0; i < 60; ++i) { srv.scene.Update(0.02f); lone.scene.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
        CHECK(!lone.log->remoteAuthority);
        CHECK(lone.log->StartQuest("shared"));
        lone.story->Set("x", vs::VsValue(1.0f));
        CHECK(lone.story->Has("x"));
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
