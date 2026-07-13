#include "okay/Net/NetBackend.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Platform/PlayFab/PlayFab.hpp"

namespace okay {

namespace {

// ---- Native: forwards to the built-in Account + Matchmaking (Supabase/local). ----
class NativeNetBackend : public INetBackend {
public:
    const char* BackendName() const override { return "Native"; }
    bool IsOnline() const override { return Account::IsOnline(); }

    bool IsLoggedIn() const override { return Account::IsLoggedIn(); }
    std::string Username() const override { return Account::Username(); }
    std::string Token() const override { return Account::Token(); }
    bool Register(const std::string& user, const std::string& password) override {
        return Account::Register(user, password).ok;
    }
    bool Login(const std::string& user, const std::string& password) override {
        return Account::Login(user, password).ok;
    }
    void Logout() override { Account::Logout(); }
    bool VerifyToken(const std::string& token, std::string& outUserId) override {
        return Account::VerifyToken(token, outUserId);
    }

    bool CloudSave(const std::string& key, const std::string& data) override {
        return Account::CloudSave(key, data);
    }
    std::string CloudLoad(const std::string& key) override { return Account::CloudLoad(key); }
    bool CloudHas(const std::string& key) override { return Account::CloudHas(key); }
    bool CloudDelete(const std::string& key) override { return Account::CloudDelete(key); }
    std::vector<std::string> CloudList() override { return Account::CloudList(); }

    bool LeaderboardSubmit(const std::string& board, long score) override {
        return Account::LeaderboardSubmit(board, score);
    }
    std::vector<account::ScoreEntry> LeaderboardTop(const std::string& board, int count) override {
        return Account::LeaderboardTop(board, count);
    }

    std::string HostSession(const std::string& name, const std::string& hostAddr, int port,
                            int maxPlayers, const std::string& room, const std::string& region) override {
        return Matchmaking::Host(name, hostAddr, port, maxPlayers, room, region);
    }
    bool SessionHeartbeat(const std::string& id, int players) override {
        return Matchmaking::Heartbeat(id, players);
    }
    bool SessionUnregister(const std::string& id) override { return Matchmaking::Unregister(id); }
    std::vector<GameSession> ListSessions(const std::string& room) override {
        return Matchmaking::List(room);
    }

    std::string LastError() const override { return Account::LastError(); }
};

// ---- PlayFab: auth, cloud saves and leaderboards over the PlayFab Client REST
// API (via okay::PlayFab). Call PlayFab::Get().Configure("<TitleId>") before
// switching to this provider. Matchmaking stays on the Native server browser —
// PlayFab's matchmaking (Party/Multiplayer Servers) is a different product tier,
// so those calls fail with a pointer instead of pretending. ----
class PlayFabNetBackend : public INetBackend {
public:
    const char* BackendName() const override { return "PlayFab"; }
    bool IsOnline() const override { return PF().IsLoggedIn(); }

    bool IsLoggedIn() const override { return PF().IsLoggedIn(); }
    std::string Username() const override { return PF().Username(); }
    std::string Token() const override { return PF().SessionTicket(); }
    bool Register(const std::string& user, const std::string& password) override {
        return PF().RegisterWithPassword(user, password);
    }
    bool Login(const std::string& user, const std::string& password) override {
        return PF().LoginWithPassword(user, password);
    }
    void Logout() override { PF().Logout(); }
    bool VerifyToken(const std::string& token, std::string& outUserId) override {
        // Only the live session's ticket can be checked client-side.
        if (!PF().IsLoggedIn() || token != PF().SessionTicket()) return false;
        outUserId = PF().PlayFabId();
        return true;
    }

    bool CloudSave(const std::string& key, const std::string& data) override {
        return PF().SetUserData(key, data);
    }
    std::string CloudLoad(const std::string& key) override {
        std::string v; PF().GetUserData(key, v); return v;
    }
    bool CloudHas(const std::string& key) override {
        std::vector<std::string> keys;
        if (!PF().ListUserData(keys)) return false;
        for (const std::string& k : keys) if (k == key) return true;
        return false;
    }
    bool CloudDelete(const std::string& key) override { return PF().DeleteUserData(key); }
    std::vector<std::string> CloudList() override {
        std::vector<std::string> keys; PF().ListUserData(keys); return keys;
    }

    bool LeaderboardSubmit(const std::string& board, long score) override {
        return PF().SetStat(board, (int)score);
    }
    std::vector<account::ScoreEntry> LeaderboardTop(const std::string& board, int count) override {
        std::vector<PlayFab::Entry> rows;
        std::vector<account::ScoreEntry> out;
        if (!PF().GetLeaderboard(board, count, rows)) return out;
        for (const PlayFab::Entry& e : rows)
            out.push_back(account::ScoreEntry{e.name, (long)e.value, e.position + 1});
        return out;
    }

    std::string HostSession(const std::string&, const std::string&, int, int,
                            const std::string&, const std::string&) override { MmFail(); return {}; }
    bool SessionHeartbeat(const std::string&, int) override { return MmFail(); }
    bool SessionUnregister(const std::string&) override { return MmFail(); }
    std::vector<GameSession> ListSessions(const std::string&) override { MmFail(); return {}; }

    std::string LastError() const override {
        return m_err.empty() ? PF().LastError() : m_err;
    }

private:
    static PlayFab& PF() { return PlayFab::Get(); }
    bool MmFail() {
        m_err = "PlayFab matchmaking isn't wired to the server browser — keep the "
                "Native provider for sessions (NetBackend::Use(Native)) or add a "
                "PlayFab Multiplayer adapter.";
        return false;
    }
    std::string m_err;
};

// ---- Stub: a provider whose adapter hasn't been compiled in yet. Every op is a
// safe no-op and LastError() points at the extension work needed. ----
class StubNetBackend : public INetBackend {
public:
    explicit StubNetBackend(const char* name) : m_name(name) {}
    const char* BackendName() const override { return m_name; }
    bool IsOnline() const override { return false; }
    bool IsLoggedIn() const override { return false; }
    std::string Username() const override { return {}; }
    std::string Token() const override { return {}; }
    bool Register(const std::string&, const std::string&) override { return Fail(); }
    bool Login(const std::string&, const std::string&) override { return Fail(); }
    void Logout() override {}
    bool VerifyToken(const std::string&, std::string&) override { return Fail(); }
    bool CloudSave(const std::string&, const std::string&) override { return Fail(); }
    std::string CloudLoad(const std::string&) override { Fail(); return {}; }
    bool CloudHas(const std::string&) override { return Fail(); }
    bool CloudDelete(const std::string&) override { return Fail(); }
    std::vector<std::string> CloudList() override { Fail(); return {}; }
    bool LeaderboardSubmit(const std::string&, long) override { return Fail(); }
    std::vector<account::ScoreEntry> LeaderboardTop(const std::string&, int) override { Fail(); return {}; }
    std::string HostSession(const std::string&, const std::string&, int, int,
                            const std::string&, const std::string&) override { Fail(); return {}; }
    bool SessionHeartbeat(const std::string&, int) override { return Fail(); }
    bool SessionUnregister(const std::string&) override { return Fail(); }
    std::vector<GameSession> ListSessions(const std::string&) override { Fail(); return {}; }
    std::string LastError() const override { return m_err; }
private:
    bool Fail() {
        m_err = std::string(m_name) + " backend is not built into this engine. "
                "Implement INetBackend for it (a REST adapter, like the Supabase Native one) "
                "and return it from CreateNetBackend().";
        return false;
    }
    const char* m_name;
    std::string m_err;
};

std::unique_ptr<INetBackend>& Slot() { static std::unique_ptr<INetBackend> s; return s; }
NetBackendProvider& CurrentProvider() { static NetBackendProvider p = NetBackendProvider::Native; return p; }

} // namespace

std::unique_ptr<INetBackend> CreateNetBackend(NetBackendProvider provider) {
    switch (provider) {
        case NetBackendProvider::Native:  return std::make_unique<NativeNetBackend>();
        case NetBackendProvider::PlayFab: return std::make_unique<PlayFabNetBackend>();
        case NetBackendProvider::Custom:  return std::make_unique<StubNetBackend>("Custom");
    }
    return std::make_unique<NativeNetBackend>();
}

INetBackend& NetBackend::Get() {
    if (!Slot()) Slot() = CreateNetBackend(CurrentProvider());
    return *Slot();
}
bool NetBackend::Exists() { return (bool)Slot(); }
void NetBackend::Use(NetBackendProvider provider) {
    CurrentProvider() = provider;
    Slot().reset();                  // rebuilt lazily on next Get()
}
NetBackendProvider NetBackend::Provider() { return CurrentProvider(); }
void NetBackend::Shutdown() { Slot().reset(); }

} // namespace okay
