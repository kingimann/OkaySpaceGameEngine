// OkayBackend: the Supabase provider, the production backend (docs/platform/okay_backend.md, sections 5 and 6).
//
// It sends exactly the requests the programs always sent: the code below was moved out of AccountService (OkayBackend
// round 2), and tests/test_okay_backend_golden_auth.cpp checks the bytes (method, address, header lines in order, body)
// against texts recorded from AccountService before the move.
//
// Written from Supabase's publicly documented HTTP routes for its Auth service (GoTrue): POST /auth/v1/signup,
// POST /auth/v1/token?grant_type=password and ?grant_type=refresh_token, GET and PUT /auth/v1/user, POST /auth/v1/recover,
// /auth/v1/verify and /auth/v1/resend, with the project's publishable key in the "apikey" header and the access token as
// "Authorization: Bearer"; and from our own SQL (docs/sql/social.sql, email_for_login). These routes are an interface we
// call; no Supabase client library or Supabase source code is used.
#pragma once

#include "okay/Backend/Auth.hpp"
#include "okay/Backend/Transport.hpp"
#include "okay/Platform/Account/AccountDetail.hpp"   // detail::JsonEscape / JsonField / PercentEncode / NotReachedMessage, PublicName

#include <string>
#include <utility>
#include <vector>

namespace okay::backend::supabase {

/// Extra header lines, one per line ("x-upsert: true\ncache-control: max-age=30"). Blank lines and stray quotes are dropped.
inline std::vector<std::string> SplitHeaders(const std::string& extra) {   // dup-allow: moved unchanged from AccountService.hpp; header lines, not a general split
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i <= extra.size(); ++i) {
        const char c = i < extra.size() ? extra[i] : '\n';
        if (c == '\n' || c == '\r') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else if (c != '"') cur += c;
    }
    return out;
}

/// One request with the header lines in the order the programs have always sent them: the publishable key ("apikey", when
/// there is one), the access token ("Authorization: Bearer", when there is one), the caller's extra lines (for example
/// PostgREST's "Prefer: ..."), then the body's Content-Type (the file's type for an upload, JSON for a body).
inline Request MakeRequest(const std::string& apiKey, const std::string& method, const std::string& url, const std::string& jsonBody,
                           const std::string& accessToken, const std::string& extraHeader = {}, const std::string& bodyFile = {},
                           const std::string& bodyType = {}, int timeoutSec = 0) {
    Request r;
    r.method = method;
    r.url = url;
    if (!apiKey.empty())      r.headers.push_back("apikey: " + apiKey);
    if (!accessToken.empty()) r.headers.push_back("Authorization: Bearer " + accessToken);
    for (const std::string& h : SplitHeaders(extraHeader)) r.headers.push_back(h);
    if (!bodyFile.empty())      r.headers.push_back("Content-Type: " + bodyType);
    else if (!jsonBody.empty()) r.headers.push_back("Content-Type: application/json");
    r.body = jsonBody;
    r.bodyFile = bodyFile;
    r.timeoutSec = timeoutSec;
    return r;
}

/// The Auth calls on Supabase. Holds the address, the publishable key and the transport it sends through (the transport
/// must outlive it); cheap to make per call.
class Auth final : public okay::backend::Auth {
public:
    Auth(std::string projectUrl, std::string apiKey, Transport& transport)
        : url_(std::move(projectUrl)), base_(url_), key_(std::move(apiKey)), t_(transport) {
        if (!base_.empty() && base_.back() == '/') base_.pop_back();   // the project address without one trailing "/"
    }

    /// The recover address for `base` (+ the redirect, only when it is an http(s) address). Pure, so it is unit tested.
    static std::string RecoverUrl(std::string base, const std::string& redirect) {
        if (!base.empty() && base.back() == '/') base.pop_back();
        std::string u = base + "/auth/v1/recover";
        if (redirect.rfind("http", 0) == 0) u += "?redirect_to=" + okay::account::detail::PercentEncode(redirect);
        return u;
    }
    /// The body of POST auth/v1/verify that trades an emailed reset code for a session. Pure, so it is unit tested.
    static std::string RecoveryVerifyBody(const std::string& email, const std::string& code) {
        return "{\"type\":\"recovery\",\"email\":\"" + okay::account::detail::JsonEscape(email) + "\",\"token\":\"" +
               okay::account::detail::JsonEscape(code) + "\"}";
    }

    Result<Session> SignUp(const std::string& email, const std::string& password, const std::string& username) override {
        std::string body = Credentials(email, password);
        if (!username.empty())   // the name to show, kept in the user's metadata
            body += ",\"data\":{\"username\":\"" + okay::account::detail::JsonEscape(username) + "\"}";
        body += "}";
        return SessionFrom(Send("POST", base_ + "/auth/v1/signup", body), true, email);
    }

    Result<Session> SignIn(const std::string& emailOrUsername, const std::string& password) override {
        // A username (no "@") is turned into the account's email by a SECURITY DEFINER function that only answers when the
        // password is right (docs/sql/social.sql), so it gives away no emails. AccountService only signs in with an email.
        std::string loginId = emailOrUsername;
        if (loginId.find('@') == std::string::npos) {
            const std::string rpcUrl = base_ + "/rest/v1/rpc/email_for_login";
            const std::string rpcBody = "{\"p_username\":\"" + okay::account::detail::JsonEscape(loginId) +
                "\",\"p_password\":\"" + okay::account::detail::JsonEscape(password) + "\"}";
            Response rr = Send("POST", rpcUrl, rpcBody);
            if (!rr.reached) rr = SendAgain("POST", rpcUrl, rpcBody, rr);   // a read: safe to ask again
            if (!rr.reached) return Fail(NotReached(rr.error), rr);
            std::string resolved;   // the function answers a JSON string (the email) or null
            if (rr.ok) {
                const std::size_t q = rr.body.find('"');
                if (q != std::string::npos) {
                    const std::size_t e = rr.body.find('"', q + 1);
                    if (e != std::string::npos) resolved = rr.body.substr(q + 1, e - q - 1);
                }
            }
            if (resolved.empty()) return Fail("Invalid username/email or password.", rr);
            loginId = resolved;
        }
        const std::string url = base_ + "/auth/v1/token?grant_type=password";
        const std::string body = Credentials(loginId, password) + "}";
        Response r = Send("POST", url, body);
        if (!r.reached) r = SendAgain("POST", url, body, r);
        return SessionFrom(std::move(r), false, loginId);
    }

    Result<Session> Refresh(const std::string& refreshToken) override {
        Result<Session> out;
        out.response = Send("POST", base_ + "/auth/v1/token?grant_type=refresh_token",
                            "{\"refresh_token\":\"" + okay::account::detail::JsonEscape(refreshToken) + "\"}");
        if (!out.response.ok) { out.error = "The session could not be renewed."; return out; }
        out.value.accessToken = okay::account::detail::JsonField(out.response.body, "access_token");
        if (out.value.accessToken.empty()) { out.error = "The answer had no access token."; return out; }
        out.value.refreshToken = okay::account::detail::JsonField(out.response.body, "refresh_token");   // rotated; may be absent
        out.ok = true;
        return out;
    }

    void SignOut(const std::string&) override {}   // nothing is sent (as before): the caller forgets the session

    Response User(const std::string& accessToken) override { return Send("GET", base_ + "/auth/v1/user", {}, accessToken); }
    Response UpdateUser(const std::string& accessToken, const std::string& json) override {
        return Send("PUT", base_ + "/auth/v1/user", json, accessToken);
    }
    Response Recover(const std::string& email, const std::string& redirect) override {
        return Send("POST", RecoverUrl(url_, redirect), "{\"email\":\"" + okay::account::detail::JsonEscape(email) + "\"}");
    }
    Response VerifyRecovery(const std::string& email, const std::string& code) override {
        return Send("POST", base_ + "/auth/v1/verify", RecoveryVerifyBody(email, code));
    }
    Response Resend(const std::string& email) override {
        return Send("POST", base_ + "/auth/v1/resend", "{\"type\":\"signup\",\"email\":\"" + okay::account::detail::JsonEscape(email) + "\"}");
    }

private:
    std::string url_, base_, key_;
    Transport& t_;

    Response Send(const std::string& method, const std::string& url, const std::string& body, const std::string& token = {}) {
        return t_.Send(MakeRequest(key_, method, url, body, token));
    }
    // A sign-in (the password grant, or the username lookup) that got no answer is sent once more. Unlike most POSTs that is
    // safe: signing in twice only makes a second sign-in. It covers a kept-alive connection the server closed while the
    // sign-in window sat idle. When the second try gives no reason, the first one's is kept.
    Response SendAgain(const std::string& method, const std::string& url, const std::string& body, const Response& first) {
        Response again = Send(method, url, body);
        if (!again.reached && again.error.empty()) again.error = first.error;
        return again;
    }
    static std::string Credentials(const std::string& email, const std::string& password) {
        return "{\"email\":\"" + okay::account::detail::JsonEscape(email) + "\",\"password\":\"" +
               okay::account::detail::JsonEscape(password) + "\"";
    }
    static std::string NotReached(const std::string& reason) {
        return okay::account::detail::NotReachedMessage("Couldn't reach the account server (offline or unreachable).", reason);
    }
    static Result<Session> Fail(std::string error, Response r) {
        Result<Session> out;
        out.error = std::move(error);
        out.response = std::move(r);
        return out;
    }
    /// The session in a sign-up or sign-in answer, or the message to show.
    static Result<Session> SessionFrom(Response r, bool signUp, const std::string& loginId) {
        if (!r.reached) {   // (the message first: the answer is moved into the result after)
            std::string why = NotReached(r.error);
            return Fail(std::move(why), std::move(r));
        }
        if (!r.ok) {   // Supabase reports errors as error_description / msg / message / error
            std::string e = okay::account::detail::JsonField(r.body, "error_description");
            if (e.empty()) e = okay::account::detail::JsonField(r.body, "msg");
            if (e.empty()) e = okay::account::detail::JsonField(r.body, "message");
            if (e.empty()) e = okay::account::detail::JsonField(r.body, "error");
            if (e.empty()) e = "Account server rejected the request (HTTP " + std::to_string(r.status) + ").";
            return Fail(std::move(e), std::move(r));
        }
        Result<Session> out;
        out.value.accessToken = okay::account::detail::JsonField(r.body, "access_token");
        if (out.value.accessToken.empty()) {   // sign-up with email confirmation on gives no session yet
            return Fail(signUp ? "Account created — check your email to confirm, then sign in. "
                                 "(Turn off email confirmation in Supabase for instant sign-in.)"
                               : "Server response was missing an access token.", std::move(r));
        }
        // The name to show: the username kept with the account, else the email's part before the "@" (never the email).
        std::string display = okay::account::detail::JsonField(r.body, "username");
        if (display.empty()) display = loginId;
        out.value.username = okay::account::PublicName(display);
        out.value.refreshToken = okay::account::detail::JsonField(r.body, "refresh_token");
        out.response = std::move(r);
        out.ok = true;
        return out;
    }
};

} // namespace okay::backend::supabase
