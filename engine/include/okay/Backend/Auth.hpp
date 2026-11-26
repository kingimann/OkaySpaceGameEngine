// OkayBackend Auth: signing people up and in, renewing and checking tokens, password reset and account changes
// (docs/platform/okay_backend.md, section 4). The calls are the same on every provider; the Supabase provider
// (okay/Backend/Supabase.hpp) sends today's requests. AccountService (okay/Platform/Account/AccountService.hpp) keeps the
// session file, the "only the launcher refreshes" rule and the messages around these calls.
//
// Written from the publicly documented behaviour of a token-based auth service (an access token plus a one-time refresh
// token) and RFC 7519 (JSON web tokens: a token is three base64url parts; the middle one is the JSON claims).
#pragma once

#include "okay/Backend/Transport.hpp"

#include <cctype>
#include <string>

namespace okay::backend {

/// The outcome of a call that makes a session. `error` is the text shown to the person when `ok` is false; `response` is
/// the last answer the provider received (for callers that look at the status or body, such as a refresh that was refused
/// because the token was already used).
template <class T>
struct Result {
    bool        ok = false;
    std::string error;
    T           value{};
    Response    response;
};

/// A signed-in session as the provider handed it out: the name to show (never an email), the access token and the
/// refresh token (Supabase hands out a new one on every refresh; the old one stops working).
struct Session {
    std::string username;
    std::string accessToken;
    std::string refreshToken;
};

/// A claim from a JSON web token's payload ("sub", "exp", ...), read without checking the signature: the programs only
/// read their own token's id and expiry. A string claim comes back without quotes, a number as its digits, "" when absent.
inline std::string TokenClaim(const std::string& jwt, const std::string& key) {
    const std::size_t a = jwt.find('.'), b = a == std::string::npos ? a : jwt.find('.', a + 1);
    if (b == std::string::npos) return "";
    const std::string part = jwt.substr(a + 1, b - a - 1);
    std::string json; unsigned acc = 0; int bits = 0;
    for (char c : part) {
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
              : (c == '-' || c == '+') ? 62 : (c == '_' || c == '/') ? 63 : -1;
        if (v < 0) continue;
        acc = (acc << 6) | (unsigned)v; bits += 6;
        if (bits >= 8) { bits -= 8; json += (char)((acc >> bits) & 0xFF); }
    }
    const std::string k = "\"" + key + "\"";
    std::size_t p = json.find(k); if (p == std::string::npos) return "";
    p = json.find(':', p + k.size()); if (p == std::string::npos) return "";
    ++p; while (p < json.size() && json[p] == ' ') ++p;
    if (p < json.size() && json[p] == '"') { const std::size_t e = json.find('"', p + 1); return e == std::string::npos ? "" : json.substr(p + 1, e - p - 1); }
    std::size_t e = p; while (e < json.size() && (std::isdigit((unsigned char)json[e]) || json[e] == '-')) ++e;
    return json.substr(p, e - p);
}

/// The Auth calls. Tokens are passed in: the provider keeps no session of its own (AccountService owns it, because the
/// launcher and the editor share one session file).
class Auth {
public:
    virtual ~Auth() = default;
    /// Make an account. `username` (may be empty) is the name to show, kept with the account.
    virtual Result<Session> SignUp(const std::string& email, const std::string& password, const std::string& username) = 0;
    /// Sign in with an email, or with a username that the provider turns into the account's email first.
    virtual Result<Session> SignIn(const std::string& emailOrUsername, const std::string& password) = 0;
    /// Trade a refresh token for a new access token (and, on Supabase, a new refresh token).
    virtual Result<Session> Refresh(const std::string& refreshToken) = 0;
    /// Sign out. The Supabase provider sends nothing (the programs never have: the session is forgotten on this computer).
    virtual void SignOut(const std::string& accessToken) = 0;
    /// Who the token belongs to ({"id": ...} on success). Also how a game host checks a joining player's token.
    virtual Response User(const std::string& accessToken) = 0;
    /// Change the signed-in account: `json` is the change ({"password": ...}, {"email": ...}, {"data": {...}}).
    virtual Response UpdateUser(const std::string& accessToken, const std::string& json) = 0;
    /// Ask for a password-reset email. `redirect` (an http(s) address, or empty) is where the emailed link should land.
    virtual Response Recover(const std::string& email, const std::string& redirect) = 0;
    /// Trade the code from a reset email for a short session (its access token is in the answer).
    virtual Response VerifyRecovery(const std::string& email, const std::string& code) = 0;
    /// Send the sign-up confirmation email again.
    virtual Response Resend(const std::string& email) = 0;
};

} // namespace okay::backend
