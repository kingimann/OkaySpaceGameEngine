// Client side of "Download my data" / "Delete my account": request building,
// confirmation gating, export assembly and error mapping. No network.
#include "test_framework.hpp"
#include "okay/Platform/Account/AccountData.hpp"

using namespace okay::account;
using namespace okay::account::data;

static ApiResponse Resp(bool reached, long status, const std::string& body) {
    ApiResponse r; r.reached = reached; r.status = status; r.ok = reached && status >= 200 && status < 300; r.body = body; return r;
}

int main() {
    RUN_SUITE("account_data");

    // ---- confirmation gating ----
    CHECK(DeleteConfirmed("Alice", "alice", "pw", true));
    CHECK(DeleteConfirmed("  alice ", "Alice", "pw", true));
    CHECK(!DeleteConfirmed("alic", "alice", "pw", true));
    CHECK(!DeleteConfirmed("", "", "pw", true));          // empty username never matches
    CHECK(!DeleteConfirmed("alice", "alice", "", true));  // password required
    CHECK(!DeleteConfirmed("alice", "alice", "pw", false)); // tick-box required
    CHECK(!DeleteConfirmed("alice2", "alice", "pw", true));

    // ---- request building: identity is the token, never the body ----
    {
        DeleteRequest q = BuildDeleteRequest("al\"ice", "p\\w");
        CHECK(q.method == "POST");
        CHECK(q.path == "/functions/v1/delete-account");
        CHECK(q.body == "{\"confirm_username\":\"al\\\"ice\",\"password\":\"p\\\\w\"}");
        CHECK(q.body.find("user_id") == std::string::npos && q.body.find("\"id\"") == std::string::npos);
    }

    // ---- error mapping ----
    {
        DeleteOutcome o = MapDeleteResponse(Resp(true, 200, "{\"ok\":true,\"steps\":[]}"));
        CHECK(o.ok && o.signOut && !o.notDeployed);
        o = MapDeleteResponse(Resp(false, 0, ""));
        CHECK(!o.ok && !o.signOut && o.message.find("may have finished") != std::string::npos);
        o = MapDeleteResponse(Resp(true, 404, "{\"code\":\"NOT_FOUND\",\"message\":\"Requested function was not found\"}"));
        CHECK(!o.ok && !o.signOut && o.notDeployed && o.message.find("not deployed") != std::string::npos);
        o = MapDeleteResponse(Resp(true, 401, "{\"error\":\"x\"}"));
        CHECK(!o.ok && !o.signOut && o.message.find("expired") != std::string::npos);
        o = MapDeleteResponse(Resp(true, 403, "{\"error\":\"wrong password\"}"));
        CHECK(!o.ok && !o.signOut && o.message.find("Wrong password") != std::string::npos);
        o = MapDeleteResponse(Resp(true, 500, "{\"error\":\"step \\\"forum_posts\\\" failed: HTTP 500\",\"failed_step\":\"forum_posts\",\"completed\":[]}"));
        CHECK(!o.ok && !o.signOut && o.failedStep == "forum_posts" && o.message.find("forum_posts") != std::string::npos);
        o = MapDeleteResponse(Resp(true, 500, "{\"failed_step\":\"auth_user\"}"));
        CHECK(!o.signOut && o.message.find("sign-in record") != std::string::npos);
        // A 200 without "ok":true is not success.
        o = MapDeleteResponse(Resp(true, 200, "<html>proxy page</html>"));
        CHECK(!o.ok && !o.signOut);
        o = MapDeleteResponse(Resp(true, 502, ""));
        CHECK(!o.ok && !o.signOut);
    }

    // ---- export ----
    const std::string uid = "11111111-2222-3333-4444-555555555555";
    {
        auto secs = ExportSections(uid);
        CHECK(secs.size() >= 20);
        for (const auto& s : secs) CHECK(s.path.find(uid) != std::string::npos);   // every read is scoped to me
        for (const auto& s : secs) CHECK(s.path.rfind("/rest/v1/", 0) == 0);
        bool msg = false, forum = false, market = false, ach = false;
        for (const auto& s : secs) {
            msg |= s.key == "messages_sent" && s.path.find("sender=eq.") != std::string::npos;
            forum |= s.key == "forum_posts"; market |= s.key == "marketplace_listings"; ach |= s.key == "achievements";
        }
        CHECK(msg && forum && market && ach);
    }
    {
        std::vector<std::string> asked;
        ExportResult r = BuildExport(uid, "Alice \"A\"", 0, [&](const std::string& path) {
            asked.push_back(path);
            if (path.find("/follows?") != std::string::npos) return Resp(true, 404, "{\"code\":\"PGRST205\"}");
            if (path.find("/forum_posts?") != std::string::npos) return Resp(true, 200, "[{\"id\":1,\"body\":\"a } brace\"},{\"id\":2,\"body\":\"b\"}]");
            return Resp(true, 200, "[]");
        });
        CHECK(r.ok && r.complete && r.failed == 0 && r.notSetUp == 1);
        CHECK(asked.size() == ExportSections(uid).size());
        CHECK(r.rows == 2);
        CHECK(r.json.find("\"exported_at\": \"1970-01-01T00:00:00Z\"") != std::string::npos);
        CHECK(r.json.find("\"username\": \"Alice \\\"A\\\"\"") != std::string::npos);
        CHECK(r.json.find("\"forum_posts\": [{\"id\":1,\"body\":\"a } brace\"}") != std::string::npos);
        CHECK(r.json.find("\"not_set_up_on_this_server\": [\"follows\"]") != std::string::npos);
        CHECK(r.json.find("\"complete\": true") != std::string::npos);
    }
    {   // a refused section makes the export honestly incomplete
        ExportResult r = BuildExport(uid, "a", 0, [&](const std::string& p) {
            return p.find("/messages?") != std::string::npos ? Resp(true, 500, "") : Resp(true, 200, "[]"); });
        CHECK(r.ok && !r.complete && r.failed == 1);
        CHECK(r.json.find("\"complete\": false") != std::string::npos);
        CHECK(r.json.find("{\"section\":\"messages_sent\",\"error\":\"HTTP 500\"}") != std::string::npos);
    }
    {   // expired session reads as such; unreachable server aborts
        ExportResult r = BuildExport(uid, "a", 0, [&](const std::string&) { return Resp(true, 401, ""); });
        CHECK(r.failed == r.sections + r.failed && r.json.find("session expired") != std::string::npos);
        ExportResult n = BuildExport(uid, "a", 0, [&](const std::string&) { return Resp(false, 0, ""); });
        CHECK(!n.ok && !n.error.empty() && n.json.empty());
        ExportResult e = BuildExport("", "a", 0, [&](const std::string&) { return Resp(true, 200, "[]"); });
        CHECK(!e.ok);
    }
    {   // progress is reported for every request, ending at total
        std::size_t last = 0, calls = 0, tot = 0;
        BuildExport(uid, "a", 0, [&](const std::string&) { return Resp(true, 200, "[]"); },
                    [&](std::size_t d, std::size_t t, const std::string&) { last = d; tot = t; ++calls; });
        CHECK(last == tot && calls == tot + 1);
    }
    TEST_MAIN_RESULT();
}
