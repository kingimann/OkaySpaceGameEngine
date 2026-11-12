#include "test_framework.hpp"
#include "UserReports.hpp"

// User reports (docs/sql/reports.sql): the requests the launcher sends, how the
// server's answer becomes an honest message, and the moderator list. No network.
namespace {
namespace rp = okayrep;
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
const char* kUid = "00000000-0000-0000-0000-0000000000b2";
const char* kMe = "00000000-0000-0000-0000-0000000000a1";

void TestBuild() {
    RUN_SUITE("building a report");
    rp::Request r; std::string err;
    CHECK(rp::Build(rp::Target::User, kUid, "harassment", "called me names", kMe, r, &err));
    CHECK(r.method == "POST" && r.path == "/rest/v1/reports" && Has(r.header, "return=minimal"));
    CHECK(Has(r.body, "\"target_type\":\"user\"") && Has(r.body, std::string("\"target_id\":\"") + kUid + "\"") &&
          Has(r.body, "\"reason\":\"harassment\"") && Has(r.body, "called me names"));
    // The client never sends who is reporting or the status: the server fills those in.
    CHECK(!Has(r.body, "reporter") && !Has(r.body, "status"));
    // One target of each kind.
    const std::pair<rp::Target, const char*> kinds[] = {{rp::Target::Message, "message"}, {rp::Target::GroupMessage, "group_message"},
        {rp::Target::ForumThread, "forum_thread"}, {rp::Target::ForumPost, "forum_post"},
        {rp::Target::MarketItem, "market_item"}, {rp::Target::Server, "server"}};
    for (const auto& k : kinds) {
        CHECK(rp::Build(k.first, "1234", "spam", "", "", r));
        CHECK(Has(r.body, std::string("\"target_type\":\"") + k.second + "\"") && Has(r.body, "\"note\":null"));
    }
    // Refusals say why and send nothing.
    CHECK(!rp::Build(rp::Target::User, kMe, "spam", "", kMe, r, &err) && Has(err, "yourself"));
    CHECK(!rp::Build(rp::Target::User, "not-a-uuid", "spam", "", kMe, r, &err) && Has(err, "Nothing to report"));
    CHECK(!rp::Build(rp::Target::Message, "12abc", "spam", "", kMe, r, &err));
    CHECK(!rp::Build(rp::Target::Message, "0", "spam", "", kMe, r, &err));
    CHECK(!rp::Build(rp::Target::Message, "", "spam", "", kMe, r, &err));
    CHECK(!rp::Build(rp::Target::Message, "5", "", "", kMe, r, &err) && Has(err, "reason"));
    CHECK(!rp::Build(rp::Target::Message, "5", "dislike", "", kMe, r, &err));
    // The note is trimmed, cleaned of control characters, escaped and capped at 500.
    CHECK(rp::Build(rp::Target::Message, "5", "scam", "  say \"hi\"\n\x01 now  ", "", r));
    CHECK(Has(r.body, "say \\\"hi\\\"\\n") && !Has(r.body, "\x01"));
    CHECK(rp::Build(rp::Target::Message, "5", "scam", std::string(900, 'x'), "", r));
    CHECK(r.body.size() < 700 && Has(r.body, std::string(500, 'x')) && !Has(r.body, std::string(501, 'x')));
    CHECK(rp::Build(rp::Target::Message, "5", "other", "   ", "", r) && Has(r.body, "\"note\":null"));
}

void TestReasons() {
    RUN_SUITE("reason list");
    int n = 0; const rp::Reason* rs = rp::Reasons(n);
    CHECK(n >= 5);
    for (int i = 0; i < n; ++i) CHECK(rp::ValidReason(rs[i].key) && std::string(rs[i].label).size() > 3);
    CHECK(!rp::ValidReason("") && !rp::ValidReason("Spam") && !rp::ValidReason("cheating "));
    CHECK(rp::ReasonLabel("harassment") == "Harassment or bullying" && rp::ReasonLabel("zzz") == "zzz");
}

void TestOutcomes() {
    RUN_SUITE("what the server answered");
    using O = rp::Outcome;
    CHECK(rp::Classify(201, "") == O::Sent && rp::Classify(204, "") == O::Sent);
    CHECK(rp::Accepted(O::Sent) && !rp::Accepted(O::NotSetUp) && !rp::Accepted(O::Other));
    // Table missing: PostgREST schema-cache miss, or Postgres undefined_table.
    CHECK(rp::Classify(404, "{\"code\":\"PGRST205\",\"message\":\"Could not find the table 'public.reports' in the schema cache\"}") == O::NotSetUp);
    CHECK(rp::Classify(400, "{\"code\":\"42P01\",\"message\":\"relation \\\"public.reports\\\" does not exist\"}") == O::NotSetUp);
    CHECK(rp::Classify(404, "") != O::Sent);
    // Signed out / expired session.
    CHECK(rp::Classify(401, "{\"message\":\"JWT expired\"}") == O::SignedOut);
    CHECK(rp::Classify(401, "") == O::SignedOut);
    // The SQL's own messages.
    CHECK(rp::Classify(409, "{\"code\":\"23505\",\"message\":\"report_duplicate: you already reported this\"}") == O::Duplicate);
    CHECK(rp::Classify(400, "{\"code\":\"23514\",\"message\":\"report_rate_limit: too many reports, try again later\"}") == O::RateLimited);
    CHECK(rp::Classify(400, "{\"code\":\"23514\",\"message\":\"report_self: you cannot report yourself or your own content\"}") == O::Self);
    CHECK(rp::Classify(404, "{\"code\":\"P0002\",\"message\":\"report_target_missing: that no longer exists\"}") == O::TargetGone);
    CHECK(rp::Classify(403, "{\"message\":\"new row violates row-level security policy for table \\\"reports\\\"\"}") == O::Denied);
    CHECK(rp::Classify(0, "") == O::NoNetwork);
    CHECK(rp::Classify(500, "boom") == O::Other);
    // Honest wording: only a Sent answer says "Report sent"; everything else says it was not.
    for (O o : {O::SignedOut, O::NotSetUp, O::Duplicate, O::RateLimited, O::Self, O::TargetGone, O::Denied, O::NoNetwork, O::Other}) {
        const std::string m = rp::Message(o);
        CHECK(!Has(m, "Report sent") && (Has(m, "not sent") || Has(m, "Nothing was sent")));
    }
    CHECK(Has(rp::Message(O::Sent), "Report sent"));
    CHECK(Has(rp::Message(O::NotSetUp), "reports.sql") && Has(rp::Message(O::SignedOut), "Sign in"));
    CHECK(Has(rp::MessageFor(404, "PGRST205"), "isn't set up") && Has(rp::MessageFor(201, ""), "Report sent"));
}

void TestModeratorSide() {
    RUN_SUITE("moderator list and review");
    rp::Request q; std::string err;
    q = rp::List("open", 50);
    CHECK(q.method == "GET" && Has(q.path, "/rest/v1/reports?select=") && Has(q.path, "&status=eq.open") && Has(q.path, "limit=50"));
    CHECK(!Has(rp::List("", 9999).path, "status=eq.") && Has(rp::List("", 9999).path, "limit=200"));
    CHECK(!Has(rp::List("x;drop", 10).path, "status=eq."));
    CHECK(rp::SetStatus(7, "resolved", "removed the post", q, &err));
    CHECK(q.method == "PATCH" && q.path == "/rest/v1/reports?id=eq.7" && Has(q.body, "\"status\":\"resolved\"") && Has(q.body, "removed the post"));
    CHECK(rp::SetStatus(7, "open", "", q) && Has(q.body, "\"review_note\":null"));
    CHECK(!rp::SetStatus(0, "resolved", "", q, &err) && Has(err, "No report"));
    CHECK(!rp::SetStatus(7, "actioned", "", q, &err) && Has(err, "Unknown"));
    CHECK(Has(rp::AdminError(404, "PGRST205"), "reports.sql") && Has(rp::AdminError(401, ""), "signed out") &&
          Has(rp::AdminError(403, "permission denied"), "moderator") && Has(rp::AdminError(0, ""), "reach") &&
          Has(rp::AdminError(500, ""), "HTTP 500"));
    std::vector<rp::Row> rows;
    const std::string body =
        "[{\"id\":4,\"reporter\":\"r1\",\"target_type\":\"forum_post\",\"target_id\":\"9\",\"target_user\":\"u1\",\"reason\":\"spam\","
        "\"note\":null,\"snapshot\":\"buy now\",\"status\":\"open\",\"review_note\":null,\"created_at\":\"2026-10-25T12:00:00.123456+00:00\"},"
        "{\"id\":0,\"reason\":\"spam\"},{\"id\":5,\"target_type\":\"user\",\"target_id\":\"x\",\"reason\":\"scam\",\"note\":\"n\",\"status\":\"resolved\"}]";
    CHECK(rp::ParseRows(body, rows) && rows.size() == 2);
    CHECK(rows[0].id == 4 && rows[0].targetType == "forum_post" && rows[0].targetId == "9" && rows[0].snapshot == "buy now" &&
          rows[0].status == "open" && rows[0].created > 1700000000 && rows[0].note.empty());
    CHECK(rows[1].id == 5 && rows[1].note == "n" && rows[1].status == "resolved");
    CHECK(!rp::ParseRows("{\"message\":\"nope\"}", rows) && !rp::ParseRows("", rows));
    CHECK(std::string(rp::TargetLabel("forum_post")) == "forum comment" && std::string(rp::TargetLabel("market_item")) == "marketplace listing");
}
} // namespace

int main() {
    TestBuild();
    TestReasons();
    TestOutcomes();
    TestModeratorSide();
    TEST_MAIN_RESULT();
}
