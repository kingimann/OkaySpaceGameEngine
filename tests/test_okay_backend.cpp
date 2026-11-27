// OkayBackend round 2 core (docs/platform/okay_backend.md): choosing the provider from configuration (OKAY_BACKEND,
// backend.json), the request builder's header order, the transports, and reading a token's claims. The byte-for-byte
// checks of every auth flow are in test_okay_backend_golden_auth.cpp. Nothing here touches the network.
#include "test_framework.hpp"
#include "okay/Backend/Backend.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Core/Base64.hpp"
#include "okay/Core/Json.hpp"
#include "okay_backend_golden.hpp"   // SaveSession, Tok (made-up session)

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
        CHECK(ob::supabase::Auth::RecoverUrl("https://h.example.test//", "") == "https://h.example.test//auth/v1/recover");   // one slash dropped, as before
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
        std::string std64 = jwt;   // the standard alphabet's '+' and '/' read as '-' and '_' (as AccountService::JwtClaim did)
        const std::string mixed = b64("{\"sub\":\"\xfb\xff\xbf?>\"}");
        std::string mixedStd = mixed;
        for (char& c : mixedStd) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
        CHECK(mixed != mixedStd);   // the payload really has those characters
        CHECK(ob::TokenClaim("h." + mixed + ".s", "sub") == ob::TokenClaim("h." + mixedStd + ".s", "sub"));
        CHECK(ob::TokenClaim("h." + mixed + ".s", "sub") == "\xfb\xff\xbf?>");
        CHECK(ob::TokenClaim("h.e30=.s", "x").empty());   // "{}" with padding: skipped characters are fine
        acc::AccountService a(root / "tok", "https://p.example.test", "key-1");
        CHECK(a.UserId().empty() && a.TokenExpiresAt() == 0);   // nobody signed in
    }

    // ---- The Supabase provider's pure helpers stay what AccountService exposed ----
    CHECK(acc::AccountService::RecoverUrl("https://p.example.test/", "https://r.example.test/a b") ==
          ob::supabase::Auth::RecoverUrl("https://p.example.test/", "https://r.example.test/a b"));
    CHECK(ob::supabase::Auth::RecoverUrl("https://p.example.test", "ftp://x") == "https://p.example.test/auth/v1/recover");
    CHECK(acc::AccountService::RecoveryVerifyBody("e@x.test", "123456") == ob::supabase::Auth::RecoveryVerifyBody("e@x.test", "123456"));

    // ---- Database (round 3): the query text, every operator, encoding, hostile values ----
    {
        ob::RecordingTransport rec;
        rec.answer = [](const ob::Request&) { ob::Response r; r.reached = r.ok = true; r.status = 200; r.body = "[]"; return r; };
        ob::supabase::Database db("https://p.example.test/", "key-1", rec, "tok-1");
        auto path = [&db](const ob::Query& q) { return q.Build("GET").path; };
        // Every filter operator, in call order.
        CHECK(path(db.From("t").Select("a,b").Eq("c", "1").Neq("d", "2").Gt("e", "3").Gte("f", "4").Lt("g", "5").Lte("h", "6")) ==
              "t?select=a,b&c=eq.1&d=neq.2&e=gt.3&f=gte.4&g=lt.5&h=lte.6");
        CHECK(path(db.From("t").Like("n", "al*").Ilike("m", "*X*")) == "t?n=like.al%2A&m=ilike.%2AX%2A");
        // (round 6) A pattern the program encoded itself is written unchanged; Encoded stays the default.
        CHECK(path(db.From("t").Like("n", "%5BCall*", ob::ValueText::AsIs).Ilike("m", "*a%20b*", ob::ValueText::AsIs)) ==
              "t?n=like.%5BCall*&m=ilike.*a%20b*");
        CHECK(path(db.From("t").Like("n", "a&b", ob::ValueText::Encoded)) == path(db.From("t").Like("n", "a&b")));
        CHECK(path(db.From("t").Ilike("n", "x y&z=1")) == "t?n=ilike.x%20y%26z%3D1");
        CHECK(path(db.From("t").Is("a", ob::IsValue::Null).Is("b", ob::IsValue::True).Is("c", ob::IsValue::False).Is("d", ob::IsValue::Unknown)) ==
              "t?a=is.null&b=is.true&c=is.false&d=is.unknown");
        CHECK(path(db.From("t").In("id", {"a", "b c", "x,y", "q\"t", ""})) == "t?id=in.(a,b%20c,%22x%2Cy%22,%22q%5C%22t%22,%22%22)");
        CHECK(path(db.From("t").In("id", {})) == "t?id=in.()");
        // (round 5) In's three ways of writing items: quoted when needed (default), encoded only, unchanged.
        CHECK(path(db.From("t").In("id", {"a", "x,y", "(z)", " s"}, ob::ValueText::EncodedUnquoted)) == "t?id=in.(a,x%2Cy,%28z%29,%20s)");
        CHECK(path(db.From("t").In("id", {"a", "x,y", "(z)", "b c&d"}, ob::ValueText::AsIs)) == "t?id=in.(a,x,y,(z),b c&d)");
        CHECK(path(db.From("t").In("id", {"u-1", "u-2"}, ob::ValueText::EncodedUnquoted)) == path(db.From("t").In("id", {"u-1", "u-2"})));
        // (round 5) Or, And and Cond: PostgREST's logical operators, in call order with the other parts.
        using Q = ob::Query;
        CHECK(path(db.From("t").Select("a").Or({Q::Cond("x", "eq", "1"), Q::Cond("y", "neq", "2")}).Limit(3)) == "t?select=a&or=(x.eq.1,y.neq.2)&limit=3");
        CHECK(path(db.From("t").Or({Q::And({Q::Cond("a", "eq", "1"), Q::Cond("b", "eq", "2")}), Q::And({Q::Cond("a", "eq", "2"), Q::Cond("b", "eq", "1")})})) ==
              "t?or=(and(a.eq.1,b.eq.2),and(a.eq.2,b.eq.1))");
        CHECK(path(db.From("t").Or({})) == "t?or=()");
        CHECK(Q::And({}) == "and()");
        CHECK(Q::Cond("a", "gt", "2026-01-02T03:04:05Z", ob::ValueText::AsIs) == "a.gt.2026-01-02T03:04:05Z");
        // Hostile values inside or=(...): by default quoted when they hold a reserved character, then encoded, so a value can
        // neither end the condition nor add a part to the query; encoded only, or unchanged, when asked.
        CHECK(Q::Cond("n", "eq", "a,b)&or=(x") == "n.eq.%22a%2Cb%29%26or%3D%28x%22");
        CHECK(Q::Cond("n", "eq", "a,b)&or=(x", ob::ValueText::EncodedUnquoted) == "n.eq.a%2Cb%29%26or%3D%28x");
        CHECK(Q::Cond("n", "eq", "a b", ob::ValueText::AsIs) == "n.eq.a b");
        CHECK(Q::Cond("n&x", "eq&y", "v") == "n%26x.eq%26y.v");   // names are written like column names
        CHECK(Q::Cond("n", "eq", "") == "n.eq.%22%22");             // an empty value is quoted (else "n.eq." is no value)
        // Order: one part, tie-breakers added to it; limit, offset, range.
        CHECK(path(db.From("t").Order("a", true).Eq("x", "1").Order("b")) == "t?order=a.desc,b.asc&x=eq.1");
        CHECK(path(db.From("t").Limit(5).Offset(10)) == "t?limit=5&offset=10");
        CHECK(path(db.From("t").Range(20, 29)) == "t?offset=20&limit=10");
        CHECK(path(db.From("t").Range(5, 2)) == "t?offset=5&limit=0");
        CHECK(path(db.From("t").Range(-4, 0)) == "t?offset=0&limit=1");
        CHECK(path(db.From("t").Range(0, 0x7fffffffffffffffLL)) == "t?offset=0&limit=9223372036854775807");
        CHECK(path(db.From("t").Select("a").Select("b")) == "t?select=b");   // a second Select replaces the first
        CHECK(path(db.From("t")) == "t");
        // Values are percent-encoded (RFC 3986 unreserved kept): a value can never add a part to the query.
        CHECK(path(db.From("t").Eq("n", "a&select=*,x=y#z?")) == "t?n=eq.a%26select%3D%2A%2Cx%3Dy%23z%3F");
        CHECK(path(db.From("t").Eq("n", std::string("\xc3\xa9\x00\n", 4))) == "t?n=eq.%C3%A9%00%0A");
        CHECK(path(db.From("t").Eq("n", "A-z_0.9~")) == "t?n=eq.A-z_0.9~");
        CHECK(path(db.From("t").Gt("at", "2026-01-02T03:04:05Z", ob::ValueText::AsIs)) == "t?at=gt.2026-01-02T03:04:05Z");
        // Names keep PostgREST's select characters and encode the rest (no "&", "=", "#", "?" or space gets through).
        CHECK(path(db.From("t").Select("*,profiles(name),a->b,c:d,e!f")) == "t?select=*,profiles(name),a->b,c:d,e!f");
        CHECK(path(db.From("t&x=1").Select("a b").Eq("c=d", "1")) == "t%26x%3D1?select=a%20b&c%3Dd=eq.1");
        // Prefer and Accept headers.
        CHECK(db.From("t").Build("POST", "{}").headers.empty());
        CHECK(db.From("t").Returning(ob::Return::Minimal).Build("POST", "{}", true).headers == "Prefer: resolution=merge-duplicates,return=minimal");
        CHECK(db.From("t").Returning(ob::Return::Representation).Build("POST", "{}").headers == "Prefer: return=representation");
        CHECK(db.From("t").Prefer("count=exact").Returning(ob::Return::Minimal).Build("PATCH", "{}").headers == "Prefer: return=minimal,count=exact");
        CHECK(db.From("t").Single().Build("GET").headers == "Accept: application/vnd.pgrst.object+json");
        CHECK(db.From("t").Single().Returning(ob::Return::Representation).Build("POST", "{}").headers ==
              "Prefer: return=representation\nAccept: application/vnd.pgrst.object+json");
        CHECK(path(db.From("t").OnConflict("board,user_id")) == "t?on_conflict=board,user_id");
        // What goes on the wire: methods, the /rest/v1 prefix, the header order (key, token, Prefer / Accept, Content-Type).
        db.From("t").Eq("a", "1").Get();
        db.From("t").Returning(ob::Return::Minimal).Insert("[{\"a\":1}]");
        db.From("t").OnConflict("a").Upsert("[{\"a\":1}]");
        db.From("t").Eq("a", "1").Update("{\"b\":2}");
        db.From("t").Eq("a", "1").Delete();
        db.From("t").Single().Eq("a", "1").Get();
        db.Rpc("my_fn", "{\"p\":1}");
        db.Rpc("bad/../fn?x", "{}");
        CHECK(rec.sent.size() == 8);
        if (rec.sent.size() == 8) {
            const auto& s = rec.sent;
            CHECK(s[0].method == "GET" && s[0].url == "https://p.example.test/rest/v1/t?a=eq.1" && s[0].body.empty());
            CHECK((s[0].headers == std::vector<std::string>{"apikey: key-1", "Authorization: Bearer tok-1"}));
            CHECK(s[1].method == "POST" && s[1].url == "https://p.example.test/rest/v1/t" && s[1].body == "[{\"a\":1}]");
            CHECK((s[1].headers == std::vector<std::string>{"apikey: key-1", "Authorization: Bearer tok-1", "Prefer: return=minimal",
                                                             "Content-Type: application/json"}));
            CHECK(s[2].method == "POST" && s[2].url == "https://p.example.test/rest/v1/t?on_conflict=a");
            CHECK(s[2].headers.size() == 4 && s[2].headers[2] == "Prefer: resolution=merge-duplicates");
            CHECK(s[3].method == "PATCH" && s[3].url == "https://p.example.test/rest/v1/t?a=eq.1" && s[3].body == "{\"b\":2}");
            CHECK(s[4].method == "DELETE" && s[4].url == "https://p.example.test/rest/v1/t?a=eq.1" && s[4].headers.size() == 2);
            CHECK(s[5].headers.size() == 3 && s[5].headers[2] == "Accept: application/vnd.pgrst.object+json");
            CHECK(s[6].method == "POST" && s[6].url == "https://p.example.test/rest/v1/rpc/my_fn" && s[6].body == "{\"p\":1}");
            CHECK(s[7].url == "https://p.example.test/rest/v1/rpc/bad%2F..%2Ffn%3Fx");   // a name can't leave rpc/
        }
        // No token: the publishable key only. The answer comes back as the provider got it.
        ob::supabase::Database anon("https://p.example.test", "key-1", rec);
        const ob::Response r = anon.From("profiles").Select("id").Eq("username", "a b").Get();
        CHECK(r.ok && r.body == "[]");
        CHECK(rec.sent.back().url == "https://p.example.test/rest/v1/profiles?select=id&username=eq.a%20b");
        CHECK((rec.sent.back().headers == std::vector<std::string>{"apikey: key-1"}));
        // A sender (how AccountService plugs in its Api): gets the path from the project address and the header lines.
        std::string seen;
        ob::supabase::Database viaSender([&seen](const std::string& m, const std::string& p, const std::string& b, const std::string& h) {
            seen = m + " " + p + " " + b + " [" + h + "]";
            return ob::Response{};
        });
        viaSender.From("cloud_saves").Returning(ob::Return::Minimal).Upsert("[]");
        CHECK(seen == "POST /rest/v1/cloud_saves [] [Prefer: resolution=merge-duplicates,return=minimal]");
        viaSender.Rpc("queue_poll", "{}");
        CHECK(seen == "POST /rest/v1/rpc/queue_poll {} []");
        // AccountService hands out the same: signed out, its Api sends nothing and answers "not reached".
        acc::AccountService a(root / "db", "https://p.example.test", "key-1");
        CHECK(!a.Db().From("cloud_saves").Select("key").Get().reached);
    }

    // ---- JSON strings the account service writes (bug found in round 3, fixed in round 4) ----
    // RFC 8259 section 7: every control character (U+0000 to U+001F) must be escaped inside a JSON string. detail::JsonEscape
    // escaped only '"', '\', line feed, carriage return and tab, so a cloud save holding any other control character was sent
    // as invalid JSON and the server refused it.
    {
        std::string all;   // every control character, then the ones that were already handled, then plain text and UTF-8
        for (int c = 0; c < 0x20; ++c) all += (char)c;
        all += "\"\\/ plain \xC3\xA9 \x7F";
        const std::string esc = acc::detail::JsonEscape(all);
        for (char c : esc) CHECK((unsigned char)c >= 0x20);   // no raw control character is left
        okay::json::Value v;
        std::string why;
        CHECK(okay::json::Parse("\"" + esc + "\"", v, &why));   // a valid JSON string ...
        CHECK(v.IsString() && v.string == all);                  // ... that reads back as the same text
        CHECK(acc::detail::JsonEscape(std::string("\x01", 1)) == "\\u0001");
        CHECK(acc::detail::JsonEscape(std::string("\0", 1)) == "\\u0000");
        CHECK(acc::detail::JsonEscape("\b\f\x1f") == "\\u0008\\u000c\\u001f");
        CHECK(acc::detail::JsonEscape("a\"b\\c\nd\re\tf") == "a\\\"b\\\\c\\nd\\re\\tf");   // unchanged for what it already did
        CHECK(acc::detail::JsonEscape("caf\xC3\xA9 / 100%") == "caf\xC3\xA9 / 100%");          // UTF-8 and "/" go as they are

        // The cloud save that showed it: the body sent is valid JSON, and the save reads back unchanged.
        const fs::path d = root / "cloud";
        golden::SaveSession(d, "alex", golden::Tok(1), "r1");
        acc::AccountService a(d, "https://p.example.test", "key-1");
        std::string sent;
        a.SetTransportForTest([&sent](const std::string&, const std::string&, const std::vector<std::string>&, const std::string& b) {
            sent = b; okay::http::Result r; r.reached = true; r.status = 201; return r;
        });
        const std::string save = "level 3\x01\x02 bell\x07 end\x1b";
        CHECK(a.CloudSave("slot1", save));
        okay::json::Value body;
        CHECK(okay::json::Parse(sent, body, &why));
        CHECK(body.IsArray() && body.array.size() == 1 && body.array[0].Get("data") &&
              body.array[0].Get("data")->string == save);
        a.SetTransportForTest([&sent](const std::string&, const std::string&, const std::vector<std::string>&, const std::string&) {
            okay::http::Result r; r.reached = true; r.status = 200;
            okay::json::Value in;
            if (okay::json::Parse(sent, in)) r.body = sent;   // the server keeps the row it was sent and answers it back
            return r;
        });
        CHECK(a.CloudLoad("slot1") == save);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
