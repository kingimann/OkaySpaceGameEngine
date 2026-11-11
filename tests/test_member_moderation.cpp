#include "test_framework.hpp"
#include "MemberModeration.hpp"

#include <string>
#include <vector>

// Launcher kick / ban / unban / promote logic (shared/MemberModeration.hpp): who may
// act on whom, the exact requests, and how a server answer becomes feedback.
using namespace okay::modmember;

namespace {
struct FakeResp { bool ok; bool reached; long status; std::string body; };
const std::string kU = "123e4567-e89b-12d3-a456-426614174000";
}

int main() {
    RUN_SUITE("member_moderation");

    // ---- permissions: every role pair ------------------------------------------
    const Role roles[] = {Role::Member, Role::Admin, Role::Owner};
    for (Role a : roles) for (Role t : roles) {
        const bool expect = (a == Role::Owner && t != Role::Owner) || (a == Role::Admin && t == Role::Member);
        CHECK(CanModerate(a, t, false) == expect);
        CHECK(CanModerate(a, t, true) == false);                    // never yourself
        CHECK(DenyReason(a, t, false).empty() == expect);
    }
    CHECK(!CanModerate(Role::None, Role::Member, false));
    CHECK(CanModerate(Role::Owner, Role::None, false));             // ban a person who already left
    CHECK(CanSetRole(Role::Owner, Role::Member, false));
    CHECK(CanSetRole(Role::Owner, Role::Admin, false));
    CHECK(!CanSetRole(Role::Owner, Role::Owner, false));
    CHECK(!CanSetRole(Role::Owner, Role::Member, true));
    CHECK(!CanSetRole(Role::Admin, Role::Member, false));           // admins cannot promote
    CHECK(!CanSetRole(Role::Member, Role::Member, false));
    CHECK(CanSeeBans(Role::Owner) && CanSeeBans(Role::Admin) && !CanSeeBans(Role::Member) && !CanSeeBans(Role::None));
    CHECK(CanUnban(Role::Admin) && !CanUnban(Role::Member));
    CHECK(ParseRole("owner") == Role::Owner && ParseRole("admin") == Role::Admin);
    CHECK(ParseRole("member") == Role::Member && ParseRole("") == Role::Member && ParseRole("root") == Role::Member);
    CHECK(DenyReason(Role::Owner, Role::Owner, true).find("yourself") != std::string::npos);
    CHECK(DenyReason(Role::Admin, Role::Owner, false).find("owner can't be removed") != std::string::npos);
    CHECK(DenyReason(Role::Member, Role::Member, false).find("owner or an admin") != std::string::npos);
    CHECK(DenyReason(Role::Admin, Role::Admin, false).find("below your role") != std::string::npos);

    // ---- request building ---------------------------------------------------------
    Request k = KickRequest(Scope::Server, 12, kU);
    CHECK(k.valid() && k.method == "POST" && k.path == "/rest/v1/rpc/kick_member");
    CHECK(k.body == "{\"p_scope\":\"server\",\"p_container\":12,\"p_target\":\"" + kU + "\"}");
    Request kg = KickRequest(Scope::Group, 7, kU);
    CHECK(kg.body.find("\"p_scope\":\"group\"") != std::string::npos && kg.body.find("\"p_container\":7") != std::string::npos);
    Request b = BanRequest(Scope::Server, 12, kU, "  spam \"links\"\n ", 3600);
    CHECK(b.path == "/rest/v1/rpc/ban_member");
    CHECK(b.body.find("\"p_reason\":\"spam \\\"links\\\"\"") != std::string::npos);   // trimmed, newline gone, quotes escaped
    CHECK(b.body.find("\"p_seconds\":3600") != std::string::npos);
    CHECK(BanRequest(Scope::Group, 1, kU, "", 0).body.find("\"p_seconds\":null") != std::string::npos);   // permanent
    CHECK(BanRequest(Scope::Group, 1, kU, "", -5).body.find("\"p_seconds\":null") != std::string::npos);
    CHECK(BanRequest(Scope::Group, 1, kU, "", 0).body.find("\"p_reason\":\"\"") != std::string::npos);
    CHECK(UnbanRequest(Scope::Server, 3, kU).path == "/rest/v1/rpc/unban_member");
    Request r = SetRoleRequest(Scope::Group, 3, kU, Role::Admin);
    CHECK(r.path == "/rest/v1/rpc/set_member_role" && r.body.find("\"p_role\":\"admin\"") != std::string::npos);
    CHECK(SetRoleRequest(Scope::Group, 3, kU, Role::Member).body.find("\"p_role\":\"member\"") != std::string::npos);
    CHECK(!SetRoleRequest(Scope::Group, 3, kU, Role::Owner).valid());   // owner is never assignable
    CHECK(!KickRequest(Scope::Server, 0, kU).valid());                  // bad container
    CHECK(!KickRequest(Scope::Server, 5, "not-a-uuid").valid());
    CHECK(!KickRequest(Scope::Server, 5, kU + "\"}").valid());          // no injection through the id
    CHECK(!BanRequest(Scope::Server, 5, "", "x", 0).valid());
    Request bl = BansListRequest(Scope::Server, 12);
    CHECK(bl.method == "GET" && bl.path.find("community_bans?scope=eq.server&container_id=eq.12") != std::string::npos);
    CHECK(!BansListRequest(Scope::Server, 0).valid());
    CHECK(CleanReason(std::string(400, 'a')).size() == 300);
    std::string utf(299, 'a'); utf += "\xC3\xA9";      // 2-byte char straddling the cap
    CHECK(CleanReason(utf) == std::string(299, 'a'));
    CHECK(CleanReason("   ").empty());

    // ---- error mapping ---------------------------------------------------------------
    Outcome o = MapResponse(true, true, 200, "true", "Kick");
    CHECK(o.ok() && o.status == Status::Accepted && o.message == "Kick done.");
    o = MapResponse(false, false, 0, "");
    CHECK(o.status == Status::Offline && !o.ok());
    o = MapResponse(false, true, 400, "{\"code\":\"P0001\",\"message\":\"You can only act on members below your role.\"}");
    CHECK(o.status == Status::Rejected && o.message == "Refused by the server: You can only act on members below your role.");
    o = MapResponse(false, true, 404, "{\"code\":\"PGRST202\",\"message\":\"Could not find the function public.kick_member(p_container, p_scope, p_target) in the schema cache\"}");
    CHECK(o.status == Status::NotApplied && o.message.find("member_moderation.sql") != std::string::npos);
    o = MapResponse(false, true, 404, "{\"code\":\"PGRST205\",\"message\":\"Could not find the table 'public.community_bans' in the schema cache\"}");
    CHECK(o.status == Status::NotApplied);
    o = MapResponse(false, true, 404, "{\"code\":\"42P01\",\"message\":\"relation \\\"community_bans\\\" does not exist\"}");
    CHECK(o.status == Status::NotApplied);
    o = MapResponse(false, true, 403, "{\"code\":\"42501\",\"message\":\"new row violates row-level security policy for table \\\"community_bans\\\"\"}");
    CHECK(o.status == Status::Rejected && o.message.find("permission") != std::string::npos);
    o = MapResponse(false, true, 401, "{\"message\":\"JWT expired\"}");
    CHECK(o.status == Status::SignedOut);
    o = MapResponse(false, true, 500, "");
    CHECK(o.status == Status::Rejected && o.message.find("HTTP 500") != std::string::npos);

    // Execute: invalid requests never reach the network; others go through api(path, method, body).
    int calls = 0; std::string seenPath, seenMethod, seenBody;
    auto api = [&](const std::string& p, const std::string& m, const std::string& bd) {
        ++calls; seenPath = p; seenMethod = m; seenBody = bd;
        return FakeResp{true, true, 200, "true"};
    };
    o = Execute(api, KickRequest(Scope::Server, 5, "bad"), "Kick");
    CHECK(calls == 0 && o.status == Status::Invalid);
    o = Execute(api, KickRequest(Scope::Server, 5, kU), "Kick");
    CHECK(calls == 1 && o.ok() && seenPath == "/rest/v1/rpc/kick_member" && seenMethod == "POST" && seenBody.find(kU) != std::string::npos);
    auto deny = [&](const std::string&, const std::string&, const std::string&) {
        return FakeResp{false, true, 400, "{\"code\":\"P0001\",\"message\":\"The owner can't be removed or banned.\"}"};
    };
    o = Execute(deny, BanRequest(Scope::Server, 5, kU, "x", 60), "Ban");
    CHECK(o.status == Status::Rejected && o.message.find("owner can't be removed") != std::string::npos);

    // ---- banned list parsing --------------------------------------------------------------
    const std::string rows =
        "[{\"user_id\":\"" + kU + "\",\"reason\":\"spam { x }\",\"until\":null,\"created_at\":\"2026-11-01T10:00:00+00:00\"},"
        "{\"user_id\":\"223e4567-e89b-12d3-a456-426614174000\",\"reason\":null,\"until\":\"2026-11-12T10:00:00+00:00\",\"created_at\":\"2026-11-01T10:00:00+00:00\"},"
        "{\"user_id\":\"323e4567-e89b-12d3-a456-426614174000\",\"reason\":\"old\",\"until\":\"2026-11-02T10:00:00+00:00\",\"created_at\":\"2026-11-01T10:00:00+00:00\"}]";
    const long now = ParseIsoUtc("2026-11-10T00:00:00Z");
    auto bans = ParseBans(rows, now);
    CHECK(bans.size() == 2);                                   // the expired one is hidden
    if (bans.size() == 2) {
        CHECK(bans[0].uid == kU && bans[0].reason == "spam { x }" && bans[0].until == 0);
        CHECK(bans[1].reason.empty() && bans[1].until == ParseIsoUtc("2026-11-12T10:00:00Z"));
    }
    CHECK(ParseBans("[]", now).empty());
    CHECK(ParseBans("{\"code\":\"x\"}", now).empty());
    CHECK(Durations().size() == 5 && Durations().back().seconds == 0);
    TEST_MAIN_RESULT();
}
