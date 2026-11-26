// OkayBackend round 2: golden request tests for the Auth part (docs/platform/okay_backend.md, sections 6 and 10.1).
//
// Every sign-in, sign-up, refresh, session check, password reset and account change that AccountService sends is run
// against a fake server, and the exact requests (method, URL, header lines in order, body) and what the program made of
// the answer are compared with the GOLDEN texts below. The golden texts were recorded from AccountService BEFORE its auth
// calls moved onto okay::backend (run with OKAY_GOLDEN_PRINT=1 to print them again). Then the same flows are run through
// the Supabase provider directly (okay::backend::supabase::Auth over a RecordingTransport) and must send the same bytes.
// To record them again from the old header: put AccountService.hpp of commit b9f097c46 in <dir>/okay/Platform/Account/,
// build this file with -DOKAY_GOLDEN_OLD_HEADER -I<dir> -Iengine/include -Itests, and run it with OKAY_GOLDEN_PRINT=1.
//
// No request ever leaves the process: every call goes to the test transport. The address, key, emails, passwords and
// tokens are made up.
#include "test_framework.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Core/Base64.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace acc = okay::account;

static const char* kUrl = "https://backend.example.test/";   // trailing slash on purpose: every flow must drop it
static const char* kKey = "test-publishable-key-0000";

static std::string B64Url(const std::string& in) {
    return okay::base64::Encode(reinterpret_cast<const std::uint8_t*>(in.data()), in.size(), okay::base64::Alphabet::Url, false);
}
static std::string Jwt(const std::string& sub, long exp) {   // JWT-shaped, unsigned: the client only reads "sub" and "exp"
    return B64Url("{\"alg\":\"none\"}") + "." + B64Url("{\"sub\":\"" + sub + "\",\"exp\":" + std::to_string(exp) + "}") + ".sig";
}
static const long kFuture = 4102444800L;   // 2100-01-01
static std::string Tok(int n) { return Jwt("u" + std::to_string(n), kFuture); }

/// One request in the text form the golden texts use.
static std::string Describe(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                            const std::string& body) {
    std::string s = method + " " + url + "\n";
    for (const std::string& h : headers) s += h + "\n";
    return s + "\n" + body + "\n---\n";
}

/// The fake server: answers each request by its route, as the scenario sets it up.
struct Fake {
    std::string log;                                   // every request, Describe()d
    std::vector<okay::http::Result> queue;             // answers in order (the next one is used per request)
    std::function<void(const std::string& url)> before;   // runs before a request is answered (to change session.txt)
    okay::http::Result Answer(const std::string& m, const std::string& u, const std::vector<std::string>& h, const std::string& b) {
        log += Describe(m, u, h, b);
        if (before) before(u);
        if (queue.empty()) { okay::http::Result r; r.error = "test: no answer queued"; return r; }
        okay::http::Result r = queue.front();
        queue.erase(queue.begin());
        return r;
    }
};
static okay::http::Result Http(long status, const std::string& body) {
    okay::http::Result r; r.reached = true; r.status = status; r.body = body; return r;
}
static okay::http::Result NoAnswer(const std::string& why) { okay::http::Result r; r.error = why; return r; }
static std::string TokenAnswer(int n, const std::string& username) {
    return "{\"access_token\":\"" + Tok(n) + "\",\"refresh_token\":\"r" + std::to_string(n) + "\",\"user\":{\"user_metadata\":{\"username\":\"" +
           username + "\"}}}";
}

static std::string Outcome(const acc::Result& r) {
    return "=> ok=" + std::to_string(r.ok) + " error=[" + r.error + "] user=[" + r.session.username + "] token=[" +
           r.session.token + "] refresh=[" + r.session.refreshToken + "]\n";
}
static std::string SessionLine(const acc::AccountService& a) {
    const acc::Session& s = a.CurrentSession();
    return "session: in=" + std::to_string(s.loggedIn) + " user=[" + s.username + "] token=[" + s.token + "] refresh=[" + s.refreshToken + "]\n";
}

// ---- The scenarios, run on AccountService (its public API, as every program calls it) ------------------------------------
struct Scenario { const char* name; std::function<std::string(const fs::path& dir)> run; };

static void SaveSession(const fs::path& dir, const std::string& user, const std::string& token, const std::string& refresh) {
    fs::create_directories(dir);
    std::ofstream f(dir / "session.txt", std::ios::trunc | std::ios::binary);
    f << user << "\n" << token << "\n" << refresh << "\n";
}

static std::vector<Scenario> Scenarios() {
    std::vector<Scenario> v;
    auto make = [](const fs::path& dir, Fake& f) {
        auto a = std::make_unique<acc::AccountService>(dir, kUrl, kKey);
        a->SetTransportForTest([&f](const std::string& m, const std::string& u, const std::vector<std::string>& h, const std::string& b) {
            return f.Answer(m, u, h, b);
        });
        return a;
    };
    v.push_back({"signup_with_username", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, TokenAnswer(1, "alex"))};
        auto a = make(d, f);
        std::string out = Outcome(a->Register("alex@example.test", "pw-123456", "alex"));
        std::string seq = f.log;
        seq += out;
        seq += SessionLine(*a);
        return seq;
    }});
    v.push_back({"signup_needs_confirmation", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{\"id\":\"u2\",\"email\":\"sam@example.test\"}")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Register("sam@example.test", "pw \"quoted\" \\ 1", std::string{}));
        seq += SessionLine(*a);
        seq += f.log;
        return seq;
    }});
    v.push_back({"signup_refused", [=](const fs::path& d) {
        Fake f; f.queue = {Http(422, "{\"code\":422,\"msg\":\"User already registered\"}")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Register("alex@example.test", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_email", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, TokenAnswer(3, "alex"))};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex@example.test", "pw-123456"));
        seq += f.log;
        seq += SessionLine(*a);
        return seq;
    }});
    v.push_back({"signin_email_no_username_in_answer", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{\"access_token\":\"" + Tok(4) + "\",\"refresh_token\":\"r4\"}")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex.doe@example.test", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_username", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "\"alex@example.test\""), Http(200, TokenAnswer(5, "alex"))};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_username_wrong", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "null")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex", "wrong-pass"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_username_unreachable", [=](const fs::path& d) {
        Fake f; f.queue = {NoAnswer("connect: winhttp error 12029"), NoAnswer("")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_retry_once_then_ok", [=](const fs::path& d) {
        Fake f; f.queue = {NoAnswer("send: winhttp error 12030"), Http(200, TokenAnswer(6, "alex"))};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex@example.test", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_unreachable", [=](const fs::path& d) {
        Fake f; f.queue = {NoAnswer("connect: winhttp error 12029"), NoAnswer("curl exit code 6")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex@example.test", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_refused", [=](const fs::path& d) {
        Fake f; f.queue = {Http(400, "{\"error\":\"invalid_grant\",\"error_description\":\"Invalid login credentials\"}")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex@example.test", "wrong-pass"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_refused_no_text", [=](const fs::path& d) {
        Fake f; f.queue = {Http(500, "oops")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex@example.test", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"signin_answer_without_token", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{}")};
        auto a = make(d, f);
        std::string seq = Outcome(a->Login("alex@example.test", "pw-123456"));
        seq += f.log;
        return seq;
    }});
    v.push_back({"resend_confirmation", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{}"), Http(429, "{}"), NoAnswer("x")};
        auto a = make(d, f);
        std::string out = Outcome(a->ResendConfirmation("sam@example.test"));
        out += Outcome(a->ResendConfirmation("sam@example.test"));
        out += Outcome(a->ResendConfirmation("sam@example.test"));
        out += Outcome(a->ResendConfirmation("not-an-email"));
        out += f.log;
        return out;
    }});
    v.push_back({"password_reset_request", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{}"), Http(200, "{}"), Http(500, "{}")};
        auto a = make(d, f);
        std::string out = Outcome(a->RequestPasswordReset("sam@example.test"));
        a->SetResetRedirect("https://reset.example.test/page?x=1&y=a b");
        out += Outcome(a->RequestPasswordReset("sam@example.test"));
        a->SetResetRedirect("not-a-web-address");
        out += Outcome(a->RequestPasswordReset("sam@example.test"));
        out += "url: " + acc::AccountService::RecoverUrl("https://backend.example.test/", "https://r.example.test/?a=1") + "\n";
        out += f.log;
        return out;
    }});
    v.push_back({"password_reset_with_code", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{\"access_token\":\"" + Tok(7) + "\"}"), Http(200, "{}"),
                           Http(403, "{\"msg\":\"Token has expired or is invalid\"}"),
                           Http(200, "{\"access_token\":\"" + Tok(8) + "\"}"), Http(422, "{\"message\":\"New password should be different\"}")};
        auto a = make(d, f);
        std::string out = Outcome(a->ResetPasswordWithCode("sam@example.test", "123456", "new-pass-1"));
        out += Outcome(a->ResetPasswordWithCode("sam@example.test", "654321", "new-pass-1"));
        out += Outcome(a->ResetPasswordWithCode("sam@example.test", "1234567", "new-pass-1"));
        out += "body: " + acc::AccountService::RecoveryVerifyBody("a\"b@example.test", "12345678") + "\n";
        out += f.log;
        out += SessionLine(*a);
        return out;
    }});
    v.push_back({"change_password_email_username", [=](const fs::path& d) {
        SaveSession(d, "alex", Tok(10), "r10");
        Fake f; f.queue = {Http(200, "{}"), Http(422, "{\"msg\":\"Email address already in use\"}"), Http(200, "{}"), Http(400, "{}")};
        auto a = make(d, f);
        std::string out = Outcome(a->ChangePassword("new-pass-2"));
        out += Outcome(a->ChangeEmail("alex2@example.test"));
        out += Outcome(a->ChangeUsername("alex_2"));
        out += Outcome(a->ChangePassword("new-pass-3"));
        out += f.log;
        out += SessionLine(*a);
        return out;
    }});
    v.push_back({"change_after_401_takes_the_newer_saved_token", [=](const fs::path& d) {
        SaveSession(d, "alex", Tok(11), "r11");
        Fake f; f.queue = {Http(401, "{}"), Http(200, "{}")};
        bool once = false;
        f.before = [&](const std::string&) { if (!once) { once = true; SaveSession(d, "alex", Tok(12), "r12"); } };
        auto a = make(d, f);
        std::string seq = Outcome(a->ChangeUsername("alex_3"));
        seq += f.log;
        seq += SessionLine(*a);
        return seq;
    }});
    v.push_back({"check_session_valid", [=](const fs::path& d) {
        SaveSession(d, "alex", Tok(13), "r13");
        Fake f; f.queue = {Http(200, "{\"id\":\"u13\"}")};
        auto a = make(d, f);
        std::string seq = "check=";
        seq += std::to_string((int)a->CheckSession());
        seq += "\n";
        seq += f.log;
        seq += SessionLine(*a);
        return seq;
    }});
    v.push_back({"check_session_refreshes", [=](const fs::path& d) {
        SaveSession(d, "alex", Jwt("u14", 1000), "r14");
        Fake f; f.queue = {Http(401, "{}"), Http(200, TokenAnswer(15, "alex"))};
        auto a = make(d, f);
        std::string seq = "check=";
        seq += std::to_string((int)a->CheckSession());
        seq += "\n";
        seq += f.log;
        seq += SessionLine(*a);
        return seq;
    }});
    v.push_back({"check_session_rejected", [=](const fs::path& d) {
        SaveSession(d, "alex", Jwt("u16", 1000), "r16");
        Fake f; f.queue = {Http(403, "{}"), Http(400, "{\"error_description\":\"Invalid Refresh Token\"}")};
        auto a = make(d, f);
        std::string seq = "check=";
        seq += std::to_string((int)a->CheckSession());
        seq += "\n";
        seq += f.log;
        seq += SessionLine(*a);
        return seq;
    }});
    v.push_back({"check_session_unreachable_and_server_error", [=](const fs::path& d) {
        SaveSession(d, "alex", Tok(17), "r17");
        Fake f; f.queue = {NoAnswer("connect: winhttp error 12029"), Http(500, "{}")};
        auto a = make(d, f);
        std::string out = "check=" + std::to_string((int)a->CheckSession()) + "\n";
        out += "check=" + std::to_string((int)a->CheckSession()) + "\n";
        out += f.log;
        out += SessionLine(*a);
        return out;
    }});
    v.push_back({"refresh_if_expiring", [=](const fs::path& d) {
        SaveSession(d, "alex", Jwt("u18", 1000), "r18");
        Fake f; f.queue = {Http(200, "{\"access_token\":\"" + Tok(19) + "\"}")};
        auto a = make(d, f);
        std::string out = "refreshed=" + std::to_string(a->RefreshIfExpiring()) + "\n";
        out += "again=" + std::to_string(a->RefreshIfExpiring()) + "\n";   // fresh now: nothing sent
        out += f.log;
        out += SessionLine(*a);
        return out;
    }});
    v.push_back({"verify_token", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "{\"id\":\"u-9\",\"email\":\"x@example.test\"}"), Http(401, "{}")};
        auto a = make(d, f);
        std::string id = "?";
        std::string out;
        for (const char* t : {"other.token.x", "bad.token.y", ""}) {   // one statement per step: the id is read after the call
            const bool ok = a->VerifyToken(t, id);
            out += "verified=" + std::to_string(ok) + " id=[" + id + "]\n";
        }
        out += f.log;
        return out;
    }});
    // The transport under every other call site (Api, ApiAnon, ApiUpload, ApiTimed): the same header builder.
    v.push_back({"transport_api_calls", [=](const fs::path& d) {
        SaveSession(d, "alex", Tok(21), "r21");
        Fake f; f.queue = {Http(200, "[]"), Http(201, ""), Http(200, "[]"), Http(200, "{}"), Http(200, "{}"), Http(200, "[]")};
        auto a = make(d, f);
        std::string out;
        out += "get=" + std::to_string(a->Api("/rest/v1/cloud_saves?select=key").status) + "\n";
        out += "post=" + std::to_string(a->Api("rest/v1/leaderboards", "POST", "[{\"board\":\"b\"}]",
                                               "Prefer: resolution=merge-duplicates,return=minimal").status) + "\n";
        out += "anon=" + std::to_string(a->ApiAnon("/rest/v1/profiles?select=id&username=eq.x", "GET", {}, "Accept: \"application/json\"\n\nRange: 0-9").status) + "\n";
        out += "upload=" + std::to_string(a->ApiUpload("/storage/v1/object/chat/u21/a.png", (d / "missing.png").string(), "image/png",
                                                       "x-upsert: true\ncache-control: max-age=30").status) + "\n";
        out += "upload_default_type=" + std::to_string(a->ApiUpload("/storage/v1/object/chat/u21/b.bin", (d / "b.bin").string(), "").status) + "\n";
        out += "timed=" + std::to_string(a->ApiTimed("/functions/v1/session-ticket", "POST", "{}", {}, 5).status) + "\n";
        out += "available=" + std::to_string(a->UsernameAvailable("al ex")) + "\n";
        out += f.log;
        return out;
    }});
    v.push_back({"transport_signed_out_sends_nothing", [=](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[]")};
        auto a = make(d, f);
        std::string out = "api_reached=" + std::to_string(a->Api("/rest/v1/cloud_saves").reached) + "\n";
        out += "cloud=" + std::to_string(a->CloudSave("slot1", "x")) + "\n";
        out += f.log;
        return out;
    }});
    v.push_back({"sign_out_sends_nothing", [=](const fs::path& d) {
        SaveSession(d, "alex", Tok(20), "r20");
        Fake f;
        auto a = make(d, f);
        a->Logout();
        std::string seq = f.log;
        seq += SessionLine(*a);
        seq += "file=";
        seq += std::to_string(fs::exists(d / "session.txt"));
        seq += "\n";
        return seq;
    }});
    return v;
}


#if !defined(OKAY_GOLDEN_OLD_HEADER)   // (recording from the old AccountService.hpp: it has no okay::backend to call)
// ---- The same flows through the Supabase provider directly (okay::backend::supabase::Auth over a RecordingTransport) -----
/// Only the request blocks of a golden text, in order.
static std::string RequestsOf(const std::string& golden) {
    std::string out;
    std::size_t i = 0;
    bool in = false;
    while (i < golden.size()) {
        std::size_t e = golden.find('\n', i);
        if (e == std::string::npos) e = golden.size();
        const std::string line = golden.substr(i, e - i);
        if (!in && (line.rfind("GET ", 0) == 0 || line.rfind("POST ", 0) == 0 || line.rfind("PUT ", 0) == 0 ||
                    line.rfind("DELETE ", 0) == 0 || line.rfind("PATCH ", 0) == 0)) in = true;
        if (in) out += line + "\n";
        if (in && line == "---") in = false;
        i = e + 1;
    }
    return out;
}
static std::string Outcome(const okay::backend::Result<okay::backend::Session>& r) {
    return "=> ok=" + std::to_string(r.ok) + " error=[" + r.error + "] user=[" + r.value.username + "] token=[" +
           r.value.accessToken + "] refresh=[" + r.value.refreshToken + "]\n";
}
struct ProviderRun {
    okay::backend::RecordingTransport rec;
    Fake fake;
    std::string outcomes;   // the Outcome() lines of SignUp / SignIn / Refresh, to compare with the golden's
    okay::backend::supabase::Auth Auth() { return okay::backend::supabase::Auth(kUrl, kKey, rec); }
    ProviderRun() {
        rec.answer = [this](const okay::backend::Request& r) {
            return okay::backend::FromHttp(fake.Answer(r.method, r.url, r.headers, r.bodyFile.empty() ? r.body : std::string()));
        };
    }
};
using ProviderFlow = std::function<void(ProviderRun&)>;
static std::vector<std::pair<const char*, ProviderFlow>> ProviderFlows() {
    namespace ob = okay::backend;
    std::vector<std::pair<const char*, ProviderFlow>> v;
    auto signIn = [](ProviderRun& p, const char* id, const char* pw) { p.outcomes += Outcome(p.Auth().SignIn(id, pw)); };
    v.push_back({"signup_with_username", [](ProviderRun& p) {
        p.fake.queue = {Http(200, TokenAnswer(1, "alex"))};
        p.outcomes += Outcome(p.Auth().SignUp("alex@example.test", "pw-123456", "alex"));
    }});
    v.push_back({"signup_needs_confirmation", [](ProviderRun& p) {
        p.fake.queue = {Http(200, "{\"id\":\"u2\",\"email\":\"sam@example.test\"}")};
        p.outcomes += Outcome(p.Auth().SignUp("sam@example.test", "pw \"quoted\" \\ 1", ""));
    }});
    v.push_back({"signup_refused", [](ProviderRun& p) {
        p.fake.queue = {Http(422, "{\"code\":422,\"msg\":\"User already registered\"}")};
        p.outcomes += Outcome(p.Auth().SignUp("alex@example.test", "pw-123456", ""));
    }});
    v.push_back({"signin_email", [=](ProviderRun& p) { p.fake.queue = {Http(200, TokenAnswer(3, "alex"))}; signIn(p, "alex@example.test", "pw-123456"); }});
    v.push_back({"signin_email_no_username_in_answer", [=](ProviderRun& p) {
        p.fake.queue = {Http(200, "{\"access_token\":\"" + Tok(4) + "\",\"refresh_token\":\"r4\"}")};
        signIn(p, "alex.doe@example.test", "pw-123456");
    }});
    v.push_back({"signin_retry_once_then_ok", [=](ProviderRun& p) {
        p.fake.queue = {NoAnswer("send: winhttp error 12030"), Http(200, TokenAnswer(6, "alex"))};
        signIn(p, "alex@example.test", "pw-123456");
    }});
    v.push_back({"signin_unreachable", [=](ProviderRun& p) {
        p.fake.queue = {NoAnswer("connect: winhttp error 12029"), NoAnswer("curl exit code 6")};
        signIn(p, "alex@example.test", "pw-123456");
    }});
    v.push_back({"signin_refused", [=](ProviderRun& p) {
        p.fake.queue = {Http(400, "{\"error\":\"invalid_grant\",\"error_description\":\"Invalid login credentials\"}")};
        signIn(p, "alex@example.test", "wrong-pass");
    }});
    v.push_back({"signin_refused_no_text", [=](ProviderRun& p) { p.fake.queue = {Http(500, "oops")}; signIn(p, "alex@example.test", "pw-123456"); }});
    v.push_back({"signin_answer_without_token", [=](ProviderRun& p) { p.fake.queue = {Http(200, "{}")}; signIn(p, "alex@example.test", "pw-123456"); }});
    v.push_back({"resend_confirmation", [](ProviderRun& p) {
        p.fake.queue = {Http(200, "{}"), Http(429, "{}"), NoAnswer("x")};
        for (int i = 0; i < 3; ++i) p.Auth().Resend("sam@example.test");   // (the 4th, a bad address, is refused before sending)
    }});
    v.push_back({"password_reset_request", [](ProviderRun& p) {
        p.fake.queue = {Http(200, "{}"), Http(200, "{}"), Http(500, "{}")};
        p.Auth().Recover("sam@example.test", "");
        p.Auth().Recover("sam@example.test", "https://reset.example.test/page?x=1&y=a b");
        p.Auth().Recover("sam@example.test", "not-a-web-address");
    }});
    v.push_back({"password_reset_with_code", [](ProviderRun& p) {
        p.fake.queue = {Http(200, "{\"access_token\":\"" + Tok(7) + "\"}"), Http(200, "{}"),
                        Http(403, "{\"msg\":\"Token has expired or is invalid\"}"),
                        Http(200, "{\"access_token\":\"" + Tok(8) + "\"}"), Http(422, "{}")};
        const ob::Response v1 = p.Auth().VerifyRecovery("sam@example.test", "123456");
        p.Auth().UpdateUser(okay::account::detail::JsonField(v1.body, "access_token"), "{\"password\":\"new-pass-1\"}");
        p.Auth().VerifyRecovery("sam@example.test", "654321");   // refused: no password change follows
        const ob::Response v3 = p.Auth().VerifyRecovery("sam@example.test", "1234567");
        p.Auth().UpdateUser(okay::account::detail::JsonField(v3.body, "access_token"), "{\"password\":\"new-pass-1\"}");
    }});
    v.push_back({"change_password_email_username", [](ProviderRun& p) {
        p.fake.queue = {Http(200, "{}"), Http(422, "{}"), Http(200, "{}"), Http(400, "{}")};
        p.Auth().UpdateUser(Tok(10), "{\"password\":\"new-pass-2\"}");
        p.Auth().UpdateUser(Tok(10), "{\"email\":\"alex2@example.test\"}");
        p.Auth().UpdateUser(Tok(10), "{\"data\":{\"username\":\"alex_2\"}}");
        p.Auth().UpdateUser(Tok(10), "{\"password\":\"new-pass-3\"}");
    }});
    v.push_back({"check_session_valid", [](ProviderRun& p) { p.fake.queue = {Http(200, "{}")}; p.Auth().User(Tok(13)); }});
    v.push_back({"check_session_refreshes", [](ProviderRun& p) {
        p.fake.queue = {Http(401, "{}"), Http(200, TokenAnswer(15, "alex"))};
        p.Auth().User(Jwt("u14", 1000));
        const auto r = p.Auth().Refresh("r14");
        CHECK(r.ok && r.value.accessToken == Tok(15) && r.value.refreshToken == "r15");
    }});
    v.push_back({"refresh_if_expiring", [](ProviderRun& p) {
        p.fake.queue = {Http(200, "{\"access_token\":\"" + Tok(19) + "\"}")};
        const auto r = p.Auth().Refresh("r18");
        CHECK(r.ok && r.value.accessToken == Tok(19) && r.value.refreshToken.empty());   // no new refresh token: the caller keeps its own
    }});
    v.push_back({"verify_token", [](ProviderRun& p) {
        p.fake.queue = {Http(200, "{\"id\":\"u-9\"}"), Http(401, "{}")};
        p.Auth().User("other.token.x");
        p.Auth().User("bad.token.y");
    }});
    v.push_back({"sign_out_sends_nothing", [](ProviderRun& p) { p.Auth().SignOut(Tok(20)); }});
    return v;
}

#endif

#include "test_okay_backend_golden_auth.inc"   // GOLDEN: the texts recorded from the code before the move

int main() {
    RUN_SUITE("okay_backend_golden_auth");
    const fs::path root = fs::temp_directory_path() / ("okay_backend_golden_" + acc::detail::RandomHex(8));
    const bool print = std::getenv("OKAY_GOLDEN_PRINT") != nullptr;
    const std::vector<Scenario> all = Scenarios();

    // 1. AccountService (the public API every program calls) sends exactly the golden requests and makes the same of the answers.
    if (print) std::cout << "// Recorded with OKAY_GOLDEN_PRINT=1 (tests/test_okay_backend_golden_auth.cpp).\n"
                            "static const char* const kGolden[][2] = {\n";
    std::size_t matched = 0;
    for (const Scenario& s : all) {
        const std::string got = s.run(root / s.name);
        if (print) { std::cout << "    {\"" << s.name << "\", R\"GOLD(" << got << ")GOLD\"},\n"; continue; }
        const char* want = nullptr;
        for (const auto& g : kGolden) if (std::string(g[0]) == s.name) want = g[1];
        CHECK(want != nullptr);
        if (!want) continue;
        const bool same = got == want;
        if (!same) std::cout << "  MISMATCH in " << s.name << "\n--- golden\n" << want << "--- now\n" << got << "\n";
        CHECK(same);
        matched += same;
    }
    if (print) { std::cout << "};\n"; return 0; }
    CHECK(matched == all.size());
    CHECK(all.size() == sizeof(kGolden) / sizeof(kGolden[0]));

#if !defined(OKAY_GOLDEN_OLD_HEADER)
    // 2. The Supabase provider called directly sends the same bytes, and makes the same sessions and messages.
    std::size_t sameRequests = 0;
    const auto flows = ProviderFlows();
    for (const auto& flow : flows) {
        const char* want = nullptr;
        for (const auto& g : kGolden) if (std::string(g[0]) == flow.first) want = g[1];
        CHECK(want != nullptr);
        if (!want) continue;
        ProviderRun p;
        flow.second(p);
        const bool same = p.fake.log == RequestsOf(want);
        if (!same) std::cout << "  PROVIDER MISMATCH in " << flow.first << "\n--- golden requests\n" << RequestsOf(want) << "--- provider\n" << p.fake.log << "\n";
        CHECK(same);
        sameRequests += same;
        std::size_t blocks = 0;   // the recorder kept exactly the requests the fake answered
        for (std::size_t k = p.fake.log.find("\n---\n"); k != std::string::npos; k = p.fake.log.find("\n---\n", k + 1)) ++blocks;
        CHECK(p.rec.sent.size() == blocks);
        std::size_t start = 0;   // each SignUp / SignIn outcome line is in the golden as AccountService gave it
        while (start < p.outcomes.size()) {
            const std::size_t e = p.outcomes.find('\n', start);
            const std::string line = p.outcomes.substr(start, e - start + 1);
            const bool found = std::string(want).find(line) != std::string::npos;
            if (!found) std::cout << "  PROVIDER OUTCOME not in golden " << flow.first << ": " << line;
            CHECK(found);
            start = e + 1;
        }
    }
    CHECK(sameRequests == flows.size());
    CHECK(flows.size() >= 19);

    // 3. Sign-in with a username. AccountService never sends it (for Supabase, Login refuses anything that is not an email
    //    before any request: the golden "signin_username*" texts above show that), so there is no recorded text. The provider
    //    keeps the path that was in AccountService; these texts are written from that code (AccountService.hpp at b9f097c46,
    //    SupabaseAuth): the email_for_login lookup, asked again once when it gets no answer, then the password grant.
    {
        const std::string rpc = "POST https://backend.example.test/rest/v1/rpc/email_for_login\napikey: test-publishable-key-0000\n"
                                "Content-Type: application/json\n\n{\"p_username\":\"alex\",\"p_password\":\"pw-123456\"}\n---\n";
        const std::string grant = "POST https://backend.example.test/auth/v1/token?grant_type=password\napikey: test-publishable-key-0000\n"
                                  "Content-Type: application/json\n\n{\"email\":\"alex@example.test\",\"password\":\"pw-123456\"}\n---\n";
        ProviderRun ok;
        ok.fake.queue = {Http(200, "\"alex@example.test\""), Http(200, TokenAnswer(5, "alex"))};
        const auto r = ok.Auth().SignIn("alex", "pw-123456");
        CHECK(ok.fake.log == rpc + grant);
        CHECK(r.ok && r.value.username == "alex" && r.value.accessToken == Tok(5) && r.value.refreshToken == "r5");
        ProviderRun wrong;
        wrong.fake.queue = {Http(200, "null")};
        const auto w = wrong.Auth().SignIn("alex", "pw-123456");
        CHECK(wrong.fake.log == rpc && !w.ok && w.error == "Invalid username/email or password.");
        ProviderRun down;
        down.fake.queue = {NoAnswer("connect: winhttp error 12029"), NoAnswer("")};
        const auto d = down.Auth().SignIn("alex", "pw-123456");
        CHECK(down.fake.log == rpc + rpc && !d.ok);
        // The second try's reason is shown; a try with no reason reads "no answer" (the transport fills it in, as before).
        CHECK(d.error == "Couldn't reach the account server (offline or unreachable).\nDetails: [no answer]");
    }

#endif

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
