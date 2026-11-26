// OkayBackend round 2 core (docs/platform/okay_backend.md): choosing the provider from configuration (OKAY_BACKEND,
// backend.json), the request builder's header order, the transports, and reading a token's claims. The byte-for-byte
// checks of every auth flow are in test_okay_backend_golden_auth.cpp. Nothing here touches the network.
#include "test_framework.hpp"
#include "okay/Backend/Backend.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Core/Base64.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ob = okay::backend;
namespace acc = okay::account;

static void WriteText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
static void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}

int main() {
    RUN_SUITE("okay_backend");
    const fs::path root = fs::temp_directory_path() / ("okay_backend_test_" + acc::detail::RandomHex(8));
    SetEnv("OKAY_BACKEND", nullptr);   // the developer's own setting must not decide these results

    // ---- Provider words ----
    CHECK(ob::ParseProviderKind("supabase") == ob::ProviderKind::Supabase);
    CHECK(ob::ParseProviderKind("  Supabase\r\n") == ob::ProviderKind::Supabase);
    CHECK(ob::ParseProviderKind("LOCAL") == ob::ProviderKind::Local);
    CHECK(ob::ParseProviderKind("auto") == ob::ProviderKind::Auto);
    CHECK(ob::ParseProviderKind("") == ob::ProviderKind::Auto);
    CHECK(ob::ParseProviderKind("supabasex") == ob::ProviderKind::Auto);   // a typo keeps the default, never knocks a program off
    CHECK(ob::ParseProviderKind("firebase") == ob::ProviderKind::Auto);
    CHECK(std::string(ob::ProviderKindName(ob::ProviderKind::Supabase)) == "supabase");
    CHECK(std::string(ob::ProviderKindName(ob::ProviderKind::Local)) == "local");
    CHECK(std::string(ob::ProviderKindName(ob::ProviderKind::Auto)) == "auto");

    // ---- Where the choice comes from: the variable, then backend.json, then the default ----
    {
        const fs::path d = root / "cfg";
        ob::Config c = ob::LoadConfig(d, nullptr);   // no folder yet, nothing set
        CHECK(c.provider == ob::ProviderKind::Auto && c.source == "default");
        WriteText(d / "backend.json", "{\n  \"provider\": \"local\"\n}\n");
        c = ob::LoadConfig(d, nullptr);
        CHECK(c.provider == ob::ProviderKind::Local && c.source == "backend.json");
        c = ob::LoadConfig(d, "supabase");           // the variable wins over the file
        CHECK(c.provider == ob::ProviderKind::Supabase && c.source == "OKAY_BACKEND");
        c = ob::LoadConfig(d, "");                   // an empty variable counts as unset
        CHECK(c.provider == ob::ProviderKind::Local && c.source == "backend.json");
        WriteText(d / "backend.json", "{\"url\":\"https://x.example.test\"}");   // no "provider": the default
        c = ob::LoadConfig(d, nullptr);
        CHECK(c.provider == ob::ProviderKind::Auto && c.source == "default");
        WriteText(d / "backend.json", "not json at all");
        CHECK(ob::LoadConfig(d, nullptr).provider == ob::ProviderKind::Auto);
        WriteText(d / "backend.json", "{\"provider\":\"local\"}" + std::string(70 * 1024, ' '));   // over 64 KiB: not read
        CHECK(ob::LoadConfig(d, nullptr).provider == ob::ProviderKind::Auto);
        CHECK(ob::LoadConfig(fs::path(), nullptr).provider == ob::ProviderKind::Auto);
    }

    // ---- The account service follows the configuration (option on), and an explicit provider still wins ----
    {
        const fs::path d = root / "svc";
        CHECK(std::string(acc::AccountService(d / "a", "https://p.example.test", "key-1").ProviderName()) == "supabase");   // default: unchanged
        CHECK(std::string(acc::AccountService(d / "b").ProviderName()) == "local");
        CHECK(std::string(acc::AccountService(d / "c", "https://p.example.test").ProviderName()) == "custom");
        WriteText(d / "a2" / "backend.json", "{\"provider\":\"local\"}");
        acc::AccountService local(d / "a2", "https://p.example.test", "key-1");
        CHECK(std::string(local.ProviderName()) == "local" && !local.IsOnline());
        CHECK(local.Register("tester", "secret123").ok);   // on-device accounts work there
        acc::AccountService explicitSb(d / "a2", "https://p.example.test", "key-1", acc::Provider::Supabase);
        CHECK(std::string(explicitSb.ProviderName()) == "supabase");
        SetEnv("OKAY_BACKEND", "supabase");                  // the variable wins over backend.json
        CHECK(std::string(acc::AccountService(d / "a2", "https://p.example.test", "key-1").ProviderName()) == "supabase");
        SetEnv("OKAY_BACKEND", "local");
        CHECK(std::string(acc::AccountService(d / "a", "https://p.example.test", "key-1").ProviderName()) == "local");
        SetEnv("OKAY_BACKEND", "nonsense");                  // unknown: today's rule
        CHECK(std::string(acc::AccountService(d / "a", "https://p.example.test", "key-1").ProviderName()) == "supabase");
        SetEnv("OKAY_BACKEND", nullptr);
    }

    // ---- The request builder: header lines in the order the programs always sent them ----
    {
        ob::Request r = ob::supabase::MakeRequest("k", "POST", "https://h.example.test/x", "{}", "tok", "\"Prefer: a\"\r\n\nX-B: 1");
        CHECK((r.headers == std::vector<std::string>{"apikey: k", "Authorization: Bearer tok", "Prefer: a", "X-B: 1", "Content-Type: application/json"}));
        CHECK(r.body == "{}" && r.bodyFile.empty() && r.timeoutSec == 0);
        r = ob::supabase::MakeRequest("", "GET", "https://h.example.test/y", "", "");    // custom server, anonymous, no body
        CHECK(r.headers.empty());
        r = ob::supabase::MakeRequest("k", "POST", "u", "ignored", "t", "", "/tmp/f.png", "image/png", 7);
        CHECK(r.headers.back() == "Content-Type: image/png" && r.bodyFile == "/tmp/f.png" && r.timeoutSec == 7);
        CHECK(ob::supabase::TrimBase("https://h.example.test/") == "https://h.example.test");
        CHECK(ob::supabase::TrimBase("https://h.example.test//") == "https://h.example.test/");   // one slash, as before
        CHECK(ob::supabase::SplitHeaders("\n\n").empty());
    }

    // ---- Transports ----
    {
        ob::RecordingTransport rec;
        ob::Request q; q.method = "GET"; q.url = "https://h.example.test/";
        ob::Response none = rec.Send(q);
        CHECK(!none.reached && !none.ok && !none.error.empty() && rec.sent.size() == 1);
        rec.answer = [](const ob::Request&) { ob::Response r; r.reached = true; r.status = 204; r.ok = true; return r; };
        CHECK(rec.Send(q).status == 204 && rec.sent.size() == 2 && rec.sent[1].url == q.url);

        okay::http::Result nr; nr.reached = true; nr.status = 302; nr.body = "b";
        ob::Response fr = ob::FromHttp(nr);
        CHECK(fr.reached && !fr.ok && fr.status == 302 && fr.body == "b");
        nr = {}; nr.maybeSent = true;
        fr = ob::FromHttp(nr);
        CHECK(!fr.reached && fr.error == "no answer" && fr.maybeSent);
        nr.status = 299; nr.reached = true;
        CHECK(ob::FromHttp(nr).ok);

        ob::HookTransport hook;
        CHECK(!hook.HasHook());
        std::string seenBody = "?";
        hook.SetHook([&](const std::string&, const std::string&, const std::vector<std::string>&, const std::string& b) {
            seenBody = b; okay::http::Result r; r.reached = true; r.status = 200; return r;
        });
        ob::Request up = ob::supabase::MakeRequest("k", "POST", "u", "json", "t", "", "/tmp/file.bin", "application/octet-stream");
        CHECK(hook.HasHook() && hook.Send(up).ok && seenBody.empty());   // an upload's file is never handed to the test function
        ob::Request js = ob::supabase::MakeRequest("k", "POST", "u", "json", "t");
        CHECK(hook.Send(js).ok && seenBody == "json");
    }

    // ---- Reading a token's claims (moved from AccountService::JwtClaim) ----
    {
        auto b64 = [](const std::string& s) {
            return okay::base64::Encode(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), okay::base64::Alphabet::Url, false);
        };
        const std::string jwt = b64("{\"alg\":\"none\"}") + "." + b64("{\"sub\":\"user-1\",\"exp\": 1700000000,\"neg\":-5}") + ".sig";
        CHECK(ob::TokenClaim(jwt, "sub") == "user-1");
        CHECK(ob::TokenClaim(jwt, "exp") == "1700000000");
        CHECK(ob::TokenClaim(jwt, "neg") == "-5");
        CHECK(ob::TokenClaim(jwt, "missing").empty());
        CHECK(ob::TokenClaim("not-a-token", "sub").empty());
        CHECK(ob::TokenClaim("a.b", "sub").empty());
        CHECK(ob::TokenClaim("", "sub").empty());
        acc::AccountService a(root / "tok", "https://p.example.test", "key-1");
        CHECK(a.UserId().empty() && a.TokenExpiresAt() == 0);   // nobody signed in
    }

    // ---- The Supabase provider's pure helpers stay what AccountService exposed ----
    CHECK(acc::AccountService::RecoverUrl("https://p.example.test/", "https://r.example.test/a b") ==
          ob::supabase::Auth::RecoverUrl("https://p.example.test/", "https://r.example.test/a b"));
    CHECK(ob::supabase::Auth::RecoverUrl("https://p.example.test", "ftp://x") == "https://p.example.test/auth/v1/recover");
    CHECK(acc::AccountService::RecoveryVerifyBody("e@x.test", "123456") == ob::supabase::Auth::RecoveryVerifyBody("e@x.test", "123456"));

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
