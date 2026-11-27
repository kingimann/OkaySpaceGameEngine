// OkayBackend round 6: golden request tests for the community headers, part 2 (docs/platform/okay_backend.md, sections 6,
// 10.1 and 19): Messages (direct messages, group chats, reactions, typing and read marks, search, edit history, pins) and
// Servers (servers, channels, channel messages, members, likes, owner management).
//
// Every table read and write and every database function call these two headers send is run against a fake server through
// their own public functions. The exact requests (method, URL with its query in order, header lines in order, body) and what
// the program made of each answer must equal the GOLDEN texts in test_okay_backend_golden_community2.inc, which were recorded
// from the headers BEFORE they moved onto okay::backend::Database (run with OKAY_GOLDEN_PRINT=1 to print them again). Then the
// same requests are sent through the Supabase provider directly (okay::backend::supabase::Database over a RecordingTransport)
// and must be the same bytes.
//
// Some request parts are clock times: the typing time and the read-mark time ("now") and the oldest call or invite message
// asked for (now - 90 s, now - 900 s, ...). The test writes a time within 10 seconds of now + k as <NOW> or <NOW-k>; any other
// time in a request is counted as wrong (checked at the end).
//
// No request ever leaves the process: every call goes to the test transport. The address, key, names and tokens are made up.
#include "test_framework.hpp"
#include "okay_backend_golden.hpp"   // the fake server, made-up tokens, RunGolden
#include "okay/Backend/Backend.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Friends.hpp"   // Friends::ParseIso
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Privacy.hpp"
#include "okay/Platform/Servers.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace acc = okay::account;
using golden::kUrl; using golden::kKey; using golden::Tok; using golden::Fake; using golden::Http; using golden::NoAnswer;
using golden::SaveSession;

// ---- Helpers -------------------------------------------------------------------------------------------------------------
static long g_badSteady = 0;   // clock times in a request that were not near any expected time (checked at the end)

static std::string IsoAt(long t) {
    std::time_t tt = (std::time_t)t;
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}
/// Clock times as fixed marks: a time within 10 s of now + k (k one of 0, -30, -90, -900) is <NOW> or <NOW-k>. The test's
/// own times (2023 and 2099) are only in answers, never in requests, so any other time in a request is counted as wrong.
static std::string Steady(const std::string& log) {
    static const std::regex iso("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z");
    std::string out, rest = log;
    std::smatch m;
    while (std::regex_search(rest, m, iso)) {
        out += m.prefix().str();
        const long t = okay::Friends::ParseIso(m[0].str());   // the engine's reader of these times (round 5 uses it too)
        const long now = (long)std::time(nullptr);
        std::string mark;
        for (long k : {0L, 30L, 90L, 900L})
            if (t >= now - k - 10 && t <= now - k + 10) { mark = k ? "<NOW-" + std::to_string(k) + ">" : "<NOW>"; break; }
        if (mark.empty()) { ++g_badSteady; mark = m[0].str(); }
        out += mark;
        rest = m.suffix().str();
    }
    return out + rest;
}

/// A fresh account signed in as `user` (golden::SignedInAccount), with the profiles view state back to unknown (it is one
/// value for the whole process, so each scenario starts from the same place).
static std::unique_ptr<acc::AccountService> SignedIn(const fs::path& dir, Fake& f, const std::string& user = "alex") {
    okay::privacy::ProfilesViewState() = 0;
    return golden::SignedInAccount<acc::AccountService>(dir, f, user);
}
/// A fresh account with no session file: signed out.
static std::unique_ptr<acc::AccountService> SignedOut(const fs::path& dir, Fake& f) {
    fs::create_directories(dir);
    okay::privacy::ProfilesViewState() = 0;
    return golden::MakeAccount<acc::AccountService>(dir, f);
}
static std::string B(bool v) { return v ? "1" : "0"; }
/// The parts joined in the order written: the items of a braced list are evaluated left to right (unlike the operands of
/// "+"), so the calls in one line reach the fake server in that order on every compiler.
static std::string Cat(std::initializer_list<std::string> parts) {
    std::string s;
    for (const std::string& p : parts) s += p;
    return s;
}
static std::string L(long v) { return std::to_string(v); }
static std::string Sup(okay::Messages::Support s) {
    return s == okay::Messages::Support::Yes ? "yes" : s == okay::Messages::Support::Missing ? "missing" : "unknown";
}
static std::string Dms(const std::vector<okay::DirectMessage>& v) {
    std::string s;
    for (const auto& m : v)
        s += "  dm " + L(m.id) + " [" + m.body + "] " + L(m.when) + " mine=" + B(m.mine) + " edited=" + B(m.edited) + " reply=" + L(m.replyTo) + "\n";
    return s;
}
static std::string Signals(const std::vector<okay::Messages::CallSignal>& v) {
    std::string s;
    for (const auto& c : v) s += "  signal " + L(c.id) + " from=[" + c.from + "] [" + c.body + "] " + L(c.when) + "\n";
    return s;
}
static std::string Counts(const std::map<long, long>& m) {
    std::string s = "{";
    for (const auto& kv : m) s += (s.size() > 1 ? "|" : "") + L(kv.first) + "=" + L(kv.second);
    return s + "}";
}
static std::string Flags(const std::map<long, bool>& m) {
    std::string s = "{";
    for (const auto& kv : m) s += (s.size() > 1 ? "|" : "") + L(kv.first) + "=" + B(kv.second);
    return s + "}";
}
static std::string EmojiCounts(const std::map<long, std::map<std::string, long>>& m) {
    std::string s = "{";
    for (const auto& kv : m) {
        s += (s.size() > 1 ? "|" : "") + L(kv.first) + ":";
        for (const auto& e : kv.second) s += e.first + "=" + L(e.second) + ";";
    }
    return s + "}";
}
static std::string EmojiSets(const std::map<long, std::set<std::string>>& m) {
    std::string s = "{";
    for (const auto& kv : m) {
        s += (s.size() > 1 ? "|" : "") + L(kv.first) + ":";
        for (const auto& e : kv.second) s += e + ";";
    }
    return s + "}";
}
static const char* kFar = "2099-01-01T00:00:00Z";   // a last-seen time in the future: online, whatever today's date is
static const char* kOld = "2023-11-14T22:13:20Z";   // 1700000000
static const char* kCid1 = "0123abcd-4567-89ab-cdef-0123456789ab";
static const char* kCid2 = "11111111-2222-3333-4444-555555555555";
static const char* kCid3 = "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE";

// ---- The scenarios, run through the headers' public functions ----------------------------------------------------------------
using golden::Scenario;

static std::vector<Scenario> Scenarios() {
    std::vector<Scenario> v;

    // ---- Messages: sending ------------------------------------------------------------------------------------------------
    v.push_back({"messages_send", [](const fs::path& d) {
        Fake f; f.queue = {Http(201, ""),                                                                   // with a client id
                           Http(400, "{\"code\":\"PGRST204\",\"message\":\"Could not find the 'client_id' column\"}"), Http(201, ""),
                           Http(403, "{\"message\":\"okay_rule:dm_friends\"}"),                               // the column is known missing now
                           NoAnswer("x"),
                           Http(201, "")};
        auto a = SignedIn(d, f);
        okay::Messages m(*a);
        std::string out;
        auto step = [&](bool ok) {
            out += "=> " + B(ok) + " err=[" + m.LastSendError() + "] status=" + L(m.LastSendResponse().status) + " reached=" +
                   B(m.LastSendResponse().reached) + "\n";
        };
        step(m.Send("u-2", "hi \"there\"\\ \nline\ttab\rcr\x01 end", 0, kCid1));
        step(m.Send("u-2", "again", 5, kCid2));
        step(m.Send("u-2", "third", 0, kCid3));
        step(m.Send("u 3&x", "no answer", 0, ""));
        out += Cat({"=> ", B(m.Send("u-2", "", 0, kCid1)), B(m.Send("", "x")), "\n"});
        step(m.Send("u-2", "plain", -4, "not-a-client-id"));
        return out + f.log;
    }});
    v.push_back({"messages_send_group", [](const fs::path& d) {
        Fake f; f.queue = {Http(201, ""), Http(500, "{}"),
                           Http(400, "{\"code\":\"42703\",\"message\":\"column client_id does not exist\"}"), Http(201, ""),
                           Http(201, "")};
        auto a = SignedIn(d, f, "al \"x\"");
        okay::Messages m(*a);
        std::string out;
        auto step = [&](bool ok) { out += "=> " + B(ok) + " err=[" + m.LastSendError() + "] status=" + L(m.LastSendResponse().status) + "\n"; };
        step(m.SendGroup(9, "hello \"all\"", 3, kCid1));
        step(m.SendGroup(9, "x", 0, "bad id"));
        step(m.SendGroup(9, "y", 0, kCid2));
        step(m.SendGroup(9, "z", 0, kCid3));
        out += Cat({"=> ", B(m.SendGroup(0, "x")), B(m.SendGroup(9, "")), "\n"});
        return out + f.log;
    }});

    // ---- Messages: reading the conversation -------------------------------------------------------------------------------
    v.push_back({"messages_pages", [](const fs::path& d) {
        const std::string rows = std::string("[{\"id\":3,\"sender\":\"u-2\",\"body\":\"b3\",\"created_at\":\"") + kOld + "\",\"edited\":true,\"reply_to\":2},"
                                 "{\"id\":2,\"sender\":\"u1\",\"body\":\"b \\\"2\\\"\",\"created_at\":\"" + kOld + "\",\"edited\":false,\"reply_to\":null},"
                                 "{\"id\":1,\"sender\":\"u-2\",\"body\":\"b1\"}]";
        Fake f; f.queue = {Http(200, rows), Http(200, rows), Http(200, "[]"), Http(404, "{}"),
                           Http(200, rows),
                           Http(200, "[{\"id\":9,\"sender\":\"u-2\",\"recipient\":\"u1\"},{\"id\":8,\"sender\":\"u1\",\"recipient\":\"u-3\"}]"),
                           Http(500, "{}")};
        auto a = SignedIn(d, f);
        okay::Messages m(*a);
        std::string out;
        bool more = true;
        out += Cat({Dms(m.WithPage("u-2", 0, 50, &more)), "  more=", B(more), "\n"});
        out += Cat({Dms(m.WithPage("a&b ~x/\xc3\xa9", 120, 2, &more)), "  more=", B(more), "\n"});
        out += Cat({Dms(m.WithPage("u-2", 0, 0, &more)), "  more=", B(more), "\n"});
        out += Cat({Dms(m.WithPage("u-2", 0, 5000, &more)), "  more=", B(more), "\n"});
        out += Cat({Dms(m.WithPage("", 0, 5, &more)), "  more=", B(more), "\n"});
        out += Dms(m.With("u 2&x"));
        out += Dms(m.With(""));
        for (const auto& r : m.UnreadRows()) out += "  row " + L(r.id) + " " + r.sender + " " + r.recipient + "\n";
        out += "  rows=" + std::to_string(m.UnreadRows(-1).size()) + "\n";
        return out + f.log;
    }});
    v.push_back({"messages_unread_and_conversations", [](const fs::path& d) {
        const std::string rows = std::string("[{\"id\":12,\"recipient\":\"u1\",\"sender\":\"u-2\",\"body\":\"newest\",\"created_at\":\"") + kOld + "\"},"
                                 "{\"id\":11,\"recipient\":\"u-2\",\"sender\":\"u1\",\"body\":\"mine\"},"
                                 "{\"id\":10,\"recipient\":\"u1\",\"sender\":\"u-3\",\"body\":\"older\"},"
                                 "{\"id\":4,\"recipient\":\"u1\",\"sender\":\"u-2\",\"body\":\"old\"}]";
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"), Http(200, rows), Http(200, rows), Http(500, "{}"), Http(200, rows)};
        auto a = SignedIn(d, f, "al ex+\xc3\xa9~");
        okay::Messages m(*a);
        std::string out;
        long newest = 0;
        out += Cat({"=> ", std::to_string(m.UnreadSince(5, newest)), " newest=", L(newest), "\n"});
        std::vector<okay::Messages::IncomingMessage> in;
        out += Cat({"=> ", std::to_string(m.UnreadSince(5, newest, &in)), " newest=", L(newest), "\n"});
        for (const auto& i : in) out += "  in " + L(i.id) + " " + i.from + " [" + i.body + "]\n";
        out += Cat({"=> ", std::to_string(m.UnreadSince(5, newest)), " newest=", L(newest), "\n"});
        for (const auto& c : m.Conversations())
            out += "  convo " + c.uid + " [" + c.lastBody + "] " + L(c.when) + " " + L(c.lastId) + " mine=" + B(c.lastMine) + "\n";
        out += "  me=" + m.MyUserId() + "\n";
        return out + f.log;
    }});
    // My own id cannot be found: every call that needs it asks again (it is only remembered once found), and sends nothing else.
    v.push_back({"messages_my_id_missing", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[]"), Http(500, "{}"), NoAnswer("x"), Http(200, "[]"), Http(200, "[]"), Http(200, "[]")};
        auto a = SignedIn(d, f, "ghost");
        okay::Messages m(*a);
        std::string out;
        long newest = 0;
        out += Cat({"=> ", std::to_string(m.UnreadSince(5, newest)), " ", L(newest), "\n"});
        out += "=> " + std::to_string(m.Conversations().size()) + "\n";
        out += "=> " + Flags(m.MyLikes()) + "\n";
        out += Signals(m.RecentCallSignals());
        out += "=> " + B(m.PeerTyping("u-2")) + "\n";
        out += "=> " + B(m.LeaveGroup(4)) + "\n";
        return out + f.log;
    }});
    v.push_back({"messages_calls_and_invites", [](const fs::path& d) {
        const std::string sig = std::string("[{\"id\":21,\"sender\":\"u-2\",\"body\":\"[Call] ABC key\",\"created_at\":\"") + kOld + "\"},{\"id\":20,\"sender\":\"u-3\",\"body\":\"[CallEnd] ABC 30\"}]";
        const std::string inv = "[{\"id\":31,\"sender\":\"u-2\",\"body\":\"[Invite] Arena okay://join/1\"},{\"id\":30,\"sender\":\"u-4\",\"body\":\"[Party] p-1\"}]";
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"), Http(200, sig), Http(200, inv), Http(500, "{}"), Http(200, "[]")};
        auto a = SignedIn(d, f);
        okay::Messages m(*a);
        std::string out = Signals(m.RecentCallSignals(90));
        out += Signals(m.InvitesSince(7, 900));
        out += Signals(m.RecentCallSignals(30));
        out += Signals(m.InvitesSince(-3));
        return Steady(out + f.log);
    }});

    // ---- Messages: edits, deletes and reactions ---------------------------------------------------------------------------
    v.push_back({"messages_edit_delete_reactions", [](const fs::path& d) {
        Fake f; f.queue = {Http(204, ""), Http(204, ""), Http(201, ""), Http(204, ""),
                           Http(200, "[{\"target_id\":7},{\"target_id\":7},{\"target_id\":8}]"),
                           Http(200, "[{\"id\":\"u1\"}]"), Http(200, "[{\"target_id\":7}]"),
                           Http(201, ""), Http(204, ""), Http(409, "{}"),
                           Http(200, "[{\"target_id\":7,\"emoji\":\"fire\"},{\"target_id\":7,\"emoji\":\"heart\"},{\"target_id\":7,\"emoji\":\"fire\"}]"),
                           Http(200, "[{\"target_id\":7,\"emoji\":\"fire\"},{\"target_id\":9,\"emoji\":\"a b\"}]"),
                           Http(500, "{}"), Http(500, "{}")};
        auto a = SignedIn(d, f);
        okay::Messages m(*a);
        std::string out;
        out += Cat({"=> ", B(m.Delete(7)), B(m.Delete(0)), B(m.Delete(-1)), "\n"});
        out += Cat({"=> ", B(m.Edit(7, "new \"b\"\n")), B(m.Edit(7, "")), B(m.Edit(0, "x")), "\n"});
        out += Cat({"=> ", B(m.Like(7, true)), B(m.Like(7, false)), B(m.Like(0, true)), "\n"});
        out += Cat({"=> ", Counts(m.Likes()), " ", Flags(m.MyLikes()), "\n"});
        out += Cat({"=> ", B(m.React(7, "fire", true)), B(m.React(7, "a&b c", false)), B(m.React(7, "x\"y", true)), B(m.React(7, "", true)), "\n"});
        out += Cat({"=> ", EmojiCounts(m.Reactions()), " ", EmojiSets(m.MyReactions()), "\n"});
        out += Cat({"=> ", EmojiCounts(m.Reactions()), " ", EmojiSets(m.MyReactions()), "\n"});
        return out + f.log;
    }});

    // ---- Messages: typing and read marks ----------------------------------------------------------------------------------
    v.push_back({"messages_typing_and_reads", [](const fs::path& d) {
        const std::string fresh = "[{\"at\":\"" + IsoAt((long)std::time(nullptr)) + "\"}]";
        Fake f; f.queue = {Http(201, ""), Http(404, "{\"code\":\"PGRST205\"}"),
                           Http(200, "[{\"id\":\"u1\"}]"), Http(200, fresh), Http(200, std::string("[{\"at\":\"") + kOld + "\"}]"),
                           Http(201, ""), Http(400, "{\"code\":\"42P01\",\"message\":\"relation does not exist\"}"),
                           Http(200, std::string("[{\"last_read_id\":40,\"updated_at\":\"") + kOld + "\"}]"),
                           Http(200, std::string("[{\"last_seen\":\"") + kOld + "\"}]"),
                           Http(200, "[]"), Http(404, "{}"), Http(200, std::string("[{\"last_seen\":\"") + kFar + "\"}]"),
                           Http(500, "{}"),
                           Http(200, "[{\"last_read_id\":38}]"), Http(403, "{}")};
        auto a = SignedIn(d, f);
        okay::Messages m(*a);
        std::string out;
        out += Cat({"=> ", B(m.SetTyping("u-2")), " ", Sup(m.TypingSupport()), "\n"});
        out += Cat({"=> ", B(m.SetTyping("u \"2\"")), " ", Sup(m.TypingSupport()), "\n"});
        out += "=> " + B(m.SetTyping("")) + "\n";
        out += Cat({"=> ", B(m.PeerTyping("u 2&x")), B(m.PeerTyping("u-2")), B(m.PeerTyping("")), " ", Sup(m.TypingSupport()), "\n"});
        out += Cat({"=> ", B(m.MarkRead("u-2", 42)), " ", Sup(m.ReadMarksSupport()), "\n"});
        out += Cat({"=> ", B(m.MarkRead("u-2", 0)), B(m.MarkRead("", 4)), "\n"});
        out += Cat({"=> ", B(m.MarkRead("u-3", 43)), " ", Sup(m.ReadMarksSupport()), "\n"});
        auto ps = [&](const okay::Messages::PeerState& s) {
            out += "  peer read=" + L(s.readId) + " viewing=" + L(s.viewingAt) + " seen=" + L(s.lastSeen) + " ok=" + B(s.ok) + "\n";
        };
        ps(m.PeerStateOf("u-2", true));
        ps(m.PeerStateOf("u 3&x", true));
        ps(m.PeerStateOf("u-2", false));
        ps(m.PeerStateOf("", true));
        out += Cat({"=> ", L(m.PeerReadId("u-2")), " ", L(m.PeerReadId("u-2")), " ", L(m.PeerReadId("")), "\n"});
        return Steady(out + f.log);
    }});

    // ---- Messages: group chats --------------------------------------------------------------------------------------------
    v.push_back({"messages_groups", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":12,\"title\":\"Team\"}]"), Http(500, "{}"),
                           Http(200, std::string("[{\"convo_id\":12,\"title\":\"Team\",\"owner\":\"u1\",\"member_count\":3,\"last_id\":40,\"last_body\":\"hey\","
                                     "\"last_sender\":\"bo\",\"last_when\":\"") + kOld + "\",\"last_read\":38},{\"convo_id\":0}]"),
                           Http(200, "[{\"member\":\"u1\"},{\"member\":\"\"},{\"member\":\"u-2\"}]"),
                           Http(400, "{\"message\":\"column role does not exist\"}"),
                           Http(200, "[{\"member\":\"u1\",\"last_read\":5},{\"member\":\"u-2\",\"last_read\":3},{\"member\":\"\"}]"),
                           Http(200, std::string("[{\"id\":\"u-2\",\"username\":\"bo\",\"display_name\":\"Bo\",\"avatar_url\":\"https://a.example.test/b.png\",\"last_seen\":\"") + kOld + "\"},"
                                     "{\"id\":\"u1\",\"username\":\"alex\",\"display_name\":null}]"),
                           Http(200, "[{\"member\":\"u-2\",\"last_read\":3,\"role\":\"admin\"}]"), Http(404, "{}"), Http(200, "[{\"id\":\"u-2\",\"username\":\"bo\"}]"),
                           Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, std::string("[{\"id\":40,\"sender\":\"u1\",\"sender_name\":\"alex\",\"body\":\"hi\",\"created_at\":\"") + kOld + "\",\"edited\":true,\"reply_to\":39},"
                                     "{\"id\":41,\"sender\":\"u-2\",\"sender_name\":\"bo\",\"body\":\"yo\"}]"),
                           Http(204, ""), Http(204, ""), Http(204, ""), Http(204, ""), Http(204, ""), Http(204, ""), Http(204, ""),
                           Http(201, ""), Http(204, ""),
                           Http(200, "[{\"target_id\":40,\"emoji\":\"heart\"}]"), Http(200, "[{\"target_id\":40,\"emoji\":\"heart\"}]")};
        auto a = SignedIn(d, f);
        okay::Messages m(*a);
        std::string out;
        out += Cat({"=> ", L(m.CreateGroup("Team \"A\"\n", {"u-2", "u \"3\""})), " ", L(m.CreateGroup("", {})), "\n"});
        for (const auto& g : m.MyGroups())
            out += "  group " + L(g.id) + " [" + g.title + "] " + g.owner + " " + std::to_string(g.members) + " " + L(g.lastId) + " [" + g.lastBody +
                   "] " + g.lastSender + " " + L(g.when) + " " + L(g.lastRead) + "\n";
        for (const auto& id : m.GroupMemberIds(12)) out += "  member " + id + "\n";
        out += "  ids0=" + std::to_string(m.GroupMemberIds(0).size()) + "\n";
        auto info = [&](long convo) {
            for (const auto& g : m.GroupMembersInfo(convo))
                out += "  info " + g.uid + " user=[" + g.username + "] name=[" + g.name + "] avatar=[" + g.avatar + "] role=[" + g.role + "] read=" +
                       L(g.lastRead) + " seen=" + L(g.lastSeen) + "\n";
        };
        info(12);
        info(13);
        for (const auto& g : m.GroupThread(12))
            out += "  gm " + L(g.id) + " " + g.sender + " [" + g.senderName + "] [" + g.body + "] " + L(g.when) + " mine=" + B(g.mine) + " edited=" +
                   B(g.edited) + " reply=" + L(g.replyTo) + "\n";
        out += Cat({"=> ", B(m.EditGroup(40, "fixed \"t\"")), B(m.EditGroup(0, "x")), B(m.DeleteGroup(40)), B(m.DeleteGroup(0)), "\n"});
        out += Cat({"=> ", B(m.AddGroupMember(12, "u-4")), B(m.AddGroupMember(12, "")), B(m.LeaveGroup(12)), B(m.LeaveGroup(0)), "\n"});
        out += Cat({"=> ", B(m.RenameGroup(12, "New \"T\"")), B(m.RenameGroup(12, "")), B(m.MarkGroupRead(12, 41)), B(m.MarkGroupRead(12, 0)), "\n"});
        out += Cat({"=> ", B(m.GroupReact(40, "heart", true)), B(m.GroupReact(40, "x y&z", false)), B(m.GroupReact(0, "heart", true)), "\n"});
        out += Cat({"=> ", EmojiCounts(m.GroupReactions()), " ", EmojiSets(m.GroupMyReactions()), "\n"});
        return out + f.log;
    }});

    // ---- Messages: search, edit history and pins --------------------------------------------------------------------------
    v.push_back({"messages_search_edits_pins", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, std::string("[{\"id\":5,\"sender\":\"u1\",\"recipient\":\"u-2\",\"body\":\"hello world\",\"created_at\":\"") + kOld + "\"},"
                                     "{\"id\":4,\"sender\":\"u-3\",\"recipient\":\"u1\",\"body\":\"hello\",\"created_at\":\"2023-11-14T22:13:10Z\"}]"),
                           Http(200, "[{\"id\":9,\"convo_id\":12,\"sender\":\"u-2\",\"sender_name\":\"bo\",\"body\":\"hello all\",\"created_at\":\"2023-11-14T22:13:30Z\"}]"),
                           Http(500, "{}"),
                           Http(200, "[{\"id\":9,\"convo_id\":12,\"sender\":\"u1\",\"sender_name\":\"alex\",\"body\":\"a\",\"created_at\":\"2023-11-14T22:13:30Z\"},"
                                     "{\"id\":8,\"convo_id\":12,\"sender\":\"u1\",\"sender_name\":\"alex\",\"body\":\"b\",\"created_at\":\"2023-11-14T22:13:20Z\"}]"),
                           Http(201, ""),
                           Http(200, std::string("[{\"old_body\":\"v1\",\"edited_at\":\"") + kOld + "\"},{\"old_body\":\"v \\\"2\\\"\"}]"),
                           Http(404, "{}"), Http(500, "{}"),
                           Http(201, ""), Http(204, ""), Http(204, ""),
                           Http(200, std::string("[{\"target_id\":5,\"pinned_by\":\"u1\",\"created_at\":\"") + kOld + "\"}]"),
                           Http(400, "{}")};
        auto a = SignedIn(d, f);
        okay::Messages m(*a);
        std::string out;
        auto hits = [&](const std::vector<okay::Messages::SearchHit>& v) {
            for (const auto& h : v)
                out += "  hit " + h.scope + " " + L(h.id) + " uid=[" + h.uid + "] group=" + L(h.groupId) + " [" + h.senderName + "] [" + h.body + "] " +
                       L(h.when) + " mine=" + B(h.mine) + "\n";
            out += "  --\n";
        };
        hits(m.Search("he*llo %w\xc3\xb6rld~&=x", 40));
        hits(m.Search("a b", 1));
        hits(m.Search("*%", 10));
        out += Cat({"=> ", B(m.RecordEdit("dm", 5, "old \"t\"\nx")), B(m.RecordEdit("group", 0, "x")), "\n"});
        bool sup = false;
        auto hist = [&](const std::vector<okay::Messages::EditVersion>& v) {
            for (const auto& e : v) out += "  version [" + e.body + "] " + L(e.when) + "\n";
            out += "  supported=" + B(sup) + "\n";
        };
        hist(m.EditHistory("dm", 5, &sup));
        hist(m.EditHistory("gr oup&x", 5, &sup));
        hist(m.EditHistory("group", 6, &sup));
        hist(m.EditHistory("dm", 0, &sup));
        out += Cat({"=> ", B(m.Pin("dm", 5, true)), B(m.Pin("dm", 5, false)), B(m.Pin("a&b", 6, false)), B(m.Pin("dm", 0, true)), "\n"});
        for (const auto& p : m.Pins("dm", &sup)) out += "  pin " + L(p.id) + " " + L(p.when) + " " + p.by + "\n";
        out += "  supported=" + B(sup) + "\n";
        out += Cat({"  pins=", std::to_string(m.Pins("group", &sup).size()), " supported=", B(sup), "\n"});
        return out + f.log;
    }});

    // ---- Messages: signed out sends nothing ---------------------------------------------------------------------------------
    v.push_back({"messages_signed_out", [](const fs::path& d) {
        Fake f;
        auto a = SignedOut(d, f);
        okay::Messages m(*a);
        std::string out;
        long newest = 3;
        bool more = true, sup = false;
        out += Cat({B(m.Available()), B(m.Send("u-2", "x", 0, kCid1)), B(m.SendGroup(1, "x")), B(m.Delete(1)), B(m.Edit(1, "x")), B(m.Like(1, true)),
                    B(m.React(1, "e", false)), B(m.SetTyping("u-2")), B(m.PeerTyping("u-2")), B(m.MarkRead("u-2", 1)), B(m.EditGroup(1, "x")),
                    B(m.DeleteGroup(1)), B(m.AddGroupMember(1, "u")), B(m.LeaveGroup(1)), B(m.RenameGroup(1, "t")), B(m.MarkGroupRead(1, 1)),
                    B(m.GroupReact(1, "e", true)), B(m.RecordEdit("dm", 1, "x")), B(m.Pin("dm", 1, true)), "\n"});
        out += Cat({std::to_string(m.WithPage("u-2", 0, 5, &more).size()), B(more), std::to_string(m.UnreadRows().size()),
                    std::to_string(m.RecentCallSignals().size()), std::to_string(m.InvitesSince(0).size()), std::to_string(m.UnreadSince(1, newest)),
                    L(newest), std::to_string(m.Conversations().size()), std::to_string(m.With("u-2").size()), std::to_string(m.Likes().size()),
                    std::to_string(m.MyLikes().size()), std::to_string(m.Reactions().size()), std::to_string(m.MyReactions().size()),
                    L(m.PeerReadId("u-2")), B(m.PeerStateOf("u-2", true).ok), L(m.CreateGroup("t", {})), std::to_string(m.MyGroups().size()),
                    std::to_string(m.GroupMemberIds(1).size()), std::to_string(m.GroupMembersInfo(1).size()), std::to_string(m.GroupThread(1).size()),
                    std::to_string(m.GroupReactions().size()), std::to_string(m.GroupMyReactions().size()), std::to_string(m.Search("x").size()),
                    std::to_string(m.EditHistory("dm", 1, &sup).size()), B(sup), std::to_string(m.Pins("dm", &sup).size()), B(sup), "[",
                    m.MyUserId(), "] [", m.LastSendError(), "]\n"});
        return out + f.log;
    }});

    // ---- Servers ------------------------------------------------------------------------------------------------------------
    v.push_back({"servers_list_create_join", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":4,\"name\":\"Den \\\"1\\\"\",\"invite_code\":\"AB12\",\"owner\":\"u1\"},{\"id\":0,\"name\":\"skip\"},{\"id\":5,\"name\":\"Two\",\"owner\":\"u-2\"}]"),
                           Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, "[{\"id\":4,\"name\":\"Den\",\"invite_code\":\"AB12\"}]"), Http(409, "dup name"), Http(500, ""),
                           Http(200, "[{\"id\":4}]"), Http(200, "null"), Http(400, "bad code"), Http(400, ""),
                           Http(500, "{}")};
        auto a = SignedIn(d, f, "al ex&~");
        okay::Servers s(*a);
        std::string out;
        for (const auto& x : s.MyServers()) out += "  server " + L(x.id) + " [" + x.name + "] " + x.invite + " " + x.owner + "\n";
        out += Cat({"  me=", s.MyUid(), " again=", s.MyUid(), " available=", B(s.Available()), "\n"});
        auto create = [&](const std::string& name) {
            okay::Server c; std::string err;
            const bool ok = s.CreateServer(name, &c, &err);
            out += "=> " + B(ok) + " " + L(c.id) + " [" + c.name + "] " + c.invite + " err=[" + err + "]\n";
        };
        create("Den \"1\"\n");
        create("x");
        create("y");
        create("");
        for (const char* code : {"AB12", "ZZ", "Q&1 \"", "e"}) {
            std::string err;
            const bool ok = s.JoinServer(code, &err);
            out += "=> " + B(ok) + " err=[" + err + "]\n";
        }
        out += "  servers=" + std::to_string(s.MyServers().size()) + "\n";
        return out + f.log;
    }});
    v.push_back({"servers_channels_members", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":9,\"name\":\"general\"},{\"id\":0,\"name\":\"skip\"},{\"id\":10,\"name\":\"voice \\\"x\\\"\"}]"),
                           Http(200, "[{\"member\":\"u-2\",\"role\":\"member\"},{\"member\":\"u1\",\"role\":\"owner\"},{\"member\":\"\"},{\"member\":\"u 3&x\",\"role\":\"admin\"}]"),
                           Http(404, "{\"code\":\"PGRST205\"}"),
                           Http(200, std::string("[{\"id\":\"u-2\",\"username\":\"bo\",\"display_name\":\"Bo\",\"avatar_url\":\"https://a.example.test/b.png\",\"last_seen\":\"") + kFar + "\"},"
                                     "{\"id\":\"u1\",\"username\":\"alex\",\"last_seen\":\"2000-01-01T00:00:00Z\"},{\"id\":\"u 3&x\",\"username\":\"cy\",\"last_seen\":\"" + kOld + "\"}]"),
                           Http(200, "[{\"member\":\"u-2\",\"role\":\"member\"}]"), Http(500, "{}"),
                           Http(500, "{}"),
                           Http(201, ""),
                           Http(200, std::string("[{\"id\":3,\"author_name\":\"bo\",\"body\":\"hi\",\"created_at\":\"") + kOld + "\",\"edited\":true},{\"id\":4,\"author_name\":\"old@mail.example.test\",\"body\":\"x\"}]"),
                           Http(200, "[]"), Http(201, ""), Http(204, "")};
        auto a = SignedIn(d, f);
        okay::Servers s(*a);
        std::string out;
        for (const auto& c : s.Channels(4)) out += "  channel " + L(c.id) + " [" + c.name + "]\n";
        out += "  channels0=" + std::to_string(s.Channels(0).size()) + "\n";
        auto members = [&](long id) {
            for (const auto& m : s.Members(id))
                out += "  member " + m.uid + " [" + m.name + "] [" + m.avatar + "] " + m.role + " online=" + B(m.online) + " seen=" + L(m.lastSeen) + "\n";
            out += "  --\n";
        };
        members(4);
        members(5);
        members(6);
        members(0);
        out += Cat({"=> ", B(s.CreateChannel(4, "general \"x\"\n")), B(s.CreateChannel(0, "x")), B(s.CreateChannel(4, "")), "\n"});
        for (const auto& m : s.Messages(9))
            out += "  cm " + L(m.id) + " [" + m.author + "] [" + m.body + "] " + L(m.when) + " edited=" + B(m.edited) + "\n";
        out += Cat({"  msgs=", std::to_string(s.Messages(9, -5).size()), std::to_string(s.Messages(0).size()), "\n"});
        out += Cat({"=> ", B(s.Post(9, "hello\n\"x\"")), B(s.Post(9, "")), B(s.Post(0, "x")), "\n"});
        out += Cat({"=> ", B(s.EditMessage(3, "edited \"e\"")), B(s.EditMessage(0, "x")), B(s.EditMessage(3, "")), "\n"});
        return out + f.log;
    }});
    v.push_back({"servers_likes_and_owner", [](const fs::path& d) {
        Fake f; f.queue = {Http(201, ""), Http(204, ""),
                           Http(200, "[{\"target_id\":3},{\"target_id\":3},{\"target_id\":4}]"),
                           Http(200, "[{\"id\":\"u1\"}]"), Http(200, "[{\"target_id\":3}]"),
                           Http(204, ""), Http(204, ""), Http(204, ""), Http(204, ""), Http(204, ""), Http(403, "{}")};
        auto a = SignedIn(d, f);
        okay::Servers s(*a);
        std::string out;
        out += Cat({"=> ", B(s.Like(3, true)), B(s.Like(3, false)), B(s.Like(0, true)), "\n"});
        out += Cat({"=> ", Counts(s.Likes()), " ", Flags(s.MyLikes()), "\n"});
        out += Cat({"=> ", B(s.Leave(4)), B(s.Leave(0)), "\n"});
        out += Cat({"=> ", B(s.RenameChannel(9, "renamed \"c\"")), B(s.RenameChannel(9, "")), B(s.DeleteChannel(9)), B(s.DeleteChannel(0)), "\n"});
        out += Cat({"=> ", B(s.RenameServer(4, "S")), B(s.RenameServer(0, "S")), B(s.DeleteServer(4)), B(s.DeleteServer(-1)), "\n"});
        return out + f.log;
    }});
    v.push_back({"servers_signed_out", [](const fs::path& d) {
        Fake f;
        auto a = SignedOut(d, f);
        okay::Servers s(*a);
        std::string err1, err2;
        std::string out = Cat({B(s.Available()), std::to_string(s.MyServers().size()), "[", s.MyUid(), "]", B(s.CreateServer("x", nullptr, &err1)),
                               B(s.JoinServer("c", &err2)), std::to_string(s.Channels(1).size()), std::to_string(s.Members(1).size()),
                               B(s.CreateChannel(1, "c")), std::to_string(s.Messages(1).size()), B(s.Post(1, "x")), B(s.EditMessage(1, "x")),
                               B(s.Like(1, true)), std::to_string(s.Likes().size()), std::to_string(s.MyLikes().size()), B(s.Leave(1)),
                               B(s.RenameChannel(1, "c")), B(s.DeleteChannel(1)), B(s.RenameServer(1, "s")), B(s.DeleteServer(1))});
        out += " [" + err1 + "] [" + err2 + "]\n";
        return out + f.log;
    }});
    return v;
}

// ---- The same requests through the Supabase provider directly (okay::backend::supabase::Database, RecordingTransport) ------
// Each flow sends, in order, the requests its scenario of the same name sent (the user-id lookups included), built only with
// the Query builder: a builder or provider change that alters one byte fails here even when a header still matches.
using ProviderRun = golden::DbRecorder;   // supabase::Database straight to the recorder, signed in as u1
using ProviderFlow = std::function<void(ProviderRun&)>;
static std::vector<std::pair<const char*, ProviderFlow>> ProviderFlows() {
    namespace ob = okay::backend;
    using Q = ob::Query;
    const auto asIs = ob::ValueText::AsIs;
    static const std::string kPage = "id,sender,body,created_at,edited,reply_to";
    static const std::string kIgnore = "resolution=ignore-duplicates";
    std::vector<std::pair<const char*, ProviderFlow>> v;
    v.push_back({"messages_send", [=](ProviderRun& p) {
        p.Db().From("messages").OnConflict("sender,client_id").Prefer(kIgnore)
            .Insert("{\"recipient\":\"u-2\",\"body\":\"hi \\\"there\\\"\\\\ \\nline\\ttabcr\x01 end\",\"client_id\":\"" + std::string(kCid1) + "\"}");
        p.Db().From("messages").OnConflict("sender,client_id").Prefer(kIgnore)
            .Insert("{\"recipient\":\"u-2\",\"body\":\"again\",\"reply_to\":5,\"client_id\":\"" + std::string(kCid2) + "\"}");
        p.Db().From("messages").Insert("{\"recipient\":\"u-2\",\"body\":\"again\",\"reply_to\":5}");
        p.Db().From("messages").Insert("{\"recipient\":\"u-2\",\"body\":\"third\"}");
        p.Db().From("messages").Insert("{\"recipient\":\"u 3&x\",\"body\":\"no answer\"}");
        p.Db().From("messages").Insert("{\"recipient\":\"u-2\",\"body\":\"plain\"}");
    }});
    v.push_back({"messages_send_group", [=](ProviderRun& p) {
        p.Db().From("group_messages").OnConflict("sender,client_id").Prefer(kIgnore)
            .Insert("{\"convo_id\":9,\"body\":\"hello \\\"all\\\"\",\"sender_name\":\"al \\\"x\\\"\",\"reply_to\":3,\"client_id\":\"" + std::string(kCid1) + "\"}");
        p.Db().From("group_messages").Insert("{\"convo_id\":9,\"body\":\"x\",\"sender_name\":\"al \\\"x\\\"\"}");
        p.Db().From("group_messages").OnConflict("sender,client_id").Prefer(kIgnore)
            .Insert("{\"convo_id\":9,\"body\":\"y\",\"sender_name\":\"al \\\"x\\\"\",\"client_id\":\"" + std::string(kCid2) + "\"}");
        p.Db().From("group_messages").Insert("{\"convo_id\":9,\"body\":\"y\",\"sender_name\":\"al \\\"x\\\"\"}");
        p.Db().From("group_messages").Insert("{\"convo_id\":9,\"body\":\"z\",\"sender_name\":\"al \\\"x\\\"\"}");
    }});
    v.push_back({"messages_pages", [=](ProviderRun& p) {
        auto page = [&](const std::string& enc, const char* before, long limitPlusOne) {
            auto db = p.Db();   // must outlive the Query made from it
            Q q = db.From("messages");
            q.Select(kPage).Or({Q::Cond("sender", "eq", enc, asIs), Q::Cond("recipient", "eq", enc, asIs)});
            if (before) q.Lt("id", before);
            q.Order("id", true).Limit(limitPlusOne).Get();
        };
        page("u-2", nullptr, 51);
        page("a%26b%20%7Ex%2F%C3%A9", "120", 3);
        page("u-2", nullptr, 2);
        page("u-2", nullptr, 201);
        p.Db().From("messages").Select(kPage).Or({Q::Cond("sender", "eq", "u 2&x", asIs), Q::Cond("recipient", "eq", "u 2&x", asIs)})
            .Order("created_at").Get();
        p.Db().From("messages").Select("id,sender,recipient").Order("id", true).Limit(300).Get();
        p.Db().From("messages").Select("id,sender,recipient").Order("id", true).Limit(-1).Get();
    }});
    v.push_back({"messages_unread_and_conversations", [=](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "al ex+\xc3\xa9~").Get();
        p.Db().From("messages").Select("id,recipient").Order("id", true).Limit(60).Get();
        p.Db().From("messages").Select("id,recipient,sender,body").Order("id", true).Limit(60).Get();
        p.Db().From("messages").Select("id,recipient").Order("id", true).Limit(60).Get();
        p.Db().From("messages").Select("id,sender,recipient,body,created_at").Order("id", true).Limit(300).Get();
    }});
    v.push_back({"messages_my_id_missing", [=](ProviderRun& p) {
        for (int i = 0; i < 6; ++i) p.Db().From("profiles").Select("id").Eq("username", "ghost").Get();
    }});
    v.push_back({"messages_calls_and_invites", [=](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("messages").Select("id,sender,body,created_at").Eq("recipient", "u1", asIs).Like("body", "%5BCall*", asIs)
            .Gte("created_at", "<NOW-90>", asIs).Order("id", true).Limit(10).Get();
        p.Db().From("messages").Select("id,sender,body,created_at").Eq("recipient", "u1", asIs)
            .Or({Q::Cond("body", "like", "%5BInvite%5D*", asIs), Q::Cond("body", "like", "%5BParty%5D*", asIs)}).Gt("id", "7")
            .Gte("created_at", "<NOW-900>", asIs).Order("id", true).Limit(10).Get();
        p.Db().From("messages").Select("id,sender,body,created_at").Eq("recipient", "u1", asIs).Like("body", "%5BCall*", asIs)
            .Gte("created_at", "<NOW-30>", asIs).Order("id", true).Limit(10).Get();
        p.Db().From("messages").Select("id,sender,body,created_at").Eq("recipient", "u1", asIs)
            .Or({Q::Cond("body", "like", "%5BInvite%5D*", asIs), Q::Cond("body", "like", "%5BParty%5D*", asIs)}).Gt("id", "-3")
            .Gte("created_at", "<NOW-900>", asIs).Order("id", true).Limit(10).Get();
    }});
    v.push_back({"messages_edit_delete_reactions", [=](ProviderRun& p) {
        p.Db().From("messages").Eq("id", "7").Delete();
        p.Db().From("messages").Eq("id", "7").Update("{\"body\":\"new \\\"b\\\"\\n\",\"edited\":true}");
        p.Db().From("message_reactions").Prefer(kIgnore).Insert("{\"scope\":\"dm\",\"target_id\":7,\"emoji\":\"heart\"}");
        p.Db().From("message_reactions").Eq("scope", "dm").Eq("target_id", "7").Eq("emoji", "heart").Delete();
        p.Db().From("message_reactions").Select("target_id").Eq("scope", "dm").Eq("emoji", "heart").Get();
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("message_reactions").Select("target_id").Eq("scope", "dm").Eq("emoji", "heart").Eq("reactor", "u1", asIs).Get();
        p.Db().From("message_reactions").Prefer(kIgnore).Insert("{\"scope\":\"dm\",\"target_id\":7,\"emoji\":\"fire\"}");
        p.Db().From("message_reactions").Eq("scope", "dm").Eq("target_id", "7").Eq("emoji", "a&b c", asIs).Delete();
        p.Db().From("message_reactions").Prefer(kIgnore).Insert("{\"scope\":\"dm\",\"target_id\":7,\"emoji\":\"x\\\"y\"}");
        for (int i = 0; i < 2; ++i) {
            p.Db().From("message_reactions").Select("target_id,emoji").Eq("scope", "dm").Get();
            p.Db().From("message_reactions").Select("target_id,emoji").Eq("scope", "dm").Eq("reactor", "u1", asIs).Get();
        }
    }});
    v.push_back({"messages_typing_and_reads", [=](ProviderRun& p) {
        p.Db().From("typing_state").OnConflict("sender").Upsert("{\"peer\":\"u-2\",\"at\":\"<NOW>\"}");
        p.Db().From("typing_state").OnConflict("sender").Upsert("{\"peer\":\"u \\\"2\\\"\",\"at\":\"<NOW>\"}");
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("typing_state").Select("at").Eq("sender", "u 2&x", asIs).Eq("peer", "u1", asIs).Get();
        p.Db().From("typing_state").Select("at").Eq("sender", "u-2", asIs).Eq("peer", "u1", asIs).Get();
        p.Db().From("message_reads").OnConflict("reader,peer").Upsert("{\"peer\":\"u-2\",\"last_read_id\":42,\"updated_at\":\"<NOW>\"}");
        p.Db().From("message_reads").OnConflict("reader,peer").Upsert("{\"peer\":\"u-3\",\"last_read_id\":43,\"updated_at\":\"<NOW>\"}");
        p.Db().From("message_reads").Select("last_read_id,updated_at").Eq("reader", "u-2", asIs).Eq("peer", "u1", asIs).Get();
        p.Db().From("profiles_visible").Select("last_seen").Eq("id", "u-2", asIs).Get();
        p.Db().From("message_reads").Select("last_read_id,updated_at").Eq("reader", "u 3&x", asIs).Eq("peer", "u1", asIs).Get();
        p.Db().From("profiles_visible").Select("last_seen").Eq("id", "u 3&x", asIs).Get();
        p.Db().From("profiles").Select("last_seen").Eq("id", "u 3&x", asIs).Get();
        p.Db().From("message_reads").Select("last_read_id,updated_at").Eq("reader", "u-2", asIs).Eq("peer", "u1", asIs).Get();
        for (int i = 0; i < 2; ++i) p.Db().From("message_reads").Select("last_read_id").Eq("reader", "u-2", asIs).Eq("peer", "u1", asIs).Get();
    }});
    v.push_back({"messages_groups", [=](ProviderRun& p) {
        p.Db().Rpc("create_group", "{\"p_title\":\"Team \\\"A\\\"\\n\",\"p_members\":[\"u-2\",\"u \\\"3\\\"\"]}");
        p.Db().Rpc("create_group", "{\"p_title\":\"\",\"p_members\":[]}");
        p.Db().Rpc("my_groups", "{}");
        p.Db().From("group_members").Eq("convo_id", "12").Select("member").Get();
        p.Db().From("group_members").Eq("convo_id", "12").Select("member,last_read,role").Get();
        p.Db().From("group_members").Eq("convo_id", "12").Select("member,last_read").Get();
        p.Db().From("profiles_visible").Select("id,username,display_name,avatar_url,last_seen").In("id", {"u1", "u-2"}, asIs).Get();
        p.Db().From("group_members").Eq("convo_id", "13").Select("member,last_read,role").Get();
        p.Db().From("profiles_visible").Select("id,username,display_name,avatar_url,last_seen").In("id", {"u-2"}, asIs).Get();
        p.Db().From("profiles").Select("id,username,display_name,avatar_url,last_seen").In("id", {"u-2"}, asIs).Get();
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("group_messages").Eq("convo_id", "12").Select("id,sender,sender_name,body,created_at,edited,reply_to").Order("created_at").Get();
        p.Db().From("group_messages").Eq("id", "40").Update("{\"body\":\"fixed \\\"t\\\"\",\"edited\":true}");
        p.Db().From("group_messages").Eq("id", "40").Delete();
        p.Db().Rpc("add_group_member", "{\"p_convo\":12,\"p_member\":\"u-4\"}");
        p.Db().From("group_members").Eq("convo_id", "12").Eq("member", "u1", asIs).Delete();
        p.Db().From("group_convos").Eq("id", "12").Update("{\"title\":\"New \\\"T\\\"\"}");
        p.Db().From("group_convos").Eq("id", "12").Update("{\"title\":\"\"}");
        p.Db().From("group_members").Eq("convo_id", "12").Eq("member", "u1", asIs).Update("{\"last_read\":41}");
        p.Db().From("message_reactions").Prefer(kIgnore).Insert("{\"scope\":\"group\",\"target_id\":40,\"emoji\":\"heart\"}");
        p.Db().From("message_reactions").Eq("scope", "group").Eq("target_id", "40").Eq("emoji", "x y&z", asIs).Delete();
        p.Db().From("message_reactions").Select("target_id,emoji").Eq("scope", "group").Get();
        p.Db().From("message_reactions").Select("target_id,emoji").Eq("scope", "group").Eq("reactor", "u1", asIs).Get();
    }});
    v.push_back({"messages_search_edits_pins", [=](ProviderRun& p) {
        auto search = [&](const std::string& pattern, long lim) {
            p.Db().From("messages").Select("id,sender,recipient,body,created_at").Ilike("body", pattern, asIs).Order("id", true).Limit(lim).Get();
            p.Db().From("group_messages").Select("id,convo_id,sender,sender_name,body,created_at").Ilike("body", pattern, asIs)
                .Order("id", true).Limit(lim).Get();
        };
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        search("*hello%20w%C3%B6rld~%26%3Dx*", 40);
        search("*a%20b*", 1);
        p.Db().From("message_edits").Insert("{\"scope\":\"dm\",\"target_id\":5,\"old_body\":\"old \\\"t\\\"\\nx\"}");
        p.Db().From("message_edits").Select("old_body,edited_at").Eq("scope", "dm", asIs).Eq("target_id", "5").Order("id").Get();
        p.Db().From("message_edits").Select("old_body,edited_at").Eq("scope", "gr oup&x", asIs).Eq("target_id", "5").Order("id").Get();
        p.Db().From("message_edits").Select("old_body,edited_at").Eq("scope", "group", asIs).Eq("target_id", "6").Order("id").Get();
        p.Db().From("message_pins").Prefer(kIgnore).Insert("{\"scope\":\"dm\",\"target_id\":5}");
        p.Db().From("message_pins").Eq("scope", "dm", asIs).Eq("target_id", "5").Delete();
        p.Db().From("message_pins").Eq("scope", "a&b", asIs).Eq("target_id", "6").Delete();
        p.Db().From("message_pins").Select("target_id,pinned_by,created_at").Eq("scope", "dm", asIs).Order("created_at", true).Limit(500).Get();
        p.Db().From("message_pins").Select("target_id,pinned_by,created_at").Eq("scope", "group", asIs).Order("created_at", true).Limit(500).Get();
    }});
    v.push_back({"servers_list_create_join", [=](ProviderRun& p) {
        p.Db().From("servers").Select("id,name,invite_code,owner").Order("created_at").Get();
        p.Db().From("profiles").Select("id").Eq("username", "al ex&~").Get();
        p.Db().Rpc("create_server", "{\"p_name\":\"Den \\\"1\\\"\\n\"}");
        p.Db().Rpc("create_server", "{\"p_name\":\"x\"}");
        p.Db().Rpc("create_server", "{\"p_name\":\"y\"}");
        for (const char* body : {"{\"p_code\":\"AB12\"}", "{\"p_code\":\"ZZ\"}", "{\"p_code\":\"Q&1 \\\"\"}", "{\"p_code\":\"e\"}"}) p.Db().Rpc("join_server", body);
        p.Db().From("servers").Select("id,name,invite_code,owner").Order("created_at").Get();
    }});
    v.push_back({"servers_channels_members", [=](ProviderRun& p) {
        static const std::string kMember = "id,username,display_name,avatar_url,last_seen";
        p.Db().From("channels").Eq("server_id", "4").Select("id,name").Order("position").Order("id").Get();
        p.Db().From("server_members").Eq("server_id", "4").Select("member,role").Get();
        p.Db().From("profiles_visible").Select(kMember).In("id", {"u-2", "u1", "u 3&x"}, asIs).Get();
        p.Db().From("profiles").Select(kMember).In("id", {"u-2", "u1", "u 3&x"}, asIs).Get();
        p.Db().From("server_members").Eq("server_id", "5").Select("member,role").Get();
        p.Db().From("profiles").Select(kMember).In("id", {"u-2"}, asIs).Get();
        p.Db().From("server_members").Eq("server_id", "6").Select("member,role").Get();
        p.Db().From("channels").Insert("{\"server_id\":4,\"name\":\"general \\\"x\\\"\\n\"}");
        p.Db().From("channel_messages").Eq("channel_id", "9").Select("id,author_name,body,created_at,edited").Order("created_at").Limit(60).Get();
        p.Db().From("channel_messages").Eq("channel_id", "9").Select("id,author_name,body,created_at,edited").Order("created_at").Limit(-5).Get();
        p.Db().From("channel_messages").Insert("{\"channel_id\":9,\"body\":\"hello\\n\\\"x\\\"\",\"author_name\":\"alex\"}");
        p.Db().From("channel_messages").Eq("id", "3").Update("{\"body\":\"edited \\\"e\\\"\",\"edited\":true}");
    }});
    v.push_back({"servers_likes_and_owner", [=](ProviderRun& p) {
        p.Db().From("message_reactions").Prefer(kIgnore).Insert("{\"scope\":\"channel\",\"target_id\":3,\"emoji\":\"heart\"}");
        p.Db().From("message_reactions").Eq("scope", "channel").Eq("target_id", "3").Eq("emoji", "heart").Delete();
        p.Db().From("message_reactions").Select("target_id").Eq("scope", "channel").Eq("emoji", "heart").Get();
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("message_reactions").Select("target_id").Eq("scope", "channel").Eq("emoji", "heart").Eq("reactor", "u1", asIs).Get();
        p.Db().From("server_members").Eq("server_id", "4").Eq("member", "u1", asIs).Delete();
        p.Db().From("channels").Eq("id", "9").Update("{\"name\":\"renamed \\\"c\\\"\"}");
        p.Db().From("channels").Eq("id", "9").Delete();
        p.Db().From("servers").Eq("id", "4").Update("{\"name\":\"S\"}");
        p.Db().From("servers").Eq("id", "4").Delete();
    }});
    return v;
}

#include "test_okay_backend_golden_community2.inc"   // GOLDEN: the texts recorded from the code before the move

int main() {
    RUN_SUITE("okay_backend_golden_community2");
#if defined(_WIN32)
    _putenv_s("OKAY_BACKEND", "");
#else
    unsetenv("OKAY_BACKEND");   // the developer's own backend setting must not decide these results
#endif
    // 1. The headers' own functions send exactly the golden requests and make the same of the answers; 2. the Supabase
    // provider called directly sends the same bytes.
    const auto flows = ProviderFlows();
    if (golden::RunGolden<ProviderRun>("tests/test_okay_backend_golden_community2.cpp", "okay_backend_golden_community2", Scenarios(),
                                       flows, kGolden, [] {
                                           // every clock time in a request was near the time it should be
                                           CHECK(g_badSteady == 0);
                                       }))
        return 0;
    CHECK(flows.size() >= 13);
    TEST_MAIN_RESULT();
}
