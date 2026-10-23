#include "okay/Net/Matchmaker.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/ServerBrowser.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Net/MatchResults.hpp"   // (JSON helpers)
#include "okay/Platform/Account/Account.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>   // native threads: MinGW's "win32" thread model has no std::thread
#elif !defined(__EMSCRIPTEN__)
#  include <mutex>
#  include <thread>
#endif

namespace okay {

namespace {

using Clock = std::chrono::steady_clock;

void SleepMs(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#elif !defined(__EMSCRIPTEN__)
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#else
    (void)ms;
#endif
}

std::string JStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((unsigned char)c >= 0x20) o += c;
    }
    return o + "\"";
}

// The Supabase queue (docs/sql/matchmaking.sql), with the signed-in account.
struct SupabaseQueue : Matchmaker::Backend {
    static std::string Why(const account::ApiResponse& r) {
        if (r.reached && r.status == 404) return "Matchmaking isn't set up on the server yet (docs/sql/matchmaking.sql).";
        std::string m = MatchResults::Field(r.body, "message");
        return !m.empty() ? m : r.reached ? "The matchmaking server refused the request." : "Couldn't reach the matchmaking server.";
    }
    bool Join(const MatchRequest& q, std::string& ticket, std::string& err) override {
        if (!Account::Exists() || !Account::IsLoggedIn()) { err = "Sign in to find a match."; return false; }
        auto r = Account::Get().Api("/rest/v1/rpc/queue_join", "POST",
            "{\"p_game\":" + JStr(q.game) + ",\"p_mode\":" + JStr(q.mode) + ",\"p_region\":" + JStr(q.region) +
            ",\"p_players\":" + std::to_string(q.players) + ",\"p_min_players\":" + std::to_string(q.minPlayers) +
            ",\"p_party\":" + (q.withParty ? "true" : "false") + "}");
        if (!r.ok) { err = Why(r); return false; }
        ticket = r.body;
        while (!ticket.empty() && (ticket.back() == '\n' || ticket.back() == ' ')) ticket.pop_back();
        if (ticket.size() >= 2 && ticket.front() == '"') ticket = ticket.substr(1, ticket.size() - 2);
        if (ticket.size() < 32) { err = "The matchmaking server sent an odd answer."; return false; }
        return true;
    }
    bool Poll(const std::string& ticket, Matchmaker::PollResult& out, std::string& err) override {
        auto r = Account::Get().Api("/rest/v1/rpc/queue_poll", "POST", "{\"p_ticket\":" + JStr(ticket) + "}");
        if (!r.ok) { err = Why(r); return false; }
        auto rows = MatchResults::SplitObjects(r.body);
        if (rows.empty()) { err = "The matchmaking server sent an empty answer."; return false; }
        const std::string& o = rows[0];
        auto F = [&](const char* k) { return MatchResults::Field(o, k); };
        out = Matchmaker::PollResult{};
        out.status = F("status");
        out.waited = std::atoi(F("waited").c_str());
        out.searching = std::atoi(F("searching").c_str());
        MatchAssignment& a = out.match;
        a.id = F("assignment"); a.players = std::atoi(F("players").c_str());
        a.serverAddr = F("server_addr"); a.serverName = F("server_name"); a.room = F("room");
        a.relayAddr = F("relay_addr"); a.relayCode = F("relay_code");
        a.isHost = F("is_host") == "true"; a.hostLink = F("host_link");
        return true;
    }
    void Leave(const std::string& ticket) override {
        if (Account::Exists() && Account::IsLoggedIn())
            Account::Get().Api("/rest/v1/rpc/queue_leave", "POST", "{\"p_ticket\":" + JStr(ticket) + "}");
    }
    bool HostReady(const std::string& assignment, const std::string& link) override {
        return Account::Get().Api("/rest/v1/rpc/queue_host_ready", "POST",
                                  "{\"p_assignment\":" + JStr(assignment) + ",\"p_link\":" + JStr(link) + "}").ok;
    }
};

} // namespace

// Shared between the UI thread and a search's worker thread (each search gets a
// generation number; a cancelled / replaced search's worker sees it change and stops).
struct Matchmaker::Impl {
#if defined(_WIN32)
    CRITICAL_SECTION cs;
    Impl() { InitializeCriticalSection(&cs); }
    ~Impl() { DeleteCriticalSection(&cs); }
    void lock() { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
#elif !defined(__EMSCRIPTEN__)
    std::mutex mu;
    void lock() { mu.lock(); }
    void unlock() { mu.unlock(); }
#else
    void lock() {}
    void unlock() {}
#endif
    std::atomic<std::uint32_t> gen{0};
    // (guarded)
    State state = State::Idle;
    std::string error, ticket, linkToPost;
    MatchRequest req;
    MatchAssignment match;
    int searching = 0;
    Clock::time_point started{};
    bool connected = false;
    std::shared_ptr<Backend> backend;
    // settings copied at Start
    float pollInterval = 2.0f, hostWait = 40.0f;

    void Set(std::uint32_t g, State s, const std::string& err = std::string()) {
        lock();
        if (gen.load() == g) { state = s; if (!err.empty()) error = err; }
        unlock();
    }
    bool Live(std::uint32_t g) const { return gen.load() == g; }

    // The search: join the queue, poll until matched, then (as a no-relay host)
    // post our join link once Pump() is hosting. Runs on its own thread.
    void Run(std::uint32_t g, std::shared_ptr<Backend> be, MatchRequest q) {
        std::string ticket, err;
        if (!be->Join(q, ticket, err)) { Set(g, State::Failed, err); return; }
        lock(); if (Live(g)) this->ticket = ticket; unlock();
        if (!Live(g)) { be->Leave(ticket); return; }
        int failures = 0;
        Clock::time_point matchedAt{};
        bool found = false, needPost = false;
        std::string assignmentId;
        while (Live(g)) {
            PollResult pr;
            if (!be->Poll(ticket, pr, err)) {
                if (++failures >= 5) { Set(g, State::Failed, err); break; }
            } else {
                failures = 0;
                if (pr.status == "gone") { Set(g, State::Failed, "The search ended (were you signed in somewhere else?)."); break; }
                if (pr.status == "searching") {
                    lock(); if (Live(g)) searching = pr.searching; unlock();
                } else if (pr.status == "matched") {
                    MatchAssignment a = pr.match;
                    if (a.game.empty()) a.game = q.game;
                    if (a.mode.empty()) a.mode = q.mode;
                    const bool ready = a.isHost || !a.serverAddr.empty() || !a.relayAddr.empty() || !a.hostLink.empty();
                    lock();
                    if (Live(g)) { match = a; state = ready ? State::Found : State::Waiting; }
                    unlock();
                    if (ready) {
                        found = true;
                        needPost = a.isHost && a.serverAddr.empty() && a.relayAddr.empty();
                        assignmentId = a.id;
                        break;
                    }
                    if (matchedAt == Clock::time_point{}) matchedAt = Clock::now();
                    if (std::chrono::duration<float>(Clock::now() - matchedAt).count() > hostWait) {
                        Set(g, State::Failed, "The player hosting the match didn't start it. Try again.");
                        break;
                    }
                }
            }
            const int steps = std::max(1, (int)(pollInterval * 20.0f));
            for (int i = 0; i < steps && Live(g); ++i) SleepMs(50);
        }
        if (found && needPost) {   // wait for Pump() to start hosting, then tell the others
            const auto until = Clock::now() + std::chrono::seconds(30);
            std::string link;
            while (Live(g) && Clock::now() < until) {
                lock(); link = linkToPost; unlock();
                if (!link.empty()) break;
                SleepMs(50);
            }
            if (!link.empty()) be->HostReady(assignmentId, link);
        }
        be->Leave(ticket);   // done with the ticket (the match itself stays)
    }
};

namespace {
struct RunArgs { std::shared_ptr<Matchmaker::Impl> impl; std::uint32_t gen; std::shared_ptr<Matchmaker::Backend> be; MatchRequest req; };
#if defined(_WIN32)
DWORD WINAPI RunEntry(LPVOID p) {
    std::unique_ptr<RunArgs> a(static_cast<RunArgs*>(p));
    a->impl->Run(a->gen, a->be, a->req);
    return 0;
}
#endif
} // namespace

Matchmaker::Matchmaker() : m(std::make_shared<Impl>()) {}
Matchmaker::~Matchmaker() { ++m->gen; }   // a running search notices, leaves the queue and stops

Matchmaker& Matchmaker::Shared() {
    static Matchmaker* mm = new Matchmaker();   // lives until exit (worker threads may outlive statics)
    return *mm;
}

void Matchmaker::SetBackend(std::shared_ptr<Backend> b) {
    m->lock(); m->backend = std::move(b); m->unlock();
}

bool Matchmaker::Start(const MatchRequest& reqIn) {
    MatchRequest req = reqIn;
    if (req.game.empty()) req.game = ServerBrowser::Shared().Game();
    req.players = std::clamp(req.players, 2, 64);
    if (req.minPlayers > req.players) req.minPlayers = req.players;
    Cancel();
    const std::uint32_t g = ++m->gen;
    m->lock();
    std::shared_ptr<Backend> be = m->backend;
    m->state = State::Searching; m->error.clear(); m->ticket.clear(); m->linkToPost.clear();
    m->req = req; m->match = MatchAssignment{}; m->searching = 0; m->connected = false;
    m->started = Clock::now();
    m->pollInterval = std::max(0.05f, pollInterval); m->hostWait = hostWaitSeconds;
    m->unlock();
    const bool custom = be != nullptr;
    if (!be) be = std::make_shared<SupabaseQueue>();
    if (req.game.empty()) { m->Set(g, State::Failed, "Which game? (no game title)"); return false; }
    if (!custom && (!Account::Exists() || !Account::IsLoggedIn())) { m->Set(g, State::Failed, "Sign in to find a match."); return false; }
#if defined(__EMSCRIPTEN__)
    m->Set(g, State::Failed, "Matchmaking isn't available in the web build yet.");
    return false;
#else
    RunArgs* a = new RunArgs{m, g, be, req};
#  if defined(_WIN32)
    HANDLE h = CreateThread(nullptr, 0, &RunEntry, a, 0, nullptr);
    if (!h) { delete a; m->Set(g, State::Failed, "Couldn't start matchmaking."); return false; }
    CloseHandle(h);
#  else
    try { std::thread([a] { std::unique_ptr<RunArgs> p(a); p->impl->Run(p->gen, p->be, p->req); }).detach(); }
    catch (...) { delete a; m->Set(g, State::Failed, "Couldn't start matchmaking."); return false; }
#  endif
    return true;
#endif
}

void Matchmaker::Cancel() {
    ++m->gen;   // a running search sees this, leaves the queue and stops
    m->lock();
    m->state = State::Idle; m->error.clear(); m->connected = false; m->searching = 0;
    m->unlock();
}

Matchmaker::State Matchmaker::GetState() const { m->lock(); State s = m->state; m->unlock(); return s; }
const char* Matchmaker::StateName(State s) {
    switch (s) {
        case State::Searching: return "searching";
        case State::Waiting:   return "waiting";
        case State::Found:     return "found";
        case State::Failed:    return "failed";
        default:               return "idle";
    }
}
float Matchmaker::Elapsed() const {
    m->lock();
    const bool on = m->state != State::Idle;
    const auto t0 = m->started;
    m->unlock();
    return on ? std::chrono::duration<float>(Clock::now() - t0).count() : 0.0f;
}
int Matchmaker::Searching() const { m->lock(); int n = m->searching; m->unlock(); return n; }
std::string Matchmaker::Error() const { m->lock(); std::string e = m->error; m->unlock(); return e; }
MatchRequest Matchmaker::Request() const { m->lock(); MatchRequest r = m->req; m->unlock(); return r; }
MatchAssignment Matchmaker::Assignment() const { m->lock(); MatchAssignment a = m->match; m->unlock(); return a; }
bool Matchmaker::Connected() const { m->lock(); bool c = m->connected; m->unlock(); return c; }

void Matchmaker::Pump(NetworkManager* net) {
    if (!autoConnect || !net) return;
    m->lock();
    const bool go = m->state == State::Found && !m->connected;
    m->unlock();
    if (go) Connect(net);
}

bool Matchmaker::Connect(NetworkManager* net) {
    if (!net) return false;
    m->lock();
    const bool ready = m->state == State::Found;
    const MatchAssignment a = m->match;
    const bool already = m->connected;
    m->unlock();
    if (!ready) return false;
    if (already) return true;
    bool ok = false;
    std::string post;
    if (a.isHost) {
        net->Stop();
        if (!a.relayAddr.empty()) {
            std::string h; std::uint16_t p = 0;
            ok = SplitHostPort(a.relayAddr, h, p, 45100) && net->HostViaRelay(h, p, a.relayCode);
        } else {
            for (int i = 0; i < 10 && !ok; ++i) ok = net->StartServer((std::uint16_t)(hostPort + i));
            if (ok) {
                JoinLink l; l.game = a.game.empty() ? std::string("game") : a.game; l.name = "Match";
                const std::string where = !hostPublicAddress.empty() ? hostPublicAddress : LocalNetworkAddress();
                l.addr = (where.empty() ? std::string("127.0.0.1") : where) + ":" + std::to_string(net->ServerPort());
                post = l.Format();
            }
        }
    } else {
        const JoinLink l = a.Link();
        ok = l.Valid() && net->JoinByLink(l.Format());
    }
    m->lock();
    if (ok) { m->connected = true; if (!post.empty()) m->linkToPost = post; }
    else { m->state = State::Failed; m->error = "Couldn't connect to the match."; }
    m->unlock();
    return ok;
}

} // namespace okay
