#pragma once
// ---------------------------------------------------------------------------
// INetBackend — the seam for online *services* (auth, cloud saves, leaderboards,
// matchmaking / server browser). It exists so the engine isn't married to its
// homegrown Supabase backend: a PlayFab (or any other) provider can be dropped in
// later by implementing this interface, with no changes to game code.
//
// This mirrors the ISteamService pattern already used for the platform layer:
// a virtual interface + a CreateNetBackend() factory + a thin facade (NetBackend).
// The default provider (Native) forwards to okay::Account + okay::Matchmaking, so
// existing behavior is unchanged. Other providers ship as stubs until built (e.g.
// a real PlayFab adapter would be a REST client behind -DOKAY_WITH_PLAYFAB), which
// keeps the engine self-contained and buildable everywhere by default.
// ---------------------------------------------------------------------------
#include "okay/Net/Matchmaking.hpp"           // okay::GameSession
#include "okay/Platform/Account/AccountService.hpp"   // okay::account::ScoreEntry
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Which online-services provider backs the engine. Native = the built-in
/// Account/Matchmaking (Supabase/local/custom). PlayFab/Custom are extension
/// points — stubs until an adapter is compiled in.
enum class NetBackendProvider { Native, PlayFab, Custom };

/// Online services interface. All calls degrade gracefully (return ""/false/empty)
/// when offline or signed out, so games can call them unconditionally.
class INetBackend {
public:
    virtual ~INetBackend() = default;

    /// Human-readable provider name (e.g. "Native", "PlayFab").
    virtual const char* BackendName() const = 0;
    /// True when the backend can reach its server.
    virtual bool IsOnline() const = 0;

    // ---- Authentication ----
    virtual bool IsLoggedIn() const = 0;
    virtual std::string Username() const = 0;
    virtual std::string Token() const = 0;
    virtual bool Register(const std::string& user, const std::string& password) = 0;
    virtual bool Login(const std::string& user, const std::string& password) = 0;
    virtual void Logout() = 0;
    /// Server-side check that a token is valid; fills the owning user id.
    virtual bool VerifyToken(const std::string& token, std::string& outUserId) = 0;

    // ---- Cloud saves (per-account key/value) ----
    virtual bool CloudSave(const std::string& key, const std::string& data) = 0;
    virtual std::string CloudLoad(const std::string& key) = 0;
    virtual bool CloudHas(const std::string& key) = 0;
    virtual bool CloudDelete(const std::string& key) = 0;
    virtual std::vector<std::string> CloudList() = 0;

    // ---- Leaderboards ----
    virtual bool LeaderboardSubmit(const std::string& board, long score) = 0;
    virtual std::vector<account::ScoreEntry> LeaderboardTop(const std::string& board, int count = 10) = 0;

    // ---- Matchmaking / server browser ----
    virtual std::string HostSession(const std::string& name, const std::string& hostAddr, int port,
                                    int maxPlayers = 8, const std::string& room = {},
                                    const std::string& region = {}) = 0;
    virtual bool SessionHeartbeat(const std::string& id, int players) = 0;
    virtual bool SessionUnregister(const std::string& id) = 0;
    virtual std::vector<GameSession> ListSessions(const std::string& room = {}) = 0;

    /// Last error from this backend (for stubs, explains how to enable the provider).
    virtual std::string LastError() const = 0;
};

/// Build a backend for the given provider. Native is always available; other
/// providers return a stub (IsOnline()==false, LastError() explains how to build
/// the adapter) until their integration is compiled in.
std::unique_ptr<INetBackend> CreateNetBackend(NetBackendProvider provider = NetBackendProvider::Native);

/// Lazy-singleton facade, mirroring okay::Steam / okay::Account. Defaults to the
/// Native backend; call Use() before first access to pick another provider.
class NetBackend {
public:
    static INetBackend& Get();
    static bool Exists();
    /// Select the provider (rebuilds the singleton). Call before Get().
    static void Use(NetBackendProvider provider);
    static NetBackendProvider Provider();
    static void Shutdown();
};

} // namespace okay
