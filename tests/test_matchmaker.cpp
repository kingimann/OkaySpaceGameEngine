#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Matchmaker.hpp"
#include "okay/Net/ServerBrowser.hpp"
#include <algorithm>
#include <functional>

#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using namespace okay;

// Matchmaking queues, client side: a Matchmaker joins a queue on a background
// thread, polls until matched, then Pump() connects the scene's NetworkManager —
// to a dedicated server's room, or to a player who hosts and posts its link.
// The queue here is an in-memory stand-in for docs/sql/matchmaking.sql.
namespace {
struct FakeQueue : Matchmaker::Backend {
    std::mutex mu;
    std::vector<std::string> waiting;                  // tickets in order
    std::map<std::string, MatchAssignment> matched;    // ticket -> assignment (isHost per ticket)
    std::map<std::string, std::string> hostLinks;      // assignment -> link
    int next = 0, leaves = 0;
    std::string serverAddr, room, joinError;           // set serverAddr: matches go to that server
    int need = 2;
    bool Join(const MatchRequest& r, std::string& ticket, std::string& err) override {
        std::lock_guard<std::mutex> l(mu);
        if (!joinError.empty()) { err = joinError; return false; }
        ticket = "t" + std::to_string(++next) + "-00000000000000000000000000000000";
        waiting.push_back(ticket);
        if ((int)waiting.size() >= need) {
            MatchAssignment a; a.id = "a" + std::to_string(next); a.game = r.game; a.players = need;
            a.serverAddr = serverAddr; a.room = serverAddr.empty() ? "" : room; a.serverName = serverAddr.empty() ? "" : "EU 1";
            for (std::size_t i = 0; i < waiting.size(); ++i) { MatchAssignment m = a; m.isHost = serverAddr.empty() && i == 0; matched[waiting[i]] = m; }
            waiting.clear();
        }
        return true;
    }
    bool Poll(const std::string& ticket, Matchmaker::PollResult& out, std::string&) override {
        std::lock_guard<std::mutex> l(mu);
        out = Matchmaker::PollResult{};
        auto it = matched.find(ticket);
        if (it == matched.end()) {
            bool w = false; for (auto& t : waiting) if (t == ticket) w = true;
            out.status = w ? "searching" : "gone"; out.searching = (int)waiting.size();
            return true;
        }
        out.status = "matched"; out.match = it->second;
        auto hl = hostLinks.find(it->second.id);
        if (hl != hostLinks.end()) out.match.hostLink = hl->second;
        return true;
    }
    void Leave(const std::string& ticket) override {
        std::lock_guard<std::mutex> l(mu);
        ++leaves;
        waiting.erase(std::remove(waiting.begin(), waiting.end(), ticket), waiting.end());
    }
    bool HostReady(const std::string& a, const std::string& link) override {
        std::lock_guard<std::mutex> l(mu); hostLinks[a] = link; return true;
    }
};
template <class F> bool Within(int ms, F done, std::function<void()> step = {}) {
    for (int i = 0; i < ms / 5; ++i) { if (done()) return true; if (step) step(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return done();
}
} // namespace

int main() {
    RUN_SUITE("matchmaker");
    MatchRequest req; req.game = "Arena"; req.mode = "duel"; req.players = 2;

    // ---- Matched onto a dedicated server: both join its room --------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->dedicated = true;
        CHECK(srv->StartServer(0));
        ss.Start();
        auto q = std::make_shared<FakeQueue>();
        q->serverAddr = "127.0.0.1:" + std::to_string(srv->ServerPort()); q->room = "match-1a2b";
        Matchmaker ma, mb;
        ma.pollInterval = mb.pollInterval = 0.05f;
        ma.SetBackend(q); mb.SetBackend(q);
        Scene ca("A"), cb("B");
        auto* na = ca.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* nb = cb.CreateGameObject("Net")->AddComponent<NetworkManager>();
        ca.Start(); cb.Start();
        CHECK(ma.Start(req));
        CHECK(ma.GetState() == Matchmaker::State::Searching);
        CHECK(Within(1000, [&] { return ma.Searching() == 1; }));
        CHECK(mb.Start(req));
        auto step = [&] { ss.Update(0.02f); ca.Update(0.02f); cb.Update(0.02f); ma.Pump(na); mb.Pump(nb); };
        CHECK(Within(3000, [&] { return na->Joined() && nb->Joined(); }, step));
        CHECK(ma.GetState() == Matchmaker::State::Found && ma.Connected());
        CHECK(na->Room() == "match-1a2b" && nb->Room() == "match-1a2b");
        CHECK(ma.Assignment().serverName == "EU 1" && !ma.Assignment().isHost);
        CHECK(Within(1000, [&] { std::lock_guard<std::mutex> l(q->mu); return q->leaves >= 2; }));   // tickets forgotten
        CHECK(srv->PlayerCount() == 2);
    }

    // ---- A player hosts (no relay): it posts its link, the other joins it -------------
    {
        auto q = std::make_shared<FakeQueue>();
        Matchmaker host, guest;
        host.pollInterval = guest.pollInterval = 0.05f;
        host.hostPort = 45231; host.hostPublicAddress = "127.0.0.1";
        host.SetBackend(q); guest.SetBackend(q);
        Scene hs("Host"), gs("Guest");
        auto* nh = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* ng = gs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        hs.Start(); gs.Start();
        CHECK(host.Start(req));
        CHECK(Within(1000, [&] { return host.Searching() == 1; }));
        CHECK(guest.Start(req));
        auto step = [&] { hs.Update(0.02f); gs.Update(0.02f); host.Pump(nh); guest.Pump(ng); };
        CHECK(Within(1000, [&] { return guest.GetState() == Matchmaker::State::Waiting || guest.GetState() == Matchmaker::State::Found; }, step));
        CHECK(Within(4000, [&] { return ng->Joined(); }, step));
        CHECK(host.Assignment().isHost && nh->IsServer() && nh->ServerPort() == 45231);
        CHECK(guest.Assignment().hostLink.find("127.0.0.1:45231") != std::string::npos);
        for (int i = 0; i < 20; ++i) { step(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        CHECK(nh->PlayerCount() == 2);
    }

    // ---- Cancel, errors, and a host that never starts --------------------------------
    {
        auto q = std::make_shared<FakeQueue>();
        Matchmaker mm; mm.pollInterval = 0.05f; mm.SetBackend(q);
        CHECK(mm.Start(req));
        CHECK(Within(1000, [&] { return mm.Searching() == 1; }));
        mm.Cancel();
        CHECK(mm.GetState() == Matchmaker::State::Idle);
        CHECK(Within(1000, [&] { std::lock_guard<std::mutex> l(q->mu); return q->leaves == 1 && q->waiting.empty(); }));

        q->joinError = "Only your party leader can start matchmaking.";
        mm.Start(req);
        CHECK(Within(1000, [&] { return mm.GetState() == Matchmaker::State::Failed; }));
        CHECK(mm.Error() == "Only your party leader can start matchmaking.");
        q->joinError.clear();

        Matchmaker a, b; a.pollInterval = b.pollInterval = 0.05f; b.hostWaitSeconds = 0.4f;
        a.SetBackend(q); b.SetBackend(q);
        a.autoConnect = false;                       // the host never comes up
        CHECK(a.Start(req) && Within(1000, [&] { return a.Searching() == 1; }) && b.Start(req));
        CHECK(Within(1000, [&] { return a.GetState() == Matchmaker::State::Found; }));
        CHECK(!a.Connected());
        CHECK(Within(2000, [&] { return b.GetState() == Matchmaker::State::Failed; }));
        CHECK(b.Error().find("didn't start") != std::string::npos);
        a.Cancel();
        CHECK(std::string(Matchmaker::StateName(Matchmaker::State::Waiting)) == "waiting");

        MatchRequest bad; bad.game = ""; bad.players = 2;
        Matchmaker none; none.SetBackend(q);
        ServerBrowser::Shared().SetGame("");
        CHECK(!none.Start(bad) && none.GetState() == Matchmaker::State::Failed);
    }

    // ---- Join links for each kind of match -------------------------------------------
    {
        MatchAssignment s; s.game = "Arena"; s.serverAddr = "203.0.113.7:45000"; s.room = "match-9"; s.serverName = "EU 1";
        JoinLink l = s.Link();
        CHECK(l.addr == "203.0.113.7:45000" && l.room == "match-9" && l.Valid());
        MatchAssignment r; r.game = "Arena"; r.relayAddr = "relay.example.com:45100"; r.relayCode = "K7Q29X";
        CHECK(r.Link().Relayed() && r.Link().code == "K7Q29X");
        MatchAssignment h; h.game = "Arena"; h.hostLink = "okay://join?game=Arena&addr=10.0.0.5:45000";
        CHECK(h.Link().addr == "10.0.0.5:45000");
        MatchAssignment w; w.game = "Arena";
        CHECK(!w.Link().Valid());                    // nothing to join yet
    }

    TEST_MAIN_RESULT();
}
