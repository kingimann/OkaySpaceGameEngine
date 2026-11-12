#include "test_framework.hpp"
#include "okay/Platform/BlockList.hpp"

#include <filesystem>
#include <string>

// Client side of docs/sql/blocks.sql: how a response is classified (SQL not run
// vs refused vs offline), how a server rule turns into a plain sentence, the
// settings JSON, and how unsent block/unblock changes layer on the server's list.
using namespace okay;
using okay::account::ApiResponse;

static ApiResponse Resp(long status, const std::string& body, bool reached = true) {
    ApiResponse r; r.reached = reached; r.status = status; r.body = body; r.ok = reached && status >= 200 && status < 300; return r;
}

int main() {
    RUN_SUITE("blocks_privacy");

    // ---- missing table vs everything else ----
    CHECK(privacy::IsMissingTable(404, "{\"code\":\"PGRST205\",\"message\":\"Could not find the table 'public.blocks' in the schema cache\"}"));
    CHECK(privacy::IsMissingTable(404, ""));
    CHECK(privacy::IsMissingTable(400, "{\"code\":\"42P01\",\"message\":\"relation \\\"public.profiles_visible\\\" does not exist\"}"));
    CHECK(!privacy::IsMissingTable(400, "{\"code\":\"42703\",\"message\":\"column profiles.foo does not exist\"}"));   // a column, not a table
    CHECK(!privacy::IsMissingTable(403, "{\"code\":\"42501\",\"message\":\"permission denied for table blocks\"}"));
    CHECK(!privacy::IsMissingTable(403, "{\"code\":\"PT403\",\"message\":\"okay_rule:dm_nobody\"}"));
    CHECK(!privacy::IsMissingTable(401, "{\"message\":\"JWT expired\"}"));
    CHECK(!privacy::IsMissingTable(500, "relation does not exist"));
    CHECK(!privacy::IsMissingTable(200, "[]"));
    CHECK(!privacy::IsMissingTable(0, ""));

    // ---- rules ----
    CHECK(privacy::ParseRule("{\"code\":\"PT403\",\"message\":\"okay_rule:dm_nobody\",\"details\":null}") == privacy::Rule::DmRefused);
    CHECK(privacy::ParseRule("okay_rule:dm_friends") == privacy::Rule::DmFriendsOnly);
    CHECK(privacy::ParseRule("{\"message\":\"okay_rule:calls_friends\"}") == privacy::Rule::CallsFriendsOnly);
    CHECK(privacy::ParseRule("okay_rule:calls_nobody\"}") == privacy::Rule::CallsRefused);
    CHECK(privacy::ParseRule("okay_rule:friend_requests_off") == privacy::Rule::FriendRequestsOff);
    CHECK(privacy::ParseRule("okay_rule:you_blocked") == privacy::Rule::YouBlocked);
    CHECK(privacy::ParseRule("okay_rule:something_new") == privacy::Rule::Unknown);
    CHECK(privacy::ParseRule("okay_rule:dm_nobody_extra") == privacy::Rule::Unknown);   // exact names only
    CHECK(privacy::ParseRule("new row violates row-level security policy") == privacy::Rule::None);
    CHECK(privacy::ParseRule("") == privacy::Rule::None);
    CHECK(std::string(privacy::RuleText(privacy::Rule::DmRefused)) == "This person does not accept messages from you.");
    CHECK(std::string(privacy::RuleText(privacy::Rule::DmFriendsOnly)).find("friends") != std::string::npos);
    CHECK(std::string(privacy::RuleText(privacy::Rule::None)).empty());
    CHECK(privacy::IsRuleText(privacy::RuleText(privacy::Rule::FriendRequestsOff)));
    CHECK(!privacy::IsRuleText("No player named 'x'."));
    CHECK(privacy::FailureText(Resp(403, "{\"message\":\"okay_rule:dm_nobody\"}"), "x") == "This person does not accept messages from you.");
    CHECK(privacy::FailureText(Resp(500, "boom"), "Message not sent.") == "Message not sent.");
    CHECK(privacy::FailureText(Resp(0, "", false), "").find("No connection") == 0);

    // ---- classifying a response ----
    CHECK(BlockList::Classify(Resp(201, "")).ok);
    { auto o = BlockList::Classify(Resp(0, "", false)); CHECK(!o.ok && o.offline && !o.missing); }
    { auto o = BlockList::Classify(Resp(404, "{\"code\":\"PGRST205\"}")); CHECK(!o.ok && o.missing && o.message.find("blocks.sql") != std::string::npos); }
    { auto o = BlockList::Classify(Resp(403, "{\"message\":\"okay_rule:you_blocked\"}")); CHECK(!o.ok && !o.missing && !o.offline && o.message.find("blocked") != std::string::npos); }
    { auto o = BlockList::Classify(Resp(403, "{\"code\":\"42501\"}")); CHECK(!o.ok && !o.missing && o.message.find("permission") != std::string::npos); }
    { auto o = BlockList::Classify(Resp(401, "{}")); CHECK(o.message.find("sign-in expired") != std::string::npos); }
    { auto o = BlockList::Classify(Resp(502, "")); CHECK(!o.ok && o.message.find("502") != std::string::npos); }

    // ---- settings JSON ----
    privacy::Settings s; s.dmFrom = 1; s.profileVis = 2; s.onlineVis = 1; s.friendRequests = 2; s.callsFrom = 0;
    const std::string j = privacy::SettingsJson(s);
    CHECK(j.find("\"dm_from\":\"friends\"") != std::string::npos);
    CHECK(j.find("\"profile_visibility\":\"nobody\"") != std::string::npos);
    CHECK(j.find("\"online_visibility\":\"friends\"") != std::string::npos);
    CHECK(j.find("\"friend_requests\":\"nobody\"") != std::string::npos);
    CHECK(j.find("\"calls_from\":\"everyone\"") != std::string::npos);
    privacy::Settings back;
    CHECK(privacy::SettingsFromBody("[" + j + "]", back));
    CHECK(back == s);
    privacy::Settings keep; keep.dmFrom = 2;
    CHECK(!privacy::SettingsFromBody("[]", keep) && keep.dmFrom == 2);   // no row: leave the local values alone
    CHECK(privacy::SettingsFromBody("[{\"dm_from\":\"nobody\"}]", back) && back.dmFrom == 2 && back.callsFrom == 1 && back.friendRequests == 0);
    CHECK(privacy::ReachFromWord("weird", 1) == 1);

    // ---- pending changes ----
    std::vector<privacy::PendingOp> ops;
    privacy::QueueOp(ops, "u1", "Bob", true);
    privacy::QueueOp(ops, "u2", "Eve", true);
    privacy::QueueOp(ops, "u1", "Bob", false);   // block then unblock: only the unblock remains
    CHECK(ops.size() == 2 && ops[1].uid == "u1" && !ops[1].block);
    const std::set<std::string> server = {"u1", "u3"};
    const auto shown = privacy::ApplyPending(server, ops);
    CHECK(shown == (std::set<std::string>{"u2", "u3"}));   // u1 unblocked locally, u2 added, u3 from the server
    CHECK(privacy::ApplyPending(server, {}) == server);
    privacy::PendingOp d;
    CHECK(privacy::DecodeOp(privacy::EncodeOp({"abc-1", "Name|with bar", true}), d) && d.uid == "abc-1" && d.block && d.name == "Name|with bar");
    CHECK(privacy::DecodeOp("-xyz", d) && !d.block && d.uid == "xyz" && d.name.empty());
    CHECK(!privacy::DecodeOp("", d) && !privacy::DecodeOp("?x|y", d) && !privacy::DecodeOp("+|y", d));

    // ---- one-time migration of the old PC-only list ----
    CHECK(privacy::ClassifyLocalList("", "alice") == privacy::LocalListAction::UploadAsLegacy);
    CHECK(privacy::ClassifyLocalList("alice", "alice") == privacy::LocalListAction::Keep);
    CHECK(privacy::ClassifyLocalList("alice", "bob") == privacy::LocalListAction::DiscardOtherAccount);

    // ---- signed out: nothing is sent, nothing is claimed ----
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "okay_blocks_privacy_test";
    std::filesystem::create_directories(dir);
    {
        okay::account::AccountService svc(dir.string());
        BlockList bl(svc);
        std::set<std::string> mine; std::map<std::string, std::string> names;
        CHECK(!bl.Fetch(mine, names).ok && !bl.Fetch(mine, names).missing);
        CHECK(!bl.Block("u", "n").ok && !bl.Unblock("u").ok);
        privacy::Settings out; bool found = true;
        CHECK(!bl.FetchSettings(out, found).ok && !found);
        CHECK(!bl.SaveSettings(out).ok);
        CHECK(bl.BlocksSupport() == privacy::Support::Unknown && bl.SettingsSupport() == privacy::Support::Unknown);
        Messages m(svc);
        CHECK(!m.Send("u", "hi") && m.LastSendError().empty());
    }
    std::filesystem::remove_all(dir);
    TEST_MAIN_RESULT();
}
