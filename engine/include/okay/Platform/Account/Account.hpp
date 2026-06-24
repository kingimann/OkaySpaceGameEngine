#pragma once
#include "okay/Platform/Account/AccountService.hpp"
#include <string>

namespace okay {

/// A process-wide account accessor so the editor, the shipped player, and game
/// scripts all share one signed-in player without each wiring up its own
/// service. The first call creates the service using the configured directory
/// and server URL (see Configure); if no server URL is configured it falls back
/// to the OKAY_ACCOUNT_SERVER environment variable, and otherwise runs the
/// local on-disk backend. This is what the `account_*` OkayScript builtins use.
class Account {
public:
    /// The shared service (created on first use).
    static account::AccountService& Get();
    /// True once Get() has created the service.
    static bool Exists();

    /// Choose where account data lives and (optionally) which auth server to
    /// use. Pass an `apiKey` to use a managed backend like Supabase (the server
    /// URL is the project URL). Call before the first Get(); calling it
    /// afterwards rebuilds the service (and drops any unsaved in-memory session).
    static void Configure(const std::string& configDir, const std::string& serverUrl = "",
                          const std::string& apiKey = "");

    /// Release the service (mainly for tests).
    static void Shutdown();

    /// Whether the shared service talks to a remote auth server.
    static bool IsOnline();
    /// Whether a player is currently signed in.
    static bool IsLoggedIn();
    /// The signed-in player's username (empty when signed out).
    static std::string Username();
    /// The current session token (empty when signed out).
    static std::string Token();

    /// Create an account and sign in. The result's `error` is also cached for
    /// LastError().
    static account::Result Register(const std::string& username, const std::string& password);
    /// Sign in to an existing account (caches `error` for LastError()).
    static account::Result Login(const std::string& username, const std::string& password);
    /// Sign out and forget the saved session.
    static void Logout();

    /// Confirm the saved session is still valid with the server (signs the
    /// player out on a definitive rejection). Safe to call on launch. Returns
    /// whether a player remains signed in.
    static bool VerifySession();

    /// Make an authenticated request to the account server with the current
    /// session token (Authorization: Bearer ...). For building server features
    /// on top of accounts (cloud saves, profiles, ...).
    static account::ApiResponse Api(const std::string& path,
                                    const std::string& method = "GET",
                                    const std::string& jsonBody = {},
                                    const std::string& extraHeader = {});

    // ---- Cloud saves (per-account storage on the server) --------------
    /// Store `data` under save slot `key`. Returns false offline / signed out.
    static bool CloudSave(const std::string& key, const std::string& data);
    /// Read save slot `key` (empty string if missing / offline).
    static std::string CloudLoad(const std::string& key);
    /// Whether save slot `key` exists on the server.
    static bool CloudHas(const std::string& key);
    /// Delete save slot `key`.
    static bool CloudDelete(const std::string& key);
    /// The player's save slot names.
    static std::vector<std::string> CloudList();

    // ---- Leaderboards (global high-score tables on the server) ---------
    /// Submit a score to a named board (the server keeps the player's best).
    static bool LeaderboardSubmit(const std::string& board, long score);
    /// The top `count` entries of a board, ranked high to low.
    static std::vector<account::ScoreEntry> LeaderboardTop(const std::string& board,
                                                           int count = 10);

    /// The error message from the most recent Register/Login (empty on success).
    static std::string LastError();
};

} // namespace okay
