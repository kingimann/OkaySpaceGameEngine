// OkayBackend: the Supabase provider, the production backend (docs/platform/okay_backend.md, sections 5 and 6).
//
// It sends exactly the requests the programs always sent: the code below was moved out of AccountService (OkayBackend
// round 2), and tests/test_okay_backend_golden_auth.cpp checks the bytes (method, address, header lines in order, body)
// against texts recorded from AccountService before the move.
//
// The Database part (OkayBackend round 3) sends PostgREST requests under /rest/v1/ (tables) and /rest/v1/rpc/ (functions);
// tests/test_okay_backend_golden_db.cpp checks them against texts recorded from the call sites before they moved.
//
// The Storage and Functions parts (OkayBackend round 4) send Supabase Storage's object routes and Edge Functions' route
// (okay/Backend/SupabaseRoutes.hpp); tests/test_okay_backend_golden_storage.cpp checks them the same way.
//
// Written from Supabase's publicly documented HTTP routes for its Auth service (GoTrue): POST /auth/v1/signup,
// POST /auth/v1/token?grant_type=password and ?grant_type=refresh_token, GET and PUT /auth/v1/user, POST /auth/v1/recover,
// /auth/v1/verify and /auth/v1/resend, with the project's publishable key in the "apikey" header and the access token as
// "Authorization: Bearer"; and from our own SQL (docs/sql/social.sql, email_for_login). These routes are an interface we
// call; no Supabase client library or Supabase source code is used.
#pragma once

#include "okay/Backend/Auth.hpp"
#include "okay/Backend/Database.hpp"
#include "okay/Backend/Functions.hpp"
#include "okay/Backend/Storage.hpp"
#include "okay/Backend/SupabaseRoutes.hpp"
#include "okay/Backend/Transport.hpp"
#include "okay/Platform/Account/AccountDetail.hpp"   // detail::JsonEscape / JsonField / PercentEncode / NotReachedMessage, PublicName

#include <functional>
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

/// The Database calls on Supabase (PostgREST): a table request goes to "/rest/v1/<table>?<query>", a function to
/// "/rest/v1/rpc/<name>" as a POST. How it is sent is up to the sender: AccountService passes its own Api (the signed-in
/// token, the "online and signed in" check, and the re-read of a newer saved token after a 401) or ApiAnon (the publishable
/// key only); a program with no account service passes the address, key, token and transport.
/// How a provider part sends when it is given the address, key, token and transport directly (no account service): one request
/// through MakeRequest's header order to `transport`, which must outlive it. `accessToken` "" sends the publishable key only.
struct DirectSend {
    std::string base, key, token;   // base: the project address without one trailing "/"
    Transport* transport = nullptr;
    DirectSend(std::string projectUrl, std::string apiKey, Transport& t, std::string accessToken)
        : base(std::move(projectUrl)), key(std::move(apiKey)), token(std::move(accessToken)), transport(&t) {
        if (!base.empty() && base.back() == '/') base.pop_back();
    }
    Response operator()(const std::string& method, const std::string& path, const std::string& body, const std::string& extra,
                        const std::string& bodyFile = {}, const std::string& bodyType = {}, int timeoutSec = 0) const {
        return transport->Send(MakeRequest(key, method, base + path, body, token, extra, bodyFile, bodyType, timeoutSec));
    }
};

class Database final : public okay::backend::Database {
public:
    /// (method, path from the project address such as "/rest/v1/cloud_saves?select=key", body, extra header lines)
    using Sender = std::function<Response(const std::string&, const std::string&, const std::string&, const std::string&)>;

    explicit Database(Sender send) : send_(std::move(send)) {}
    /// Straight to `transport` with MakeRequest's header order. `accessToken` "" sends the publishable key only.
    Database(std::string projectUrl, std::string apiKey, Transport& transport, std::string accessToken = {}) {
        send_ = DirectSend(std::move(projectUrl), std::move(apiKey), transport, std::move(accessToken));
    }

    /// The path of a table request (pure, for tests).
    static std::string TablePath(const DbRequest& r) { return "/rest/v1/" + r.path; }
    /// The path of a function call (pure, for tests). The name is written like a table name (Query::Name).
    static std::string RpcPath(const std::string& function) { return "/rest/v1/rpc/" + Query::Name(function); }

    Response Send(const DbRequest& r) override { return send_(r.method, TablePath(r), r.body, r.headers); }
    Response Rpc(const std::string& function, const std::string& json) override {
        return send_("POST", RpcPath(function), json, {});
    }

private:
    Sender send_;
};

/// The Storage calls on Supabase: an object at "/storage/v1/object/<bucket>/<path>", its public read under
/// "/storage/v1/object/public/", a listing at "/storage/v1/object/list/<bucket>" (POST with a JSON body), several objects removed
/// at "/storage/v1/object/<bucket>" (DELETE with {"prefixes":[...]}). Like Database, how it is sent is up to the sender:
/// AccountService passes its Api (requests) and ApiUpload (uploads: the signed-in token, the file as the body); a program
/// with no account service passes the address, key, token and transport.
class Storage final : public okay::backend::Storage {
public:
    /// (method, path from the project address, body, extra header lines): Database's sender.
    using Sender = Database::Sender;
    /// (path from the project address, local file, content type, extra header lines): sends the file as a POST body.
    using Uploader = std::function<Response(const std::string&, const std::string&, const std::string&, const std::string&)>;

    Storage(std::string projectUrl, Sender send, Uploader upload)
        : base_(std::move(projectUrl)), send_(std::move(send)), upload_(std::move(upload)) {
        if (!base_.empty() && base_.back() == '/') base_.pop_back();   // the project address without one trailing "/"
    }
    /// Straight to `transport` with MakeRequest's header order. `accessToken` "" sends the publishable key only.
    Storage(std::string projectUrl, std::string apiKey, Transport& transport, std::string accessToken = {}) {
        const DirectSend d(std::move(projectUrl), std::move(apiKey), transport, std::move(accessToken));
        base_ = d.base;
        send_ = d;
        upload_ = [d](const std::string& path, const std::string& file, const std::string& type, const std::string& extra) {
            return d("POST", path, {}, extra, file, type.empty() ? "application/octet-stream" : type);
        };
    }

    /// The extra header lines of an upload (pure, for tests): "x-upsert: true" and "cache-control: max-age=N", one per line.
    static std::string UploadHeaders(const UploadOptions& o) {
        std::string h = o.upsert ? "x-upsert: true" : "";
        if (o.cacheSeconds >= 0) h += (h.empty() ? "" : "\n") + std::string("cache-control: max-age=") + std::to_string(o.cacheSeconds);
        return h;
    }
    /// The body of a listing (pure, for tests): {"prefix":...,"limit":N,"offset":N,"sortBy":{"column":...,"order":"asc"|"desc"}}.
    static std::string ListBody(const ListOptions& o) {
        return "{\"prefix\":\"" + okay::account::detail::JsonEscape(o.prefix) + "\",\"limit\":" + std::to_string(o.limit) +
               ",\"offset\":" + std::to_string(o.offset) + ",\"sortBy\":{\"column\":\"" + okay::account::detail::JsonEscape(o.sortColumn) +
               "\",\"order\":\"" + (o.descending ? "desc" : "asc") + "\"}}";
    }
    /// The body of a removal of several objects (pure, for tests): {"prefixes":["a","b"]}.
    static std::string RemoveBody(const std::vector<std::string>& paths) {
        std::string body = "{\"prefixes\":[";
        for (std::size_t i = 0; i < paths.size(); ++i) body += (i ? ",\"" : "\"") + okay::account::detail::JsonEscape(paths[i]) + "\"";
        return body + "]}";
    }

    Response Upload(const std::string& bucket, const std::string& path, const std::string& localFile, const std::string& contentType,
                    const UploadOptions& options = {}) override {
        return upload_(routes::StorageObject(bucket, path), localFile, contentType, UploadHeaders(options));
    }
    Response ReadPublic(const std::string& bucket, const std::string& path, const std::string& query = {}) override {
        return send_("GET", routes::StoragePublic(bucket, path) + (query.empty() ? std::string() : "?" + query), {}, {});
    }
    Response List(const std::string& bucket, const ListOptions& options) override {
        return send_("POST", routes::StorageList(bucket), ListBody(options), {});
    }
    Response Remove(const std::string& bucket, const std::vector<std::string>& paths) override {
        return send_("DELETE", routes::StorageBucket(bucket), RemoveBody(paths), {});
    }
    Response RemoveObject(const std::string& bucket, const std::string& path) override {
        return send_("DELETE", routes::StorageObject(bucket, path), {}, {});
    }
    std::string PublicUrl(const std::string& bucket, const std::string& path) const override {
        return base_ + routes::StoragePublic(bucket, path);
    }

private:
    std::string base_;
    Sender send_;
    Uploader upload_;
};

/// The Functions calls on Supabase: POST "/functions/v1/<name>" with the JSON body. AccountService passes its ApiTimed (the
/// signed-in token, the re-read of a newer saved token after a 401, the time limit); a program with no account service passes
/// the address, key, token and transport.
class Functions final : public okay::backend::Functions {
public:
    /// (method, path from the project address, body, extra header lines, time limit in seconds; 0 = none)
    using Sender = std::function<Response(const std::string&, const std::string&, const std::string&, const std::string&, int)>;

    explicit Functions(Sender send) : send_(std::move(send)) {}
    /// Straight to `transport` with MakeRequest's header order.
    Functions(std::string projectUrl, std::string apiKey, Transport& transport, std::string accessToken = {}) {
        send_ = [d = DirectSend(std::move(projectUrl), std::move(apiKey), transport, std::move(accessToken))](
                    const std::string& method, const std::string& path, const std::string& body, const std::string& extra, int timeoutSec) {
            return d(method, path, body, extra, {}, {}, timeoutSec);
        };
    }

    using okay::backend::Functions::Invoke;
    Response Invoke(const std::string& name, const std::string& json, int timeoutSec = 0) override {
        return send_("POST", routes::Function(name), json, {}, timeoutSec);
    }

private:
    Sender send_;
};

} // namespace okay::backend::supabase
