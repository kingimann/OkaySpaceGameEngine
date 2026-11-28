// OkaySpace account service — a small client-side account system for signing
// players in and out. It is used both by the launcher (its Account tab) and by
// the engine, which exposes a process-wide instance (okay::Account) and the
// account_* OkayScript builtins so games can sign players in from script.
//
// It is built around a single AccountService that supports two backends,
// chosen at runtime by whether a server URL is configured:
//
//   * Remote  — when a server URL is set, Register/Login POST JSON over HTTPS
//               (through the HTTP seam, okay::http::Send: WinHTTP on Windows,
//               our own client on Linux) and read back a session token. This is
//               the real "online account" path; point it at your auth server.
//
//   * Local   — when no server URL is set, accounts live in a small on-disk
//               database next to the launcher's config. Passwords are never
//               stored in the clear: each one gets a random salt from the OS
//               and is stretched with PBKDF2-HMAC-SHA256 (600,000 rounds, the
//               hash text says which, so the cost can go up later). Accounts
//               made by older versions (a plain SHA-256 chain) still sign in
//               and are re-hashed the same way on that sign-in. This lets the
//               feature work out of the box (and offline) until a server is
//               wired up. It is a development fallback, not a real server.
//
// The header is dependency-free (only the C++17 standard library + a bundled
// public-domain SHA-256) so the launcher can use it without extra build glue.
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#if !defined(_WIN32)
#include <time.h>   // nanosleep (detail::NapMs)
#endif
#include <filesystem>
#if defined(OKAY_XBOX_UWP)
#include "okay/Core/Paths.hpp"
#endif
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  if defined(OKAY_XBOX_UWP)
#    include <bcrypt.h>
#  endif
#endif
#include "okay/Crypto/Sha256.hpp"   // the engine's one SHA-256 / HMAC / PBKDF2
#include "okay/Platform/NativeFetch.hpp"   // the HTTP seam: okay::http::Send (WinHTTP / our own client)
#include "okay/Platform/Account/AccountDetail.hpp"   // PublicName and the detail:: helpers
#include "okay/Backend/Backend.hpp"                  // OkayBackend: transport, Auth, the Supabase provider, provider choice

namespace okay::account {

namespace fs = std::filesystem;


// ===========================================================================
// Public types
// ===========================================================================

/// The currently signed-in player. `loggedIn` is false when nobody is signed
/// in; `token` is an opaque session token (issued by the server, or generated
/// locally for the offline backend).
struct Session {
    bool        loggedIn = false;
    std::string username;
    std::string token;         // access token (short-lived for Supabase)
    std::string refreshToken;  // used to renew the access token (Supabase)
};

/// Result of a register/login attempt. On failure `error` carries a short,
/// human-readable reason suitable for showing in the UI.
struct Result {
    bool        ok = false;
    std::string error;
    Session     session;
};

/// Result of an authenticated request to the account server (see Api). `ok` is
/// true for a 2xx response; `reached` is false when the server couldn't be
/// contacted at all (offline / DNS / TLS). `status` is the HTTP status code
/// (0 when not reached) and `body` is the raw response body.
/// (OkayBackend's Response, okay/Backend/Transport.hpp: ok, reached, status, body, error = the seam's reason when not
/// reached, maybeSent.)
using ApiResponse = okay::backend::Response;

/// One row of a server leaderboard: the player's name, their score, and their
/// rank (1 = top).
struct ScoreEntry {
    std::string name;
    long        score = 0;
    int         rank  = 0;
};

/// Which backend signs players in.
///   Auto     — pick from the configuration: Local when no server URL is set,
///              Supabase when an API key is also set, otherwise Custom.
///   Local    — on-device accounts (a dev fallback; not a real server).
///   Custom   — our own auth server (the reference server's /register, /login).
///   Supabase — a hosted Supabase project (auth/v1 REST + anon apikey).
enum class Provider { Auto, Local, Custom, Supabase };

/// How a check of the saved session with the server went (AccountService::CheckSession).
enum class SessionCheck { Valid, Rejected, Unreachable, ServerError, NoSession };

/// A client-side account service. Construct it with a config directory (where a
/// saved session and any local accounts live) and, for online use, a server URL
/// (+ an API key for managed providers like Supabase). With nothing configured
/// it falls back to local, on-device accounts for development.
class AccountService {
public:
    explicit AccountService(fs::path configDir, std::string serverUrl = {},
                            std::string apiKey = {}, Provider provider = Provider::Auto)
        : configDir_(std::move(configDir)), serverUrl_(std::move(serverUrl)),
          apiKey_(std::move(apiKey)), provider_(Resolve(FromBackendConfig(provider, configDir_), serverUrl_, apiKey_)) {
        std::error_code ec;
        fs::create_directories(configDir_, ec);
        LoadSession();
    }

    /// Tests only: send every request to `t` (method, url, header lines, body) instead of the network, so the sign-in and
    /// session code can be run against a fake server. Empty = the network again.
    void SetTransportForTest(std::function<okay::http::Result(const std::string&, const std::string&,
                                                              const std::vector<std::string>&, const std::string&)> t) {
        transport_.SetHook(std::move(t));
    }
    /// Tests only: `o` sees every whole request just before it is sent (also the body file of an upload and the time limit,
    /// which SetTransportForTest's function does not get). It changes nothing that is sent. Empty = no observer.
    void ObserveRequestsForTest(std::function<void(const okay::backend::Request&)> o) { transport_.SetObserver(std::move(o)); }

    /// True when configured to talk to a real (non-local) backend.
    bool IsOnline() const { return provider_ != Provider::Local; }
    Provider GetProvider() const { return provider_; }
    /// Identifier of the active backend ("local", "custom", or "supabase").
    const char* ProviderName() const {
        switch (provider_) {
            case Provider::Supabase: return "supabase";
            case Provider::Custom:   return "custom";
            default:                 return "local";
        }
    }
    /// Whether the backend identifies players by email (Supabase) rather than a
    /// freeform username (local / custom). Lets the UI label the field.
    bool UsesEmail() const { return provider_ == Provider::Supabase; }
    const std::string& ServerUrl() const { return serverUrl_; }

    const Session& CurrentSession() const { return session_; }
    bool IsLoggedIn() const { return session_.loggedIn; }

    /// Create a new account (2-arg form: no separate display name).
    Result Register(const std::string& id, const std::string& password) {
        return Register(id, password, std::string{});
    }
    /// Create a new account. For Supabase `id` is the email and `username` is an
    /// optional display name (stored in user metadata); for local/custom backends
    /// `id` is the username and `username` is ignored.
    Result Register(const std::string& id, const std::string& password,
                    const std::string& username) {
        std::string err = Validate(id, password);
        if (err.empty() && !username.empty() && (username.size() < 2 || username.size() > 32))
            err = "Username must be 2–32 characters.";
        if (!err.empty()) return Fail(err);
        switch (provider_) {
            case Provider::Supabase: return SupabaseAuth(true, id, password, username);
            case Provider::Custom:   return RemoteAuth("register", id, password);
            default:                 return LocalRegister(id, password);
        }
    }

    /// Sign in to an existing account.
    Result Login(const std::string& username, const std::string& password) {
        std::string err = Validate(username, password);
        if (!err.empty()) return Fail(err);
        switch (provider_) {
            case Provider::Supabase: return SupabaseAuth(false, username, password);
            case Provider::Custom:   return RemoteAuth("login", username, password);
            default:                 return LocalLogin(username, password);
        }
    }

    /// Sign out and forget the saved session.
    void Logout() {
        if (provider_ == Provider::Supabase) Sb().SignOut(session_.token);   // sends nothing (OkayBackend Auth::SignOut)
        session_ = {};
        std::error_code ec;
        fs::remove(SessionPath(), ec);
    }

    /// Where the emailed reset link should land (the hosted page from
    /// supabase/auth-pages/reset-password, see docs/online/password-reset.md). Must be an
    /// http(s) URL that is also on the Supabase Auth redirect allow-list. Empty =
    /// not configured: Supabase then uses the project's Site URL instead.
    void SetResetRedirect(std::string url) { resetRedirect_ = std::move(url); }
    const std::string& ResetRedirect() const { return resetRedirect_; }
    bool HasResetRedirect() const { return resetRedirect_.rfind("http", 0) == 0; }

    /// The recover endpoint for `base` (+ optional redirect). Pure, so it is unit tested.
    static std::string RecoverUrl(std::string base, const std::string& redirect) {
        return okay::backend::supabase::Auth::RecoverUrl(std::move(base), redirect);
    }

    /// Send the sign-up confirmation email again (Supabase only), for "Check your email" after creating an account.
    Result ResendConfirmation(const std::string& email) {
        if (provider_ != Provider::Supabase) return Fail("Confirmation emails need the online (Supabase) account server.");
        if (!LooksLikeEmail(email)) return Fail("Enter a valid email address.");
        ApiResponse r = Sb().Resend(email);
        if (!r.reached) return Fail("Couldn't reach the account server.");
        if (!r.ok) return Fail("Couldn't send the email again (HTTP " + std::to_string(r.status) + ").");
        Result ok; ok.ok = true; return ok;
    }

    /// Send a password-reset email (Supabase only). The user follows the emailed
    /// link to set a new password. Returns ok when the request was accepted.
    Result RequestPasswordReset(const std::string& email) {
        if (provider_ != Provider::Supabase)
            return Fail("Password reset needs the online (Supabase) account server.");
        if (!LooksLikeEmail(email)) return Fail("Enter a valid email address.");
        ApiResponse r = Sb().Recover(email, resetRedirect_);
        if (!r.reached) return Fail("Couldn't reach the account server.");
        if (!r.ok) return Fail("Couldn't start password reset (HTTP " + std::to_string(r.status) + ").");
        Result ok; ok.ok = true; return ok;
    }

    /// A reset code from the email: 6 to 10 digits (Supabase's {{ .Token }} in the "Reset Password" email template).
    static bool LooksLikeRecoveryCode(const std::string& code) {
        if (code.size() < 6 || code.size() > 10) return false;
        for (char c : code) if (c < '0' || c > '9') return false;
        return true;
    }
    /// The body of POST auth/v1/verify that trades an emailed reset code for a session. Pure, so it is unit tested.
    static std::string RecoveryVerifyBody(const std::string& email, const std::string& code) {
        return okay::backend::supabase::Auth::RecoveryVerifyBody(email, code);
    }

    /// Choose a new password with the code from the reset email, all inside the program: no web page (and no GitHub Pages) is
    /// needed. Supabase: POST auth/v1/verify (type recovery) gives a short session, then PUT auth/v1/user sets the password.
    /// The session is used only for that and is not kept: the person signs in with the new password afterwards.
    Result ResetPasswordWithCode(const std::string& email, const std::string& code, const std::string& newPassword) {
        if (provider_ != Provider::Supabase) return Fail("Password reset needs the online (Supabase) account server.");
        if (!LooksLikeEmail(email)) return Fail("Enter a valid email address.");
        if (!LooksLikeRecoveryCode(code)) return Fail("Enter the code from the reset email (digits only).");
        if (newPassword.size() < 6) return Fail("Password must be at least 6 characters.");
        ApiResponse v = Sb().VerifyRecovery(email, code);
        if (!v.reached) return Fail("Couldn't reach the account server.");
        const std::string token = detail::JsonField(v.body, "access_token");
        if (!v.ok || token.empty()) return Fail("That code is wrong or has expired. Ask for a new reset email.");
        ApiResponse u = Sb().UpdateUser(token, "{\"password\":\"" + detail::JsonEscape(newPassword) + "\"}");
        if (!u.reached) return Fail("Couldn't reach the account server.");
        if (!u.ok) {
            std::string e = detail::JsonField(u.body, "msg");
            if (e.empty()) e = detail::JsonField(u.body, "message");
            return Fail(e.empty() ? "Password change failed." : e);
        }
        Result ok; ok.ok = true; return ok;
    }

    /// Change the signed-in user's password. Supabase: PUT auth/v1/user with the
    /// bearer token. Local: re-hash. Requires being signed in.
    Result ChangePassword(const std::string& newPassword) {
        if (!session_.loggedIn) return Fail("Sign in first.");
        if (newPassword.size() < 6) return Fail("Password must be at least 6 characters.");
        if (provider_ == Provider::Supabase) {
            ApiResponse r = UpdateUserWithFreshToken("{\"password\":\"" + detail::JsonEscape(newPassword) + "\"}");
            if (!r.reached) return Fail("Couldn't reach the account server.");
            if (!r.ok) {
                std::string e = detail::JsonField(r.body, "msg");
                if (e.empty()) e = detail::JsonField(r.body, "message");
                return Fail(e.empty() ? "Password change failed." : e);
            }
            Result ok; ok.ok = true; ok.session = session_; return ok;
        }
        if (provider_ == Provider::Local) {
            auto records = ReadDb();
            std::string key = Lower(session_.username);
            for (auto& rec : records) {
                if (Lower(rec.user) != key) continue;
                rec.salt = detail::RandomHex(16);
                rec.hash = detail::HashPasswordV2(newPassword, rec.salt);
                WriteDb(records);
                Result ok; ok.ok = true; ok.session = session_; return ok;
            }
            return Fail("Account not found.");
        }
        return Fail("Changing the password isn't supported for this server.");
    }

    /// Change the signed-in user's email (Supabase only). Supabase may send a
    /// confirmation link to the new address before it takes effect.
    Result ChangeEmail(const std::string& newEmail) {
        if (!session_.loggedIn) return Fail("Sign in first.");
        if (!LooksLikeEmail(newEmail)) return Fail("Enter a valid email address.");
        if (provider_ != Provider::Supabase)
            return Fail("Email change needs the online (Supabase) account server.");
        ApiResponse r = UpdateUserWithFreshToken("{\"email\":\"" + detail::JsonEscape(newEmail) + "\"}");
        if (!r.reached) return Fail("Couldn't reach the account server.");
        if (!r.ok) {
            std::string e = detail::JsonField(r.body, "msg");
            if (e.empty()) e = detail::JsonField(r.body, "message");
            return Fail(e.empty() ? "Email change failed." : e);
        }
        Result ok; ok.ok = true; ok.session = session_; return ok;
    }

    /// Change the signed-in user's display username. Supabase stores it in user
    /// metadata; the local backend renames the account. Updates the session.
    Result ChangeUsername(const std::string& newUsername) {
        if (!session_.loggedIn) return Fail("Sign in first.");
        if (newUsername.size() < 2 || newUsername.size() > 32)
            return Fail("Username must be 2–32 characters.");
        if (provider_ == Provider::Supabase) {
            ApiResponse r = UpdateUserWithFreshToken("{\"data\":{\"username\":\"" + detail::JsonEscape(newUsername) + "\"}}");
            if (!r.reached) return Fail("Couldn't reach the account server.");
            if (!r.ok) {
                std::string e = detail::JsonField(r.body, "msg");
                if (e.empty()) e = detail::JsonField(r.body, "message");
                return Fail(e.empty() ? "Username change failed." : e);
            }
            session_.username = newUsername;   // display name
            SaveSession();
            Result ok; ok.ok = true; ok.session = session_; return ok;
        }
        if (provider_ == Provider::Local) {
            auto records = ReadDb();
            std::string newKey = Lower(newUsername);
            for (const auto& rec : records)            // must stay unique
                if (Lower(rec.user) == newKey && Lower(rec.user) != Lower(session_.username))
                    return Fail("That username is already taken.");
            std::string key = Lower(session_.username);
            for (auto& rec : records) {
                if (Lower(rec.user) != key) continue;
                rec.user = newUsername;
                WriteDb(records);
                session_.username = newUsername;
                SaveSession();
                Result ok; ok.ok = true; ok.session = session_; return ok;
            }
            return Fail("Account not found.");
        }
        return Fail("Changing the username isn't supported for this server.");
    }

    /// Make an authenticated request to the account server, attaching the
    /// current session token as `Authorization: Bearer <token>`. `path` is the
    /// part after the server URL (e.g. "/profile"); `method` defaults to GET.
    /// `jsonBody`, when non-empty, is sent as an application/json body.
    ///
    /// Use this to build server features on top of accounts (cloud saves,
    /// profiles, ...). Returns a not-reached response for the local backend or
    /// when no one is signed in.
    ApiResponse Api(const std::string& path, const std::string& method = "GET",
                    const std::string& jsonBody = {}, const std::string& extraHeader = {}) const {
        return ApiTimed(path, method, jsonBody, extraHeader, 0);
    }
    /// Api with a time limit in seconds (0 = none, as Api). A request that is not answered in time comes back not-reached, so a caller
    /// that must not hang (the join-time session ticket) can retry or give up. The limit is enforced by both native clients (WinHTTP on
    /// Windows, our own client on Linux; okay::http::Request). A request that may already have reached the server comes back
    /// not-reached with maybeSent set, and is never sent a second time.
    ApiResponse ApiTimed(const std::string& path, const std::string& method, const std::string& jsonBody,
                         const std::string& extraHeader, int timeoutSec) const {
        if (!IsOnline() || !session_.loggedIn) return {};
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += path.empty() || path.front() == '/' ? path : ("/" + path);
        return WithFreshToken([&](const std::string& token) {
            return HttpRequest(method, url, jsonBody, token, extraHeader, {}, {}, timeoutSec);
        });
    }

    /// The Database calls (OkayBackend, okay/Backend/Database.hpp) through this account: every request goes through Api, so
    /// it carries the signed-in token, sends nothing when offline or signed out, and re-reads a newer saved token after a 401.
    /// The object it returns must outlive the Query made from it (`auto db = svc.Db(); db.From(...)...`).
    okay::backend::supabase::Database Db() const {
        return okay::backend::supabase::Database(
            [this](const std::string& method, const std::string& path, const std::string& body, const std::string& extra) {
                return Api(path, method, body, extra);
            });
    }
    /// Db with the publishable key only (ApiAnon): public reads that need no session, such as the username check.
    okay::backend::supabase::Database DbAnon() const {
        return okay::backend::supabase::Database(
            [this](const std::string& method, const std::string& path, const std::string& body, const std::string& extra) {
                return ApiAnon(path, method, body, extra);
            });
    }
    /// The Storage calls (OkayBackend, okay/Backend/Storage.hpp) through this account: requests go through Api and uploads
    /// through ApiUpload, so they carry the signed-in token and send nothing when offline or signed out. Public addresses
    /// are built from this account's server address.
    okay::backend::supabase::Storage Storage() const {
        return okay::backend::supabase::Storage(
            serverUrl_,
            [this](const std::string& method, const std::string& path, const std::string& body, const std::string& extra) {
                return Api(path, method, body, extra);
            },
            [this](const std::string& path, const std::string& file, const std::string& type, const std::string& extra) {
                return ApiUpload(path, file, type, extra);
            });
    }
    /// The Functions calls (OkayBackend, okay/Backend/Functions.hpp) through this account: through ApiTimed, so a call carries
    /// the signed-in token, sends nothing when offline or signed out, re-reads a newer saved token after a 401, and keeps its
    /// time limit.
    okay::backend::supabase::Functions Functions() const {
        return okay::backend::supabase::Functions(
            [this](const std::string& method, const std::string& path, const std::string& body, const std::string& extra, int timeoutSec) {
                return ApiTimed(path, method, body, extra, timeoutSec);
            });
    }

    /// Re-read the access token saved by another instance (same account).
    /// True when it changed.
    bool ReloadSessionToken() const {
        Session disk;
        if (!ReadSessionFile(disk) || disk.token == session_.token) return false;
        session_.token = disk.token;
        if (!disk.refreshToken.empty()) session_.refreshToken = disk.refreshToken;
        return true;
    }

    /// Take the session saved in session.txt as it is now (the launcher and the editor share that file, and either can sign
    /// in, refresh the token or sign out while the other runs). A different sign-in there replaces ours; no file (signed out
    /// elsewhere) signs this instance out too, without touching the disk. True when the in-memory session changed. The
    /// editor's sign-in gate calls it when the file changes (SessionFileStamp), and CheckSession calls it first, so a check
    /// never uses a token another program has already replaced.
    bool SyncSessionFromDisk() {
        Session disk;
        if (!ReadSessionFile(disk)) {
            if (!session_.loggedIn) return false;
            session_ = {};
            return true;
        }
        if (session_.loggedIn && disk.token == session_.token && disk.refreshToken == session_.refreshToken &&
            disk.username == session_.username) return false;
        session_ = disk;
        return true;
    }

    /// When session.txt was last written (an opaque number; -1 when there is no file). Cheap enough to poll once a
    /// second: the editor's sign-in gate re-checks when it changes, so a sign-in in the launcher opens the editor.
    long long SessionFileStamp() const {
        std::error_code ec;
        const auto t = fs::last_write_time(SessionPath(), ec);
        if (ec) return -1;
        const long long n = (long long)t.time_since_epoch().count();
        return n == -1 ? -2 : n;
    }

    /// The signed-in account's id (the JWT "sub"), "" when unknown. Profiles
    /// use the same id, so this finds MY profile even if the shown name differs.
    std::string UserId() const { return JwtClaim(session_.token, "sub"); }
    /// When the access token expires (unix seconds), 0 when unknown. 64-bit: `long` is 32 bits on Windows, where a
    /// time after 2038 (a test token's 2100) came back cut to a negative number.
    long long TokenExpiresAt() const { return std::atoll(JwtClaim(session_.token, "exp").c_str()); }
    /// Renew the access token when it expires within `marginSec` (Supabase).
    /// Call from ONE place (the launcher's main session). True if refreshed.
    bool RefreshIfExpiring(long marginSec = 600) {
        if (provider_ != Provider::Supabase || !session_.loggedIn) return false;
        const long long exp = TokenExpiresAt();
        if (exp > 0 && exp - (long long)std::time(nullptr) > marginSec) return false;
        ReloadSessionToken();   // someone else may already have renewed it
        if (TokenExpiresAt() - (long long)std::time(nullptr) > marginSec) return true;
        return RefreshSupabaseToken();
    }

    /// Anonymous request (apikey only, no bearer) — for public reads that don't
    /// need a session, e.g. checking username availability before sign-up.
    ApiResponse ApiAnon(const std::string& path, const std::string& method = "GET",
                        const std::string& jsonBody = {}, const std::string& extraHeader = {}) const {
        if (!IsOnline()) return {};
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += path.empty() || path.front() == '/' ? path : ("/" + path);
        return HttpRequest(method, url, jsonBody, /*authToken=*/{}, extraHeader);
    }

    /// Upload a local file as the raw request body with the session token — e.g.
    /// Supabase Storage `POST /storage/v1/object/<bucket>/<path>` for chat
    /// attachments. `contentType` is the file's MIME type.
    ApiResponse ApiUpload(const std::string& path, const std::string& localFile,
                          const std::string& contentType, const std::string& extraHeader = {}) const {
        if (!IsOnline() || !session_.loggedIn) return {};
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += path.empty() || path.front() == '/' ? path : ("/" + path);
        return HttpRequest("POST", url, {}, /*authToken=*/session_.token, extraHeader,
                           localFile, contentType.empty() ? "application/octet-stream" : contentType);
    }

    /// Whether a username (profiles.username handle) is free. Online only; on a
    /// local backend or when offline it optimistically returns true.
    bool UsernameAvailable(const std::string& username) const {
        if (provider_ != Provider::Supabase || username.empty()) return true;
        ApiResponse r = DbAnon().From("profiles").Select("id").Eq("username", username).Get();   // the name percent-encoded
        if (!r.ok) return true;                       // couldn't check -> don't block
        return r.body.find('{') == std::string::npos; // no rows -> available
    }

    /// Confirm the saved session is still valid with the server (GET /verify
    /// with the bearer token). On a definitive rejection (401/403) the local
    /// session is cleared so the player is signed out. Network errors leave the
    /// session intact (so being offline doesn't sign players out). The local
    /// backend has nothing to check, so it just reports whether someone is in.
    /// `reached` (optional): false when the server could not be asked (the launcher's sign-in gate blocks then).
    bool VerifySession(bool* reached = nullptr) {
        const SessionCheck c = CheckSession();
        if (reached) *reached = c != SessionCheck::Unreachable;
        return c == SessionCheck::Valid || c == SessionCheck::Unreachable;   // offline: keep the session
    }

    /// The same check as VerifySession, saying which way it went: Valid (the server
    /// confirmed it, or a local backend has someone signed in), Rejected (401/403
    /// and the refresh failed: signed out), Unreachable (no answer: the session is
    /// kept, but nothing confirmed it), ServerError (it answered with another
    /// error: kept, not confirmed), NoSession (nobody is signed in). The editor's
    /// sign-in gate (editor/SignInGate.hpp) opens only on Valid.
    ///
    /// The launcher and the editor share session.txt, and Supabase refresh tokens work once (each refresh hands out a new
    /// one). So the check reads the file first (another program may have signed in, refreshed or signed out), and after a
    /// rejection and a failed refresh it reads it again: when the other program saved a newer sign-in meanwhile, that one
    /// is checked instead, and the file is never deleted while it holds a sign-in other than the one that was rejected.
    SessionCheck CheckSession() {
        if (IsOnline()) SyncSessionFromDisk();
        if (!session_.loggedIn) return SessionCheck::NoSession;
        if (!IsOnline()) return SessionCheck::Valid;
        for (int attempt = 0; attempt < 2; ++attempt) {
            // Supabase exposes the signed-in user at auth/v1/user; our custom server uses /verify.
            ApiResponse r = provider_ == Provider::Supabase
                ? WithFreshToken([this](const std::string& token) { return Sb().User(token); })
                : Api("/verify");
            if (!r.reached) return SessionCheck::Unreachable;
            if (r.status != 401 && r.status != 403)
                return r.ok ? SessionCheck::Valid : SessionCheck::ServerError;   // any other failure: kept, nothing confirmed it
            // Token expired/revoked. Try to renew from the refresh token before giving up: a 1-hour Supabase access token
            // expiring shouldn't sign the player out.
            if (RefreshSupabaseToken()) return SessionCheck::Valid;
            if (ForgetSessionUnlessNewerOnDisk()) return SessionCheck::Rejected;
            // another program saved a newer sign-in meanwhile: check that one
        }
        return SessionCheck::Rejected;   // (the newer sign-in on disk is left for its owner)
    }

    /// Verify an ARBITRARY access token (not our own session) and, on success, hand
    /// back the account id it belongs to. This is what a multiplayer host uses to
    /// authenticate a joining client's Supabase token — wire it into
    /// NetworkManager::SetTokenVerifier. Supabase: GET auth/v1/user with the token as
    /// Bearer ({"id": "<uuid>", ...}); custom server: GET /verify with the token.
    /// Returns false for the local backend, offline, or an empty/invalid token.
    bool VerifyToken(const std::string& token, std::string& outUserId) const {
        outUserId.clear();
        if (token.empty() || !IsOnline()) return false;
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Sb().User(token);
            if (!r.ok) return false;
            outUserId = detail::JsonField(r.body, "id");
            return !outUserId.empty();
        }
        std::string base = serverUrl_;
        if (!base.empty() && base.back() == '/') base.pop_back();
        return HttpRequest("GET", base + "/verify", {}, token).ok;   // custom server
    }

    // ---- Cloud saves ---------------------------------------------------
    // Per-account key/value storage on the server, so progress follows the
    // player across devices. `key` names a save slot ("save1", "settings");
    // `data` is arbitrary text (your serialized save). These require the online
    // backend and a signed-in player; on the local backend they no-op (Save/
    // Delete return false, Load returns "", List returns empty).
    //
    // Wire protocol (see the reference server):
    //   POST   /cloud/<key>   {"data": "..."}   -> 200
    //   GET    /cloud/<key>                       -> 200 {"data": "..."} | 404
    //   DELETE /cloud/<key>                       -> 200
    //   GET    /cloud                             -> 200 {"keys": [...]}
    bool CloudSave(const std::string& key, const std::string& data) {
        if (!CloudKeyOk(key)) return false;
        if (provider_ == Provider::Supabase)   // upsert a row in the cloud_saves table (docs/sql/cloud_saves.sql)
            return Db().From("cloud_saves").Returning(okay::backend::Return::Minimal)
                       .Upsert("[{\"key\":\"" + detail::JsonEscape(key) + "\",\"data\":\"" + detail::JsonEscape(data) + "\"}]").ok;
        return Api("/cloud/" + key, "POST",
                   "{\"data\":\"" + detail::JsonEscape(data) + "\"}").ok;
    }
    std::string CloudLoad(const std::string& key) {
        if (!CloudKeyOk(key)) return {};
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Db().From("cloud_saves").Select("data").Eq("key", key).Get();   // (CloudKeyOk: encoding changes nothing)
            return r.ok ? detail::JsonField(r.body, "data") : std::string{};  // [{"data":...}]
        }
        ApiResponse r = Api("/cloud/" + key, "GET");
        return r.ok ? detail::JsonField(r.body, "data") : std::string{};
    }
    bool CloudHas(const std::string& key) {
        if (!CloudKeyOk(key)) return false;
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Db().From("cloud_saves").Select("key").Eq("key", key).Get();
            return r.ok && r.body.find('{') != std::string::npos;  // non-empty result array
        }
        return Api("/cloud/" + key, "GET").ok;
    }
    bool CloudDelete(const std::string& key) {
        if (!CloudKeyOk(key)) return false;
        if (provider_ == Provider::Supabase)
            return Db().From("cloud_saves").Eq("key", key).Delete().ok;
        return Api("/cloud/" + key, "DELETE").ok;
    }
    std::vector<std::string> CloudList() {
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Db().From("cloud_saves").Select("key").Get();
            return r.ok ? detail::JsonFieldValues(r.body, "key") : std::vector<std::string>{};
        }
        ApiResponse r = Api("/cloud", "GET");
        return r.ok ? detail::JsonStringArray(r.body, "keys") : std::vector<std::string>{};
    }

    // ---- Leaderboards --------------------------------------------------
    // Global high-score tables keyed by a board name. Submitting keeps the
    // player's best. Requires the online backend + a signed-in player; no-ops
    // on the local backend (Submit returns false, Top returns empty).
    //
    // Wire protocol (see the reference server):
    //   POST /leaderboard/<board>          {"score": N}   -> 200
    //   GET  /leaderboard/<board>?count=N                 -> 200
    //        {"entries": ["1,alice,500", "2,bob,300", ...]}  (rank,name,score)
    bool LeaderboardSubmit(const std::string& board, long score) {
        if (!CloudKeyOk(board)) return false;
        if (provider_ == Provider::Supabase)   // upsert (board,user); a DB trigger keeps the best (docs/sql/leaderboards.sql)
            return Db().From("leaderboards").Returning(okay::backend::Return::Minimal)
                       .Upsert("[{\"board\":\"" + detail::JsonEscape(board) + "\",\"name\":\"" + detail::JsonEscape(session_.username) +
                               "\",\"score\":" + std::to_string(score) + "}]").ok;
        return Api("/leaderboard/" + board, "POST",
                   "{\"score\":" + std::to_string(score) + "}").ok;
    }
    std::vector<ScoreEntry> LeaderboardTop(const std::string& board, int count = 10) {
        std::vector<ScoreEntry> out;
        if (!CloudKeyOk(board)) return out;
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Db().From("leaderboards").Select("name,score").Eq("board", board).Order("score", true).Limit(count).Get();
            if (!r.ok) return out;
            auto names  = detail::JsonFieldValues(r.body, "name");
            auto scores = detail::JsonNumberValues(r.body, "score");
            for (std::size_t i = 0; i < names.size() && i < scores.size(); ++i) {
                ScoreEntry e; e.name = names[i]; e.score = scores[i]; e.rank = (int)i + 1;
                out.push_back(std::move(e));
            }
            return out;
        }
        ApiResponse r = Api("/leaderboard/" + board + "?count=" + std::to_string(count), "GET");
        if (!r.ok) return out;
        for (const std::string& s : detail::JsonStringArray(r.body, "entries")) {
            // "rank,name,score" — names are usernames, so they carry no commas.
            std::size_t c1 = s.find(',');
            std::size_t c2 = s.rfind(',');
            if (c1 == std::string::npos || c2 == c1) continue;
            ScoreEntry e;
            try { e.rank = std::stoi(s.substr(0, c1)); } catch (...) {}
            e.name = s.substr(c1 + 1, c2 - c1 - 1);
            try { e.score = std::stol(s.substr(c2 + 1)); } catch (...) {}
            out.push_back(std::move(e));
        }
        return out;
    }

private:
    // Save keys live in a URL path, so keep them to a safe, simple alphabet.
    static bool CloudKeyOk(const std::string& key) {
        if (key.empty() || key.size() > 64) return false;
        for (char c : key)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                return false;
        return true;
    }

    /// Where requests go: the network, or the test function set by SetTransportForTest. Mutable: sending is not a change
    /// to the account service.
    mutable okay::backend::HookTransport transport_;

    fs::path    configDir_;
    std::string serverUrl_;
    std::string resetRedirect_;   // see SetResetRedirect
    std::string apiKey_;
    Provider    provider_;
    mutable Session session_;   // mutable: Api() picks up a token refreshed by another instance

    fs::path SessionPath() const { return configDir_ / "session.txt"; }

    // A claim from a JWT's payload (base64url JSON), without verifying it — we
    // only read our own token's id / expiry.
    static std::string JwtClaim(const std::string& jwt, const std::string& key) { return okay::backend::TokenClaim(jwt, key); }

    /// The Supabase provider's Auth calls (OkayBackend), on this service's address, key and transport. Made per call, so
    /// copying an AccountService never leaves it pointing at another one's transport.
    okay::backend::supabase::Auth Sb() const { return okay::backend::supabase::Auth(serverUrl_, apiKey_, transport_); }

    /// A call with the session's access token. Expired token (401): the launcher refreshes it (RefreshIfExpiring) and
    /// saves it; workers built from the saved session pick that up and try once more. (Workers never refresh themselves:
    /// Supabase rotates refresh tokens, and racing refreshes can revoke the whole session.)
    template <class Call>
    ApiResponse WithFreshToken(Call call) const {
        ApiResponse r = call(session_.token);
        if (r.status == 401 && provider_ == Provider::Supabase && ReloadSessionToken()) r = call(session_.token);
        return r;
    }
    /// PUT auth/v1/user (password, email or username change) for the signed-in account, as Api() sent it.
    ApiResponse UpdateUserWithFreshToken(const std::string& json) const {
        return WithFreshToken([&](const std::string& token) { return Sb().UpdateUser(token, json); });
    }

    /// Provider::Auto follows the backend configuration (OKAY_BACKEND, or "provider" in backend.json in the config folder;
    /// okay/Backend/Backend.hpp): "supabase" or "local" picks that one, anything else keeps Auto. An explicit provider wins.
    static Provider FromBackendConfig(Provider p, const fs::path& configDir) {
        if (p != Provider::Auto) return p;
        switch (okay::backend::LoadConfig(configDir).provider) {
            case okay::backend::ProviderKind::Supabase: return Provider::Supabase;
            case okay::backend::ProviderKind::Local:    return Provider::Local;
            default:                                    return Provider::Auto;
        }
    }
    fs::path DbPath() const { return configDir_ / "accounts.db"; }

    // Turn Provider::Auto into a concrete backend from what's configured.
    static Provider Resolve(Provider p, const std::string& url, const std::string& key) {
        if (p != Provider::Auto) return p;
        if (url.empty()) return Provider::Local;
        return key.empty() ? Provider::Custom : Provider::Supabase;
    }

    static std::string Lower(std::string s) {
        for (char& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    }

    static Result Fail(const std::string& msg) {
        Result r; r.ok = false; r.error = msg; return r;
    }

    static bool LooksLikeEmail(const std::string& s) {
        std::size_t at = s.find('@');
        if (at == std::string::npos || at == 0) return false;
        std::size_t dot = s.find('.', at);
        return dot != std::string::npos && dot + 1 < s.size();
    }

    std::string Validate(const std::string& username, const std::string& password) const {
        if (provider_ == Provider::Supabase) {
            if (!LooksLikeEmail(username)) return "Enter a valid email address.";
        } else {
            if (username.size() < 3) return "Username must be at least 3 characters.";
            if (username.size() > 32) return "Username is too long (max 32).";
            for (char c : username)
                if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                    return "Username may use letters, digits, '_', '-', '.' only.";
        }
        if (password.size() < 6) return "Password must be at least 6 characters.";
        return {};
    }

    // ---- session persistence ------------------------------------------
    /// session.txt as a signed-in Session (user, token, optional refresh token, one per line). False when there is no file
    /// or it holds no sign-in.
    bool ReadSessionFile(Session& out) const {
        std::ifstream f(SessionPath());
        if (!f) return false;
        std::string user, token, refresh;
        std::getline(f, user);
        std::getline(f, token);
        std::getline(f, refresh);   // optional 3rd line (older files won't have it)
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
        };
        trim(user); trim(token); trim(refresh);
        if (user.empty() || token.empty()) return false;
        // Older sessions may have been saved with the email as the display
        // name; show only the local-part so the UI never shows a raw email.
        if (provider_ == Provider::Supabase) user = PublicName(user);
        out = Session{};
        out.loggedIn = true;
        out.username = user;
        out.token    = token;
        out.refreshToken = refresh;
        return true;
    }

    void LoadSession() {
        Session disk;
        if (ReadSessionFile(disk)) session_ = disk;
    }

    void SaveSession() {   // whole-file and atomic: the other program never reads half a session
        detail::WriteFileAtomic(SessionPath(), session_.username + "\n" + session_.token + "\n" + session_.refreshToken + "\n");
    }

    /// Our sign-in was rejected and could not be renewed. When session.txt still holds it (or nothing), sign out (the file
    /// goes) and return true. When another program has saved a different sign-in there meanwhile, take that one instead,
    /// leave the file alone and return false, so the editor never signs the launcher out (or the other way round).
    bool ForgetSessionUnlessNewerOnDisk() {
        Session disk;
        if (ReadSessionFile(disk) && disk.token != session_.token) {
            session_ = disk;
            return false;
        }
        Logout();
        return true;
    }

    /// The access token on disk is newer than ours and not about to expire: another program refreshed (Supabase refresh
    /// tokens work once, so ours is spent). Takes it and returns true.
    static constexpr int kSpentWaitMs = 3000;   // how long a refused refresh waits for the other program's new pair
    /// Supabase's answer to a refresh token that was used already ("Invalid Refresh Token: Already Used",
    /// error_code refresh_token_already_used).
    static bool RefreshTokenSpent(const std::string& body) {
        return body.find("Already Used") != std::string::npos || body.find("already_used") != std::string::npos;
    }
    bool AdoptFreshTokenFromDisk() {
        Session disk;
        if (!ReadSessionFile(disk) || disk.token == session_.token) return false;
        const long long exp = std::atoll(JwtClaim(disk.token, "exp").c_str());
        if (exp <= 0 || exp - (long long)std::time(nullptr) <= 30) return false;
        session_ = disk;
        return true;
    }

    Result Succeed(const std::string& username, const std::string& token,
                   const std::string& refresh = {}) {
        session_.loggedIn = true;
        session_.username = username;
        session_.token    = token;
        session_.refreshToken = refresh;
        SaveSession();
        Result r; r.ok = true; r.session = session_; return r;
    }

    // Renew the Supabase access token from the stored refresh token. Returns
    // true and updates the session on success. Used by VerifySession so a
    // signed-in player isn't kicked out just because the 1-hour token expired.
    bool RefreshSupabaseToken() {
        if (provider_ != Provider::Supabase || session_.refreshToken.empty() || !IsOnline())
            return false;
        // Another program (the launcher or the editor) may have refreshed already: then our refresh token is spent, and
        // using it again fails (or, with Supabase's reuse detection, ends the whole sign-in). Take theirs instead.
        if (AdoptFreshTokenFromDisk()) return true;
        const okay::backend::Result<okay::backend::Session> renewed = Sb().Refresh(session_.refreshToken);
        const ApiResponse& r = renewed.response;
        if (!r.ok) {
            if (AdoptFreshTokenFromDisk()) return true;   // the other program won the race a moment ago
            // Refused because the token was already used: the other program's refresh reached the server first and its new
            // pair is on its way to session.txt. Wait a short, bounded time for it instead of signing out (which showed the
            // editor's sign-in window while the launcher was signed in; owner bug 2026-11-25).
            if (r.reached && RefreshTokenSpent(r.body)) {
                for (int waited = 0; waited < kSpentWaitMs; waited += 50) {
                    detail::NapMs(50);
                    if (AdoptFreshTokenFromDisk()) return true;
                }
            }
            return false;
        }
        if (!renewed.ok) return false;   // (an answer with no access token)
        session_.token = renewed.value.accessToken;
        if (!renewed.value.refreshToken.empty()) session_.refreshToken = renewed.value.refreshToken;   // Supabase rotates refresh tokens
        SaveSession();
        return true;
    }

    // ---- local backend -------------------------------------------------
    // Each record is one line: "username<TAB>saltHex<TAB>hashHex".
    struct Record { std::string user, salt, hash; };

    std::vector<Record> ReadDb() const {
        std::vector<Record> out;
        std::ifstream f(DbPath());
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::size_t t1 = line.find('\t');
            std::size_t t2 = line.find('\t', t1 == std::string::npos ? t1 : t1 + 1);
            if (t1 == std::string::npos || t2 == std::string::npos) continue;
            Record r;
            r.user = line.substr(0, t1);
            r.salt = line.substr(t1 + 1, t2 - t1 - 1);
            r.hash = line.substr(t2 + 1);
            while (!r.hash.empty() && (r.hash.back() == '\r' || r.hash.back() == '\n'))
                r.hash.pop_back();
            out.push_back(std::move(r));
        }
        return out;
    }

    void WriteDb(const std::vector<Record>& records) const {
        std::ofstream f(DbPath(), std::ios::trunc);
        for (const auto& r : records)
            f << r.user << '\t' << r.salt << '\t' << r.hash << '\n';
        f.close();
        std::error_code ec;   // the hashes are for this user's eyes only
        fs::permissions(DbPath(), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    }

    Result LocalRegister(const std::string& username, const std::string& password) {
        auto records = ReadDb();
        std::string key = Lower(username);
        for (const auto& r : records)
            if (Lower(r.user) == key) return Fail("That username is already taken.");
        Record r;
        r.user = username;
        r.salt = detail::RandomHex(16);
        r.hash = detail::HashPasswordV2(password, r.salt);
        records.push_back(r);
        WriteDb(records);
        return Succeed(username, detail::RandomHex(24));
    }

    Result LocalLogin(const std::string& username, const std::string& password) {
        auto records = ReadDb();
        std::string key = Lower(username);
        for (auto& r : records) {
            if (Lower(r.user) != key) continue;
            bool good = false, upgrade = false;
            std::uint32_t rounds = 0;
            if (detail::StoredRounds(r.hash, rounds)) {                   // the current format
                good = detail::SecureEquals(detail::HashPasswordV2(password, r.salt, rounds), r.hash);
                upgrade = good && rounds < detail::kPasswordRounds;      // made when the cost was lower
            } else if (!detail::IsHashV2(r.hash)) {                      // an older version's SHA-256 chain
                good = detail::SecureEquals(detail::HashPassword(password, r.salt), r.hash);
                upgrade = good;
            }                                                            // (an unreadable v2 record never matches)
            if (!good) return Fail("Incorrect password.");
            if (upgrade) {                                               // transparent: the password is in hand, store it the new way
                r.salt = detail::RandomHex(16);
                r.hash = detail::HashPasswordV2(password, r.salt);
                WriteDb(records);
            }
            return Succeed(r.user, detail::RandomHex(24));
        }
        return Fail("No account found with that username.");
    }

    // ---- remote backend ------------------------------------------------
    // POST {"username":..,"password":..} to <serverUrl>/<action>; a 2xx
    // response is expected to contain a "token" field.
    Result RemoteAuth(const std::string& action, const std::string& username,
                      const std::string& password) {
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += "/" + action;

        std::string body = "{\"username\":\"" + detail::JsonEscape(username) +
                           "\",\"password\":\"" + detail::JsonEscape(password) + "\"}";
        // No bearer token yet — this is how the player gets one.
        ApiResponse r = HttpRequest("POST", url, body, /*authToken=*/{});
        if (!r.reached && action == "login") r = RetrySignIn("POST", url, body, r);

        if (!r.reached)
            return Fail(detail::NotReachedMessage("Couldn't reach the account server (offline or unreachable).", r.error));
        if (!r.ok) {
            std::string serverErr = detail::JsonField(r.body, "error");
            return Fail(serverErr.empty()
                ? ("Account server rejected the request (HTTP " + std::to_string(r.status) + ").")
                : serverErr);
        }
        std::string token = detail::JsonField(r.body, "token");
        if (token.empty()) return Fail("Server response was missing a session token.");
        return Succeed(username, token);
    }

    // ---- managed backend: Supabase (OkayBackend's Supabase provider, okay/Backend/Supabase.hpp) ----------
    // Sign up, or sign in with the email; the provider sends the requests and reads the answer (the name to show, the
    // access and refresh tokens, or the message for the person).
    Result SupabaseAuth(bool signUp, const std::string& email, const std::string& password,
                        const std::string& username = {}) {
        const okay::backend::Result<okay::backend::Session> r =
            signUp ? Sb().SignUp(email, password, username) : Sb().SignIn(email, password);
        if (!r.ok) return Fail(r.error);
        return Succeed(r.value.username, r.value.accessToken, r.value.refreshToken);
    }

    // A sign-in to the custom server that got no answer is sent once more (the Supabase provider does the same for its
    // own sign-in). Unlike most POSTs that is safe: signing in twice only makes a second sign-in, nothing happens twice to the account. It covers a kept-alive
    // connection the server closed while the sign-in window sat idle (WinHTTP then fails the send or the receive and,
    // because the request may have gone out, the seam does not hand a POST to curl). `first` is the failed answer; when
    // the second try also fails, the first reason is kept unless the second one says more.
    ApiResponse RetrySignIn(const std::string& method, const std::string& url, const std::string& body,
                            const ApiResponse& first) const {
        ApiResponse again = HttpRequest(method, url, body, /*authToken=*/{});
        if (!again.reached && again.error.empty()) again.error = first.error;
        return again;
    }

    // Perform one HTTP request through the HTTP seam (okay::http::Send, NativeFetch.hpp): WinHTTP on Windows, our own
    // client on Linux; no outside program, so secrets (the auth header, the body) never leave this process. Any status
    // is an answer (no curl -f), so the server's {"error": ...} message survives. A request is never sent twice.
    ApiResponse HttpRequest(const std::string& method, const std::string& url,
                            const std::string& jsonBody,
                            const std::string& authToken,
                            const std::string& extraHeader = {},
                            const std::string& bodyFile = {},     // raw file body (uploads)
                            const std::string& bodyType = {},
                            int timeoutSec = 0) const {
        // The header lines (apikey, bearer token, `extraHeader` split one per line, Content-Type) are built by the backend's
        // one request builder; the transport keeps the seam's reason when there is no answer (no secrets in it).
        return transport_.Send(okay::backend::supabase::MakeRequest(apiKey_, method, url, jsonBody, authToken, extraHeader,
                                                                    bodyFile, bodyType, timeoutSec));
    }
};

/// Best-effort per-user config directory for launcher data: %APPDATA%/OkaySpace
/// on Windows, $XDG_CONFIG_HOME or ~/.config/OkaySpace elsewhere, falling back
/// to `fallbackBesideExe` if no home directory is known.
inline fs::path DefaultConfigDir(const fs::path& fallbackBesideExe) {
#if defined(OKAY_XBOX_UWP)
    { const std::string ad = okay::Paths::Env::Current().appData; if (!ad.empty()) return fs::path(ad) / "OkaySpace"; }   // ApplicationData, set by the UWP shell
#elif defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
        return fs::path(appdata) / "OkaySpace";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
        return fs::path(xdg) / "OkaySpace";
    if (const char* home = std::getenv("HOME"))
        return fs::path(home) / ".config" / "OkaySpace";
#endif
    return fallbackBesideExe;
}

} // namespace okay::account
