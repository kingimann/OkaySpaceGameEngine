// Chat delivery, chat limits, game invites and joining a friend: the pure logic
// the friends window and the launcher's chat call (no network here).
//   engine/include/okay/Platform/ChatDelivery.hpp  client ids, retry rules, the offline queue file,
//                                                  unread counts, history paging merge, safe links, limits
//   engine/include/okay/Platform/Invites.hpp       the invite record, what an invite card says, server answers,
//                                                  "Join game" from a friend's presence
//   engine/include/okay/Net/JoinLink.hpp           version, kind, players and invite id in the link
//   shared/JoinPlan.hpp                            which program to start for a join (player, editor or a pick),
//                                                  and the editor's --join (steps, version check, results in words)
//   engine/include/okay/Platform/Privacy.hpp       the new server rule names
//   engine/include/okay/Platform/Messages.hpp      the send request with a client id
#include "test_framework.hpp"
#include "okay/Platform/ChatDelivery.hpp"
#include "okay/Platform/Invites.hpp"
#include "okay/Platform/Messages.hpp"
#include "okay/Net/JoinLink.hpp"
#include "JoinPlan.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace okay;

static void TestClientIds() {
    RUN_SUITE("client ids");
    std::set<std::string> seen;
    bool allValid = true;
    for (int i = 0; i < 2000; ++i) {
        const std::string id = chat::MakeClientId();
        if (!chat::IsClientId(id)) allValid = false;
        seen.insert(id);
    }
    CHECK(allValid);
    CHECK(seen.size() == 2000);   // no repeats
    const std::string id = chat::MakeClientId();
    CHECK(id.size() == 36 && id[8] == '-' && id[13] == '-' && id[14] == '4' && id[18] == '-' && id[23] == '-');
    CHECK(id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b');   // RFC 4122 variant
    CHECK(!chat::IsClientId(""));
    CHECK(!chat::IsClientId("11111111-2222-3333-4444-55555555555"));      // one short
    CHECK(!chat::IsClientId("1111111g-2222-3333-4444-555555555555"));     // not hex
    CHECK(!chat::IsClientId("11111111-2222-3333-4444-555555555555\"x"));  // can't break out of JSON
    CHECK(chat::IsClientId("11111111-2222-3333-4444-555555555555"));
}

static void TestRetryRules() {
    RUN_SUITE("retry rules");
    account::ApiResponse r;
    r.reached = false; CHECK(chat::IsRetryable(r));                    // no connection: try again later
    r.reached = true; r.status = 503; CHECK(chat::IsRetryable(r));
    r.status = 500; CHECK(chat::IsRetryable(r));
    r.status = 429; CHECK(chat::IsRetryable(r));
    r.status = 408; CHECK(chat::IsRetryable(r));
    r.status = 403; r.body = "{\"message\":\"okay_rule:dm_nobody\"}"; CHECK(!chat::IsRetryable(r));   // a rule said no
    r.status = 403; r.body = "{\"message\":\"okay_rule:rate_limited\"}"; CHECK(chat::IsRetryable(r)); // too fast: later is fine
    r.status = 400; r.body = "bad"; CHECK(!chat::IsRetryable(r));
    r.status = 401; CHECK(chat::IsRetryable(r));                       // token refresh, then again
    r.status = 409; CHECK(!chat::IsRetryable(r));
    r.ok = true; r.status = 201; CHECK(!chat::IsRetryable(r));          // nothing to retry
    CHECK(chat::RetryDelaySeconds(0) == 2);
    CHECK(chat::RetryDelaySeconds(1) == 4);
    CHECK(chat::RetryDelaySeconds(3) == 16);
    CHECK(chat::RetryDelaySeconds(10) == 60);
    CHECK(chat::RetryDelaySeconds(1000) == 60);                        // no overflow
    CHECK(chat::RetryDelaySeconds(-3) == 2);
}

static void TestOfflineQueue() {
    RUN_SUITE("offline queue file");
    std::vector<chat::QueuedMessage> q(3);
    q[0].clientId = "11111111-2222-4333-8444-555555555555"; q[0].toUid = "uid-1"; q[0].body = "hi\tthere\nsecond line \\ end";
    q[0].created = 1700000000; q[0].attempts = 2; q[0].replyTo = 42;
    q[1].clientId = "21111111-2222-4333-8444-555555555555"; q[1].toGroup = 7; q[1].body = "group msg";
    q[2].clientId = "31111111-2222-4333-8444-555555555555"; q[2].toChannel = 9; q[2].body = "";   // empty body is dropped
    const std::string text = chat::EncodeQueue(q);
    const std::vector<chat::QueuedMessage> back = chat::DecodeQueue(text);
    CHECK(back.size() == 2);
    if (back.size() == 2) {
        CHECK(back[0].clientId == q[0].clientId && back[0].toUid == "uid-1" && back[0].body == q[0].body);
        CHECK(back[0].created == 1700000000 && back[0].attempts == 2 && back[0].replyTo == 42);
        CHECK(back[1].toGroup == 7 && back[1].toUid.empty() && back[1].body == "group msg");
    }
    // A damaged file never crashes and keeps what it can read.
    const std::string damaged = "okaychatq1\nnot a line\n" + text.substr(text.find('\n') + 1) + "x\ty\n";
    CHECK(chat::DecodeQueue(damaged).size() == 2);
    CHECK(chat::DecodeQueue("").empty());
    CHECK(chat::DecodeQueue("something else\n").empty());               // not our file
    // A line with a bad client id is dropped (it would go into JSON).
    std::vector<chat::QueuedMessage> bad(1); bad[0].clientId = "x\"}"; bad[0].toUid = "u"; bad[0].body = "b";
    CHECK(chat::DecodeQueue(chat::EncodeQueue(bad)).empty());
    // Old entries are given up on after a week, so a forgotten queue does not post stale text.
    CHECK(chat::ShouldGiveUp(q[0], 1700000000 + 8 * 86400));
    CHECK(!chat::ShouldGiveUp(q[0], 1700000000 + 3600));
}

struct Row { long id; std::string body; };
static void TestMergeAndPaging() {
    RUN_SUITE("history merge and paging");
    std::vector<Row> log = {{10, "a"}, {11, "b"}, {12, "c"}};
    // An older page and a resent copy of 11 arrive: one of each id, oldest first.
    chat::MergeById(log, std::vector<Row>{{8, "x"}, {9, "y"}, {11, "b2"}});
    CHECK(log.size() == 5);
    CHECK(log.front().id == 8 && log.back().id == 12);
    bool sorted = true; for (std::size_t i = 1; i < log.size(); ++i) if (log[i - 1].id >= log[i].id) sorted = false;
    CHECK(sorted);
    CHECK(log[3].id == 11 && log[3].body == "b2");                     // the newer copy (an edit) wins
    // Paging: the cursor is the oldest id we hold.
    CHECK(chat::OldestId(log) == 8);
    CHECK(chat::OldestId(std::vector<Row>{}) == 0);
    CHECK(Messages::PageQuery("uid-9", 0, 50).find("order=id.desc&limit=51") != std::string::npos);    // one extra says "more"
    CHECK(Messages::PageQuery("uid-9", 120, 50).find("id=lt.120") != std::string::npos);
    CHECK(Messages::PageQuery("uid-9", 0, 5000).find("limit=201") != std::string::npos);               // capped
    CHECK(Messages::PageQuery("a&b", 0, 10).find("a&b") == std::string::npos);                          // escaped
}

static void TestUnread() {
    RUN_SUITE("unread counts");
    const std::string me = "me";
    std::vector<chat::UnreadRow> rows = {
        {20, "ann", me}, {19, "ann", me}, {18, me, "ann"}, {17, "ann", me},
        {16, "bob", me}, {15, "bob", me},
        {14, "cy", me},
    };
    std::map<std::string, long> seen = {{"ann", 18}, {"bob", 0}};
    std::set<std::string> blocked = {"cy"};
    const chat::UnreadSummary u = chat::CountUnread(rows, me, seen, blocked);
    CHECK(u.perPeer.at("ann") == 2);                                  // 19, 20 (17 is before what I saw)
    CHECK(u.perPeer.at("bob") == 2);
    CHECK(u.perPeer.count("cy") == 0);                                // blocked people raise nothing
    CHECK(u.total == 4);
    CHECK(u.conversations == 2);
    CHECK(chat::BadgeText(0).empty() && chat::BadgeText(7) == "7" && chat::BadgeText(150) == "99+");
}

static void TestLinks() {
    RUN_SUITE("safe links");
    CHECK(chat::ClassifyLink("https://example.com/a?b=1") == chat::LinkKind::Web);
    CHECK(chat::ClassifyLink("http://example.com") == chat::LinkKind::Web);
    CHECK(chat::ClassifyLink("HTTPS://EXAMPLE.COM") == chat::LinkKind::Web);
    CHECK(chat::ClassifyLink("okay://join?game=A&addr=1.2.3.4:5") == chat::LinkKind::Join);
    CHECK(chat::ClassifyLink("javascript:alert(1)") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("  JavaScript:alert(1)") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("java\tscript:alert(1)") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("data:text/html,<b>") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("file:///C:/Windows/system32/calc.exe") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("vbscript:x") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("ms-settings:") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("\\\\server\\share\\x.exe") == chat::LinkKind::Unsafe);
    CHECK(chat::ClassifyLink("https://") == chat::LinkKind::Unsafe);                // no host
    CHECK(chat::ClassifyLink("https://exa mple.com") == chat::LinkKind::Unsafe);    // a space
    CHECK(chat::ClassifyLink("https://a.com/\"&calc") == chat::LinkKind::Unsafe);   // would break the shell quotes
    CHECK(chat::LinkHost("https://User:pw@Example.COM:8443/path") == "example.com");
    CHECK(chat::LinkHost("https://example.com") == "example.com");
    CHECK(chat::LinkHost("https://xn--pple-43d.com/login").find("xn--") != std::string::npos);
    CHECK(chat::LooksDeceptive("https://xn--pple-43d.com/login"));                 // punycode lookalike
    CHECK(chat::LooksDeceptive("https://paypal.com@evil.example/"));               // user part hides the host
    CHECK(chat::LinkHost("https://paypal.com@evil.example/") == "evil.example");
    CHECK(!chat::LooksDeceptive("https://example.com/a"));
    const std::string t = chat::ConfirmText("https://paypal.com@evil.example/");
    CHECK(t.find("evil.example") != std::string::npos && t.find("Careful") != std::string::npos);
    CHECK(chat::ConfirmText("https://example.com").find("example.com") != std::string::npos);
    CHECK(chat::ConfirmText("javascript:x").find("can't be opened") != std::string::npos);
}

static void TestLimits() {
    RUN_SUITE("limits");
    CHECK(chat::CheckOutgoing("hello", {}, 1000).empty());
    CHECK(!chat::CheckOutgoing("", {}, 1000).empty());
    CHECK(!chat::CheckOutgoing("   \n ", {}, 1000).empty());
    CHECK(chat::CheckOutgoing(std::string(4000, 'x'), {}, 1000).empty());
    const std::string tooLong = chat::CheckOutgoing(std::string(4001, 'x'), {}, 1000);
    CHECK(tooLong.find("4000") != std::string::npos);
    // 4000 characters, not bytes: 4000 two-byte letters fit.
    std::string accents; for (int i = 0; i < 4000; ++i) accents += "\xC3\xA9";
    CHECK(chat::CheckOutgoing(accents, {}, 1000).empty());
    CHECK(okay::utf8::Length(accents) == 4000);
    std::vector<long> recent(30, 990);
    CHECK(chat::CheckOutgoing("x", recent, 1000).find("too fast") != std::string::npos);
    CHECK(chat::CheckOutgoing("x", recent, 1051).empty());            // a minute later
    std::vector<long> fewer(29, 990);
    CHECK(chat::CheckOutgoing("x", fewer, 1000).empty());
    CHECK(chat::kMaxBodyChars == 4000 && chat::kMaxPerMinute == 30);   // the same numbers as docs/sql/chat_invites.sql
}

static void TestPrivacyRules() {
    RUN_SUITE("server rule names");
    CHECK(privacy::ParseRule("{\"message\":\"okay_rule:invite_friends\"}") == privacy::Rule::InviteFriendsOnly);
    CHECK(privacy::ParseRule("okay_rule:rate_limited") == privacy::Rule::RateLimited);
    CHECK(privacy::ParseRule("okay_rule:too_long") == privacy::Rule::TooLong);
    CHECK(std::string(privacy::RuleText(privacy::Rule::InviteFriendsOnly)).find("friends") != std::string::npos);
    CHECK(std::string(privacy::RuleText(privacy::Rule::RateLimited)).find("too fast") != std::string::npos);
    CHECK(privacy::IsRuleText(privacy::RuleText(privacy::Rule::TooLong)));
}

static void TestJoinLinkFields() {
    RUN_SUITE("join link: version, kind, players, invite id");
    JoinLink l; l.game = "Star Arena"; l.relay = "relay.example:45100"; l.code = "K7Q-29X";
    l.version = "1.4.2"; l.target = "dedicated"; l.players = 3; l.maxPlayers = 8; l.inviteId = 77; l.engine = "2.850.1";
    JoinLink b;
    CHECK(JoinLink::Parse(l.Format(), b));
    CHECK(b.version == "1.4.2" && b.target == "dedicated" && b.players == 3 && b.maxPlayers == 8 && b.inviteId == 77 && b.engine == "2.850.1");
    CHECK(b.SameSession(l));
    CHECK(b.Full() == false);
    b.players = 8; CHECK(b.Full());
    // An old link has none of them and still parses.
    JoinLink old;
    CHECK(JoinLink::Parse("okay://join?game=Arena&addr=203.0.113.7:45000", old));
    CHECK(old.version.empty() && old.target.empty() && old.players == 0 && old.maxPlayers == 0 && old.inviteId == 0 && !old.Full());
    // Junk numbers are ignored, not trusted.
    JoinLink j;
    CHECK(JoinLink::Parse("okay://join?game=A&addr=1.2.3.4:5&p=-4&max=99999999999&inv=abc", j));
    CHECK(j.players == 0 && j.maxPlayers == 0 && j.inviteId == 0);
    // A target that is not one of ours is dropped.
    CHECK(JoinLink::Parse("okay://join?game=A&addr=1.2.3.4:5&kind=evil", j) && j.target.empty());
}

static void TestInviteView() {
    RUN_SUITE("invite card");
    const long now = 1700000000;
    GameInvite inv;
    inv.id = 5; inv.kind = "game"; inv.title = "Star Arena"; inv.status = "pending";
    inv.senderName = "maya"; inv.created = now - 60; inv.expires = now + 80 * 60;
    JoinLink l; l.game = "Star Arena"; l.relay = "r:1"; l.code = "K7Q"; inv.link = l.Format();

    InviteView v = ViewInvite(inv, now, PresenceSeen::Unknown, "");
    CHECK(v.state == InviteState::Pending && v.canAccept);
    CHECK(v.text.find("maya invited you to play Star Arena") != std::string::npos);
    CHECK(v.sub.find("1 h 20 min") != std::string::npos);

    // The friend's presence shows the same match: still pending.
    JoinLink same = l; same.host = "maya"; same.players = 2;
    v = ViewInvite(inv, now, PresenceSeen::Known, same.Format());
    CHECK(v.state == InviteState::Pending && v.canAccept);
    // The friend left that match: says so plainly.
    JoinLink other = l; other.code = "ZZZ";
    v = ViewInvite(inv, now, PresenceSeen::Known, other.Format());
    CHECK(v.state == InviteState::Ended && !v.canAccept && v.text.find("no longer in that match") != std::string::npos);
    v = ViewInvite(inv, now, PresenceSeen::Known, "");
    CHECK(v.state == InviteState::Ended);
    // Expired.
    v = ViewInvite(inv, now + 81 * 60, PresenceSeen::Unknown, "");
    CHECK(v.state == InviteState::Expired && !v.canAccept && v.text.find("expired") != std::string::npos);
    // Answered.
    inv.status = "accepted"; v = ViewInvite(inv, now, PresenceSeen::Unknown, "");
    CHECK(v.state == InviteState::Accepted && !v.canAccept);
    inv.status = "declined"; v = ViewInvite(inv, now, PresenceSeen::Unknown, "");
    CHECK(v.state == InviteState::Declined && v.text.find("declined") != std::string::npos);
    inv.status = "cancelled"; v = ViewInvite(inv, now, PresenceSeen::Unknown, "");
    CHECK(v.state == InviteState::Cancelled && v.text.find("withdrawn") != std::string::npos);
    // Party invites do not depend on a match.
    inv.status = "pending"; inv.kind = "party";
    v = ViewInvite(inv, now, PresenceSeen::Known, "");
    CHECK(v.state == InviteState::Pending && v.text.find("party") != std::string::npos);
    // A full match says so but still lets you try.
    inv.kind = "game"; JoinLink full = l; full.players = 8; full.maxPlayers = 8;
    v = ViewInvite(inv, now, PresenceSeen::Known, full.Format());
    CHECK(v.state == InviteState::Pending && v.sub.find("full") != std::string::npos);
    // A legacy chat invite (no record): an old card is called old.
    v = ViewLegacyInvite("maya", "Star Arena", now - 7 * 3600, now);
    CHECK(v.state == InviteState::Expired);
    v = ViewLegacyInvite("maya", "Star Arena", now - 60, now);
    CHECK(v.state == InviteState::Pending && v.canAccept);
    // Never an email as a name.
    inv.senderName = "maya@example.com";
    v = ViewInvite(inv, now, PresenceSeen::Unknown, "");
    CHECK(v.text.find("@example.com") == std::string::npos);
}

static void TestInviteEvents() {
    RUN_SUITE("notification events for new invites");
    const long now = 1700000000;
    std::vector<GameInvite> v(3);   // newest first, as Invites::Incoming returns them
    v[0].id = 9; v[0].senderId = "u1"; v[0].senderName = "maya"; v[0].title = "Star Arena"; v[0].expires = now + 600;
    v[1].id = 8; v[1].senderId = "u2"; v[1].senderName = "bo@example.com"; v[1].kind = "party"; v[1].expires = now + 600;
    v[2].id = 7; v[2].senderId = "u3"; v[2].senderName = "cy"; v[2].expires = now - 1;           // expired: no event
    std::vector<chat::Event> ev = InviteEventsSince(v, 0, now);
    CHECK(ev.size() == 2);
    if (ev.size() == 2) {
        CHECK(ev[0].inviteId == 8 && ev[1].inviteId == 9);                                     // oldest first
        CHECK(ev[0].kind == chat::Event::Kind::Invite && ev[0].fromName == "bo" && ev[0].peerUid == "u2");
        CHECK(ev[1].text.find("Star Arena") != std::string::npos);
    }
    CHECK(InviteEventsSince(v, 9, now).empty());                                                  // already seen
    v[0].status = "accepted"; CHECK(InviteEventsSince(v, 8, now).empty());
}

static void TestInviteAnswers() {
    RUN_SUITE("server answers in words");
    CHECK(InviteAnswerOk("accepted") && InviteAnswerOk("declined") && InviteAnswerOk("cancelled"));
    CHECK(!InviteAnswerOk("expired") && !InviteAnswerOk("gone") && !InviteAnswerOk(""));
    CHECK(InviteAnswerText("expired").find("expired") != std::string::npos);
    CHECK(InviteAnswerText("gone").find("no longer") != std::string::npos);
    CHECK(InviteAnswerText("already_accepted").find("already accepted") != std::string::npos);
    CHECK(InviteAnswerText("invite_friends").find("friends") != std::string::npos);
    CHECK(InviteAnswerText("dm_nobody") == privacy::RuleText(privacy::Rule::DmRefused));
    CHECK(!InviteAnswerText("\"something new\"").empty());
    CHECK(Invites::RpcAnswer("\"accepted\"") == "accepted");
    CHECK(Invites::RpcAnswer("accepted") == "accepted");
    CHECK(Invites::RpcAnswer("") == "");
    // The record from a PostgREST row.
    const std::string row = "{\"id\":12,\"sender\":\"u1\",\"recipient\":\"u2\",\"kind\":\"game\",\"title\":\"Star Arena\","
                            "\"link\":\"okay://join?game=Star%20Arena&addr=1.2.3.4:5\",\"game_version\":\"1.4.2\",\"target\":\"exported\","
                            "\"status\":\"pending\",\"created_at\":\"2023-11-14T22:13:20+00:00\",\"expires_at\":\"2023-11-15T00:13:20+00:00\"}";
    const GameInvite g = Invites::FromRow(row);
    CHECK(g.id == 12 && g.senderId == "u1" && g.recipientId == "u2" && g.kind == "game" && g.gameVersion == "1.4.2" && g.target == "exported");
    CHECK(g.created == 1700000000 && g.expires == 1700007200);
    CHECK(Invites::SendBody("u2", g).find("\"recipient\":\"u2\"") != std::string::npos);
    CHECK(Invites::SendBody("u2", g).find("\"status\"") == std::string::npos);   // the server sets the status
    // Kinds the server accepts.
    CHECK(Invites::ValidKind("game") && Invites::ValidKind("party") && Invites::ValidKind("server") && Invites::ValidKind("room"));
    CHECK(!Invites::ValidKind("") && !Invites::ValidKind("x"));
}

static void TestJoinFromPresence() {
    RUN_SUITE("join from a friend's presence");
    Friend f; f.id = "u1"; f.username = "maya"; f.status = "accepted"; f.online = true; f.playing = "Star Arena";
    JoinLink l; l.game = "Star Arena"; l.addr = "1.2.3.4:5"; l.players = 3; l.maxPlayers = 8;
    f.sessionCode = l.Format();
    JoinCheck c = CheckJoinFriend(f, false);
    CHECK(c.ok && c.link.addr == "1.2.3.4:5" && c.text.empty());
    CHECK(CheckJoinFriend(f, true).text.find("blocked") != std::string::npos);
    Friend p = f; p.status = "pending";
    CHECK(!CheckJoinFriend(p, false).ok && CheckJoinFriend(p, false).text.find("friends") != std::string::npos);
    Friend off = f; off.online = false;
    CHECK(!CheckJoinFriend(off, false).ok && CheckJoinFriend(off, false).text.find("offline") != std::string::npos);
    Friend none = f; none.sessionCode.clear();
    CHECK(!CheckJoinFriend(none, false).ok && CheckJoinFriend(none, false).text.find("isn't in a game you can join") != std::string::npos);
    Friend bare = f; bare.sessionCode = "K7Q-29X";                      // an old launcher's bare relay code
    CHECK(!CheckJoinFriend(bare, false).ok && CheckJoinFriend(bare, false).bareCode == "K7Q-29X");
    JoinLink fl = l; fl.players = 8; Friend full = f; full.sessionCode = fl.Format();
    c = CheckJoinFriend(full, false);
    CHECK(!c.ok && c.text.find("full (8/8)") != std::string::npos);
    CHECK(Joinable(f) && !Joinable(off) && !Joinable(none) && !Joinable(full));
}

static void TestJoinPlan() {
    RUN_SUITE("which program to start");
    using namespace joinplan;
    std::vector<Candidate> lib;
    Candidate g; g.kind = Candidate::Kind::Exported; g.title = "Star Arena"; g.path = "/games/star/scene.okay"; g.gameVersion = "1.4.2";
    lib.push_back(g);
    Candidate pr; pr.kind = Candidate::Kind::Project; pr.title = "Star Arena"; pr.path = "/proj/star"; pr.engineVersion = "2.850.0";
    lib.push_back(pr);
    const std::vector<std::string> editors = {"2.849.0", "2.850.1"};

    JoinLink l; l.game = "Star Arena"; l.addr = "1.2.3.4:5"; l.version = "1.4.2"; l.host = "maya";
    Plan p = PlanJoin(l, lib, editors);
    CHECK(p.action == Action::RunPlayer && p.index == 0);
    // Case and spacing in the title do not matter.
    l.game = "  star arena "; CHECK(PlanJoin(l, lib, editors).action == Action::RunPlayer); l.game = "Star Arena";
    // A dedicated server is joined with the player too.
    l.target = "dedicated"; CHECK(PlanJoin(l, lib, editors).action == Action::RunPlayer);
    // Version mismatch, with both versions shown.
    l.target = ""; l.version = "1.5.0";
    p = PlanJoin(l, lib, editors);
    CHECK(p.action == Action::VersionMismatch && p.text.find("1.5.0") != std::string::npos && p.text.find("1.4.2") != std::string::npos);
    CHECK(p.text.find("maya") != std::string::npos);
    // Unknown versions on either side do not block.
    l.version = ""; CHECK(PlanJoin(l, lib, editors).action == Action::RunPlayer);
    // A friend playing their project in the editor: open the project in the right editor version.
    l.target = "project"; l.engine = "2.850.0";
    p = PlanJoin(l, lib, editors);
    CHECK(p.action == Action::OpenEditor && p.index == 1 && p.editorVersion == "2.850.1");
    // ...no editor of that line installed.
    p = PlanJoin(l, lib, {"2.849.0"});
    CHECK(p.action == Action::NeedEditor && p.text.find("2.850") != std::string::npos);
    // ...only the exported game here: play that instead.
    std::vector<Candidate> onlyGame = {g};
    CHECK(PlanJoin(l, onlyGame, editors).action == Action::RunPlayer);
    // Not in the library: ask which game (or say it is missing when the library is empty).
    l.target = ""; l.game = "Other Game";
    p = PlanJoin(l, lib, editors);
    CHECK(p.action == Action::AskWhichGame && p.text.find("Other Game") != std::string::npos);
    CHECK(PlanJoin(l, {}, editors).action == Action::Missing);
    // Not a join link at all.
    JoinLink bad; CHECK(PlanJoin(bad, lib, editors).action == Action::Invalid);
}

static void TestSendRequest() {
    RUN_SUITE("send request with a client id");
    const std::string cid = "11111111-2222-4333-8444-555555555555";
    CHECK(Messages::SendPath("messages", cid) == "/rest/v1/messages?on_conflict=sender,client_id");
    CHECK(Messages::SendPath("messages", "") == "/rest/v1/messages");
    CHECK(Messages::SendPath("group_messages", "bad id") == "/rest/v1/group_messages");   // never puts junk in the URL
    CHECK(Messages::ClientIdField(cid) == ",\"client_id\":\"" + cid + "\"");
    CHECK(Messages::ClientIdField("x\"}").empty());
    CHECK(Messages::ClientIdColumnMissing(400, "{\"code\":\"PGRST204\",\"message\":\"Could not find the 'client_id' column of 'messages'\"}"));
    CHECK(Messages::ClientIdColumnMissing(400, "{\"code\":\"42703\",\"message\":\"column \\\"client_id\\\" does not exist\"}"));
    CHECK(!Messages::ClientIdColumnMissing(400, "{\"code\":\"42703\",\"message\":\"column \\\"other\\\" does not exist\"}"));
    CHECK(!Messages::ClientIdColumnMissing(403, "client_id"));
}

static void TestInviteCards() {
    RUN_SUITE("invite cards in chat");
    const long now = 1700000000;
    GameInvite rec;
    rec.id = 9; rec.kind = "game"; rec.title = "Star Arena"; rec.status = "pending"; rec.senderId = "u-maya"; rec.recipientId = "u-me";
    rec.created = now - 60; rec.expires = now + 30 * 60;
    JoinLink l; l.game = "Star Arena"; l.relay = "r:1"; l.code = "K7Q"; rec.link = l.Format();

    // Received, with a pending record: Accept and Decline, the time left.
    InviteCard c = CardForInvite("Star Arena", false, "maya", "", &rec, rec.created, now, PresenceSeen::Unknown, "");
    CHECK(c.fromRecord && c.accept && c.decline && !c.withdraw && !c.join);
    CHECK(c.view.text.find("maya invited you to play Star Arena") != std::string::npos);
    CHECK(c.view.sub.find("30 min") != std::string::npos);
    // The record already names a sender: kept.
    GameInvite named = rec; named.senderName = "Maya Chen";
    CHECK(CardForInvite("Star Arena", false, "maya", "", &named, 0, now, PresenceSeen::Unknown, "").view.text.find("Maya Chen") == 0);
    // Answered, expired, withdrawn, ended: no buttons.
    for (const char* st : {"accepted", "declined", "cancelled"}) {
        GameInvite r = rec; r.status = st;
        const InviteCard x = CardForInvite("Star Arena", false, "maya", "", &r, 0, now, PresenceSeen::Unknown, "");
        CHECK(!x.accept && !x.decline && !x.withdraw && !x.join);
    }
    CHECK(!CardForInvite("Star Arena", false, "maya", "", &rec, 0, now + 31 * 60, PresenceSeen::Unknown, "").accept);
    CHECK(CardForInvite("Star Arena", false, "maya", "", &rec, 0, now + 31 * 60, PresenceSeen::Unknown, "").view.state == InviteState::Expired);
    CHECK(CardForInvite("Star Arena", false, "maya", "", &rec, 0, now, PresenceSeen::Known, "").view.state == InviteState::Ended);
    // The match is full right now: still answerable, and says so.
    JoinLink full = l; full.players = 8; full.maxPlayers = 8;
    c = CardForInvite("Star Arena", false, "maya", "", &rec, 0, now, PresenceSeen::Known, full.Format());
    CHECK(c.accept && c.view.sub.find("full (8/8)") != std::string::npos);

    // My own sent invite: Withdraw while pending; the answer once there is one.
    c = CardForInvite("Star Arena", true, "", "Maya Chen", &rec, 0, now, PresenceSeen::Unknown, "");
    CHECK(c.withdraw && !c.accept && !c.decline);
    CHECK(c.view.text == "You invited Maya Chen to play Star Arena");
    CHECK(c.view.sub.find("Waiting for an answer") == 0 && c.view.sub.find("30 min") != std::string::npos);
    GameInvite acc = rec; acc.status = "accepted";
    c = CardForInvite("Star Arena", true, "", "Maya Chen", &acc, 0, now, PresenceSeen::Unknown, "");
    CHECK(!c.withdraw && c.view.state == InviteState::Accepted && c.view.sub == "Maya Chen accepted.");
    GameInvite can = rec; can.status = "cancelled";
    CHECK(CardForInvite("Star Arena", true, "", "Maya Chen", &can, 0, now, PresenceSeen::Unknown, "").view.sub == "You withdrew this invite.");
    CHECK(!CardForInvite("Star Arena", true, "", "Maya", &rec, 0, now + 31 * 60, PresenceSeen::Unknown, "").withdraw);
    // An email is never shown as the name.
    CHECK(CardForInvite("Star Arena", true, "", "maya@example.com", &rec, 0, now, PresenceSeen::Unknown, "").view.text.find("@") == std::string::npos);

    // No record (an older launcher, or the SQL not run, or still loading): the plain Join of before.
    c = CardForInvite("Star Arena", false, "maya", "", nullptr, now - 120, now, PresenceSeen::Unknown, "");
    CHECK(!c.fromRecord && c.join && c.joinLabel == "Join game" && !c.accept);
    CHECK(c.view.sub.find("Sent 2 min ago") == 0);
    c = CardForInvite("Star Arena", false, "maya", "", nullptr, now - 7 * 3600, now, PresenceSeen::Unknown, "");
    CHECK(c.join && c.joinLabel == "Try to join" && c.view.state == InviteState::Expired);
    GameInvite none;   // a lookup that found nothing (id 0) counts as no record
    CHECK(CardForInvite("Star Arena", false, "maya", "", &none, now - 60, now, PresenceSeen::Unknown, "").join);
    c = CardForInvite("Star Arena", true, "", "", nullptr, now - 60, now, PresenceSeen::Unknown, "");
    CHECK(!c.join && !c.withdraw && c.view.text == "You sent an invite to play Star Arena");

    // Invite answered: only a change from pending that was seen before.
    std::vector<GameInvite> sent;
    GameInvite a1 = rec; a1.id = 20; a1.status = "accepted"; a1.recipientId = "u-jon";
    GameInvite a2 = rec; a2.id = 21; a2.status = "declined"; a2.recipientId = "u-jon";
    GameInvite a3 = rec; a3.id = 22; a3.status = "accepted"; a3.recipientId = "u-jon";   // first look: no event
    GameInvite a4 = rec; a4.id = 23; a4.status = "pending";
    sent = {a4, a3, a2, a1};   // newest first, as Sent() returns them
    std::map<long, std::string> known = {{20, "pending"}, {21, "pending"}, {23, "pending"}};
    std::map<std::string, std::string> names = {{"u-jon", "Jonas"}};
    auto ev = AnsweredInviteEvents(sent, known, names);
    CHECK(ev.size() == 2);
    if (ev.size() == 2) {
        CHECK(ev[0].inviteId == 20 && ev[0].kind == chat::Event::Kind::InviteAnswered && ev[0].text == "Jonas accepted your invite to play Star Arena.");
        CHECK(ev[1].inviteId == 21 && ev[1].text.find("declined") != std::string::npos && ev[1].peerUid == "u-jon");
    }
    known[20] = "accepted";   // already told
    CHECK(AnsweredInviteEvents({a1}, known, names).empty());
    CHECK(AnsweredInviteEvents(sent, {}, names).empty());
}

static void TestLinkClicks() {
    RUN_SUITE("clicking a link");
    chat::LinkClick c = chat::ClickLink("  https://example.com/page?x=1 ");
    CHECK(c.action == chat::LinkClick::Action::Confirm && c.url == "https://example.com/page?x=1");
    CHECK(c.text.find("example.com") != std::string::npos && c.text.find("Open it?") != std::string::npos);
    c = chat::ClickLink("https://bank.com@evil.example/");
    CHECK(c.action == chat::LinkClick::Action::Confirm && c.text.find("evil.example") != std::string::npos && c.text.find("Careful") != std::string::npos);
    c = chat::ClickLink("okay://join?game=Arena&addr=1.2.3.4:5");
    CHECK(c.action == chat::LinkClick::Action::Join);
    for (const char* bad : {"javascript:alert(1)", "file:///C:/Windows", "data:text/html,hi", "https://a.com/\"&calc", "ftp://x.org/"}) {
        c = chat::ClickLink(bad);
        CHECK(c.action == chat::LinkClick::Action::Refuse && c.text.find("can't be opened safely") != std::string::npos);
    }
}

static void TestUnreadFloor() {
    RUN_SUITE("unread counts with the old mark as a floor");
    std::vector<chat::UnreadRow> rows = {{50, "u-maya", "me"}, {51, "u-maya", "me"}, {60, "u-maya", "me"}, {58, "u-jon", "me"},
                                         {61, "me", "u-jon"}, {62, "u-sam", "me"}};
    std::map<std::string, long> seen = {{"u-maya", 50}, {"u-sam", 70}};
    const auto adj = chat::SeenWithFloor(seen, rows, 55);
    CHECK(adj.at("u-maya") == 55 && adj.at("u-jon") == 55 && adj.at("u-sam") == 70 && adj.at("me") == 55);
    const chat::UnreadSummary u = chat::CountUnread(rows, "me", adj, {});
    CHECK(u.total == 2 && u.perPeer.at("u-maya") == 1 && u.perPeer.at("u-jon") == 1 && !u.perPeer.count("u-sam"));
    CHECK(chat::SeenWithFloor(seen, rows, 0) == seen);   // no floor: unchanged
    const chat::UnreadSummary all = chat::CountUnread(rows, "me", seen, {});
    CHECK(all.total == 3);   // without the floor maya's 51 and jon's old 58 count
}

static void TestGroupEvents() {
    RUN_SUITE("group chat notifications");
    std::vector<Messages::GroupConvo> gs(3);
    gs[0].id = 7; gs[0].title = "Jam"; gs[0].lastId = 90; gs[0].lastSender = "Maya"; gs[0].lastBody = "build is up";
    gs[1].id = 8; gs[1].lastId = 40; gs[1].lastSender = "kingimann";
    gs[2].id = 9; gs[2].lastId = 12; gs[2].lastSender = "Jonas";
    std::map<long, long> seen;
    CHECK(GroupMessageEvents(gs, seen, "kingimann").empty());   // first look: marks only
    CHECK(seen[7] == 90 && seen[8] == 40);
    gs[0].lastId = 91; gs[0].lastBody = "3 hours left"; gs[1].lastId = 41; gs[2].lastId = 13; gs[2].lastRead = 13;
    auto ev = GroupMessageEvents(gs, seen, "kingimann");
    CHECK(ev.size() == 1);   // 8: my own message; 9: already read
    if (!ev.empty()) CHECK(ev[0].kind == chat::Event::Kind::GroupMessage && ev[0].groupId == 7 && ev[0].messageId == 91 &&
                           ev[0].fromName == "Maya" && ev[0].text == "3 hours left");
    CHECK(GroupMessageEvents(gs, seen, "kingimann").empty());   // nothing newer
    gs[0].lastId = 92; gs[0].lastSender = "maya@example.com";
    ev = GroupMessageEvents(gs, seen, "kingimann");
    CHECK(ev.size() == 1 && !ev.empty() && ev[0].fromName.find('@') == std::string::npos);   // never an email
    gs[0].lastId = 93;
    CHECK(GroupMessageEvents(gs, seen, "kingimann", {"g:7"}).empty());   // muted
    CHECK(seen[7] == 93);
}

static void TestResumeQueue() {
    RUN_SUITE("the offline queue after a restart");
    const long now = 1700000000;
    std::vector<chat::QueuedMessage> q(4);
    q[0].clientId = chat::MakeClientId(); q[0].toUid = "u-maya"; q[0].body = "still there?"; q[0].created = now - 600; q[0].attempts = 8;
    q[1] = q[0];                                                                  // the same message twice: once
    q[2].clientId = chat::MakeClientId(); q[2].toGroup = 7; q[2].body = "old"; q[2].created = now - 8 * 86400;   // too old: dropped
    q[3].clientId = chat::MakeClientId(); q[3].toChannel = 3; q[3].body = "gg"; q[3].created = now - 60; q[3].replyTo = 12;
    // Through the file and back, as the launcher saves it.
    const auto back = chat::ResumeQueue(chat::DecodeQueue(chat::EncodeQueue(q)), now);
    CHECK(back.size() == 2);
    if (back.size() == 2) {
        CHECK(back[0].clientId == q[0].clientId && back[0].toUid == "u-maya" && back[0].attempts == 0 && back[0].created == now - 600);
        CHECK(back[1].toChannel == 3 && back[1].replyTo == 12 && back[1].body == "gg");
    }
    CHECK(chat::ResumeQueue({}, now).empty());
}

static void TestKeepScroll() {
    RUN_SUITE("load older keeps the place");
    CHECK(chat::KeepScrollAfterPrepend(0.0f, 500.0f, 900.0f) == 400.0f);     // at the top: the old first message stays in view
    CHECK(chat::KeepScrollAfterPrepend(120.0f, 500.0f, 900.0f) == 520.0f);
    CHECK(chat::KeepScrollAfterPrepend(50.0f, 500.0f, 500.0f) == 50.0f);     // nothing older came
    CHECK(chat::KeepScrollAfterPrepend(10.0f, 500.0f, 300.0f) == 0.0f);      // never negative
    CHECK(chat::KeepScrollAfterPrepend(900.0f, 500.0f, 700.0f) == 700.0f);   // never past the end
}

static void TestJoinPlanButtons() {
    RUN_SUITE("join plan buttons");
    using namespace joinplan;
    Candidate g; g.kind = Candidate::Kind::Exported; g.title = "Star Arena"; g.gameVersion = "1.4.2";
    Candidate pr; pr.kind = Candidate::Kind::Project; pr.title = "Star Arena"; pr.engineVersion = "2.850.0";
    JoinLink l; l.game = "Star Arena"; l.addr = "1.2.3.4:5"; l.version = "1.5.0";
    Plan p = PlanJoin(l, {g}, {});
    CHECK(p.action == Action::VersionMismatch && p.theirVersion == "1.5.0" && p.yourVersion == "1.4.2");
    CHECK(GetVersionLabel(p) == "Get version 1.5.0");
    l.version = ""; l.target = "project"; l.engine = "2.850.0";
    p = PlanJoin(l, {pr}, {"2.849.0"});
    CHECK(p.action == Action::NeedEditor && p.theirVersion == "2.850.0" && GetVersionLabel(p) == "Get version 2.850.0");
    CHECK(GetVersionLabel(PlanJoin(l, {pr}, {"2.850.3"})).empty());   // OpenEditor: nothing to get
}

static void TestGroupUnread() {
    RUN_SUITE("group chat unread counts");
    const std::string me = "u-me";
    std::vector<chat::GroupUnreadRow> rows = {
        {120, 7, "u-maya"}, {119, 7, "u-jon"}, {118, 7, me}, {117, 7, "u-maya"}, {110, 7, "u-maya"},
        {116, 9, "u-sam"}, {115, 9, "u-troll"},
        {114, 4, me},
        {113, 5, "u-maya"},
    };
    // Read up to 117 in group 7 here, 100 on the server; group 9 read on another computer up to 115.
    const std::map<long, long> seen = chat::GroupSeen({{7, 117}, {9, 0}}, {{7, 100}, {9, 115}, {12, 40}});
    CHECK(seen.at(7) == 117 && seen.at(9) == 115 && seen.at(12) == 40);
    const std::set<std::string> blocked = {"u-troll"};
    chat::GroupUnreadSummary u = chat::CountGroupUnread(rows, me, seen, blocked);
    CHECK(u.perGroup.at(7) == 2);                       // 119 and 120 (118 is mine, 117 and older were read)
    CHECK(u.perGroup.at(9) == 1);                       // 116 (115 was read elsewhere)
    CHECK(u.perGroup.count(4) == 0);                    // only my own message
    CHECK(u.perGroup.at(5) == 1);                       // never opened: everything counts
    CHECK(u.total == 4 && u.conversations == 3);
    // A blocked sender never counts.
    std::vector<chat::GroupUnreadRow> onlyTroll = {{130, 3, "u-troll"}};
    CHECK(chat::CountGroupUnread(onlyTroll, me, {}, blocked).total == 0);
    // The newest message of a group is not in the rows (older than the window, or newer than the last read): at least 1.
    // In the rows but mine: nothing.
    const std::map<long, long> newest = {{7, 120}, {4, 114}, {12, 80}, {13, 50}};
    u = chat::CountGroupUnread(rows, me, seen, blocked, newest);
    CHECK(u.perGroup.at(7) == 2);                       // the real number wins
    CHECK(u.perGroup.count(4) == 0);                    // 114 is mine and is in the rows
    CHECK(u.perGroup.at(12) == 1);                      // 80 > 40 read, not in the rows
    CHECK(u.perGroup.at(13) == 1);                      // nothing read, not in the rows
    CHECK(u.total == 6);
    std::map<long, long> readAll = seen; readAll[12] = 80;
    CHECK(chat::CountGroupUnread(rows, me, readAll, blocked, newest).perGroup.count(12) == 0);
    // Muted groups keep their (dim) number but are left out of the total; the open group counts nothing.
    u = chat::CountGroupUnread(rows, me, seen, blocked, newest, {7}, 9);
    CHECK(u.perGroup.at(7) == 2 && u.perGroup.count(9) == 0);
    CHECK(u.total == 3);                                // 5, 12, 13
    // Nothing at all.
    CHECK(chat::CountGroupUnread({}, me, {}, {}).total == 0);
}

static void TestEditorJoin() {
    RUN_SUITE("editor --join");
    using namespace joinplan;
    JoinLink l; l.game = "Star Arena"; l.relay = "relay.example.com:45100"; l.code = "K7Q29X"; l.host = "maya";
    l.target = "project"; l.engine = "2.850.0";
    const std::string link = l.Format();
    // The command line: both spellings; a bad link or none is "".
    CHECK(JoinArgFrom({"--launcher", "42", "--open", "/p/Assets/main.okayscene", "--join", link}) == link);
    CHECK(JoinArgFrom({"--join=" + link}) == link);
    CHECK(JoinArgFrom({"--join", "https://example.com"}).empty());
    CHECK(JoinArgFrom({"--join"}).empty());
    CHECK(JoinArgFrom({"--open", "x"}).empty());
    // The launcher adds it after the open-with arguments; nothing for a bad link.
    const std::vector<std::string> base = {"--launcher", "42", "--open", "/p/Assets/main.okayscene", "--keep-version"};
    std::vector<std::string> withJoin = base; withJoin.push_back("--join"); withJoin.push_back(link);
    CHECK(WithJoinArg(base, link) == withJoin);
    CHECK(WithJoinArg(base, "") == base);
    CHECK(WithJoinArg(base, "okay://join?game=x") == base);
    CHECK(JoinArgFrom(WithJoinArg(base, link)) == link);

    // Version: the same line joins (any patch), another line says both versions; a dev build or an unknown version never blocks.
    CHECK(EditorJoinProblem(l, "2.850.3").empty());
    const std::string mism = EditorJoinProblem(l, "2.849.0");
    CHECK(mism.find("maya") != std::string::npos && mism.find("2.850") != std::string::npos && mism.find("2.849.0") != std::string::npos);
    CHECK(EditorJoinProblem(l, "").empty());
    JoinLink noEng = l; noEng.engine.clear();
    CHECK(EditorJoinProblem(noEng, "2.849.0").empty());
    CHECK(EditorJoinProblem(JoinLink{}, "2.850.0") == "That isn't a join link.");

    // The steps: nothing before the sign-in gate and the project; then Play; then join.
    EditorJoinState s;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Wait);                     // the sign-in gate is closed
    s.projectOpen = true; s.playing = true;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Wait);                     // still closed, even with a project open
    s = EditorJoinState{}; s.projectsAllowed = true; s.projectBusy = true;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Wait);                     // the project is still opening (or its question is up)
    s.projectBusy = false;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Drop);                     // it did not open
    CHECK(EditorJoinDropText(s, "Star Arena").find("Star Arena") != std::string::npos);
    s.projectOpen = true; s.projectIsTheOneAsked = false;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Drop);                     // another project is open (the open was cancelled)
    s.projectIsTheOneAsked = true;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::EnterPlay);
    s.playAsked = true; s.playBusy = true;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Wait);                     // "Save before continuing?" is up
    s.playBusy = false;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Drop);                     // it was cancelled: no second Play
    CHECK(EditorJoinDropText(s, "").find("Play mode did not start") == 0);
    s.playing = true;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Join);
    s.playAsked = false;
    CHECK(NextEditorJoinStep(s) == EditorJoinStep::Join);                     // already playing (a hand-off): join straight away

    // The result in words.
    EditorJoinProgress p;
    CHECK(WatchEditorJoin(p, "maya").state == EditorJoinPhase::Waiting);
    p.joined = true;
    EditorJoinResult o = WatchEditorJoin(p, "maya");
    CHECK(o.state == EditorJoinPhase::Joined && o.text == "Joined maya's game.");
    p = EditorJoinProgress{}; p.rejected = true; p.rejectReason = "server is full";
    o = WatchEditorJoin(p, "maya");
    CHECK(o.state == EditorJoinPhase::Failed && o.text.find("maya's game is full") == 0);
    p.rejectReason = "wrong password";
    CHECK(WatchEditorJoin(p, "maya").text.find("password") != std::string::npos);
    p.rejectReason = "the host did not answer";
    CHECK(WatchEditorJoin(p, "maya").text.find("may have ended") != std::string::npos);
    p.rejectReason = "kicked by the anti-cheat; try again in 30 s";
    CHECK(WatchEditorJoin(p, "maya").text == "Couldn't join maya's game: kicked by the anti-cheat; try again in 30 s.");
    p = EditorJoinProgress{}; p.lastError = "No game is hosted with code K7Q29X (check the code; the host may have stopped).";
    o = WatchEditorJoin(p, "maya");
    CHECK(o.state == EditorJoinPhase::Failed && o.text.find("has ended") != std::string::npos);
    p.lastError = "The online game ended: the relay session closed.";
    CHECK(WatchEditorJoin(p, "maya").text == "maya's game has ended.");
    p.lastError = "Could not reach the relay server r:1 (no answer after 8 s).";
    CHECK(WatchEditorJoin(p, "").text.find("Could not reach the relay") != std::string::npos);
    CHECK(WatchEditorJoin(p, "").text.find("the host's game") != std::string::npos);
    p = EditorJoinProgress{}; p.gateRefusal = "your session ticket was refused";
    CHECK(WatchEditorJoin(p, "maya").text.find("ticket") != std::string::npos);
    p = EditorJoinProgress{}; p.stopped = true;
    CHECK(WatchEditorJoin(p, "maya").state == EditorJoinPhase::Failed);
    p = EditorJoinProgress{}; p.seconds = 29.0f;
    CHECK(WatchEditorJoin(p, "maya").state == EditorJoinPhase::Waiting);
    p.seconds = 30.0f;
    CHECK(WatchEditorJoin(p, "maya").text == "maya's game did not answer. It may have ended.");
    // Joined wins over everything else.
    p.joined = true; p.lastError = "x"; p.rejected = true;
    CHECK(WatchEditorJoin(p, "maya").state == EditorJoinPhase::Joined);
}

int main() {
    TestClientIds();
    TestRetryRules();
    TestOfflineQueue();
    TestMergeAndPaging();
    TestUnread();
    TestLinks();
    TestLimits();
    TestPrivacyRules();
    TestJoinLinkFields();
    TestInviteView();
    TestInviteEvents();
    TestInviteAnswers();
    TestJoinFromPresence();
    TestJoinPlan();
    TestSendRequest();
    TestInviteCards();
    TestLinkClicks();
    TestUnreadFloor();
    TestGroupEvents();
    TestResumeQueue();
    TestKeepScroll();
    TestJoinPlanButtons();
    TestGroupUnread();
    TestEditorJoin();
    TEST_MAIN_RESULT();
}
