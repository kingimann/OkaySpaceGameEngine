// OkayBackend round 5: golden request tests for the community headers, part 1 (docs/platform/okay_backend.md, sections 6,
// 10.1 and 18): Profiles, Friends, BlockList, Privacy (ProfilesGet), Invites and Parties.
//
// Every table read and write and every database function call these six headers send is run against a fake server through
// their own public functions. The exact requests (method, URL with its query in order, header lines in order, body) and what
// the program made of each answer must equal the GOLDEN texts in test_okay_backend_golden_community1.inc, which were recorded
// from the headers BEFORE they moved onto okay::backend::Database (run with OKAY_GOLDEN_PRINT=1 to print them again). Then the
// same requests are sent through the Supabase provider directly (okay::backend::supabase::Database over a RecordingTransport)
// and must be the same bytes.
//
// Three parts of some bodies change from run to run: Friends' presence time ("last_seen": now), an invite's expiry (now + the
// invite's lifetime) and Friends' new friend code (made from the clock). The test writes them as <NOW>, <NOW+3600> and <CODE>
// and checks them separately (the times within 10 seconds of what they should be, the code six letters from its alphabet).
//
// No request ever leaves the process: every call goes to the test transport. The address, key, names and tokens are made up.
#include "test_framework.hpp"
#include "okay_backend_golden.hpp"   // the fake server, made-up tokens, RequestsOf
#include "okay/Backend/Backend.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/BlockList.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Invites.hpp"
#include "okay/Platform/Parties.hpp"
#include "okay/Platform/Privacy.hpp"
#include "okay/Platform/Profiles.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
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
static long g_badSteady = 0;   // clock times or friend codes that were not what they should be (checked at the end)

/// The parts that change from run to run as fixed marks: a time within 10 s of now is <NOW>, of now + 3600 s <NOW+3600>
/// (other times are the test's own and stay); a new friend code is <CODE>.
static std::string Steady(const std::string& log) {
    static const std::regex iso("\"([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z)\"");
    static const std::regex code("\"friend_code\":\"([^\"]*)\"");
    std::string out;
    std::string rest = log;
    std::smatch m;
    while (std::regex_search(rest, m, iso)) {
        out += m.prefix().str();
        const long t = okay::Friends::ParseIso(m[1].str());
        const long now = (long)std::time(nullptr);
        if (t >= now - 10 && t <= now + 10) out += "\"<NOW>\"";
        else if (t >= now + 3600 - 10 && t <= now + 3600 + 10) out += "\"<NOW+3600>\"";
        else out += m[0].str();
        rest = m.suffix().str();
    }
    out += rest;
    std::string res;
    rest = out;
    while (std::regex_search(rest, m, code)) {
        res += m.prefix().str();
        const std::string c = m[1].str();
        bool good = c.size() == 6;
        for (char ch : c) if (std::string("ABCDEFGHJKLMNPQRSTUVWXYZ23456789").find(ch) == std::string::npos) good = false;
        if (!good) ++g_badSteady;
        res += "\"friend_code\":\"<CODE>\"";
        rest = m.suffix().str();
    }
    return res + rest;
}

static std::unique_ptr<acc::AccountService> Make(const fs::path& dir, Fake& f) {
    auto a = std::make_unique<acc::AccountService>(dir, kUrl, kKey);
    a->SetTransportForTest([&f](const std::string& m, const std::string& u, const std::vector<std::string>& h, const std::string& b) {
        return f.Answer(m, u, h, b);
    });
    return a;
}
/// A fresh account signed in as `user` (user id "u1": the token's subject), and the profiles view state back to unknown
/// (it is one value for the whole process, so each scenario starts from the same place).
static std::unique_ptr<acc::AccountService> SignedIn(const fs::path& dir, Fake& f, const std::string& user = "alex") {
    SaveSession(dir, user, Tok(1), "r1");
    okay::privacy::ProfilesViewState() = 0;
    return Make(dir, f);
}
static std::string B(bool v) { return v ? "1" : "0"; }
static std::string L(long v) { return std::to_string(v); }
template <class T> static std::string Join(const std::vector<T>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) { s += i ? "|" : ""; if constexpr (std::is_same<T, std::string>::value) s += v[i]; else s += std::to_string(v[i]); }
    return s + "]";
}
static std::string Pairs(const std::vector<std::pair<std::string, std::string>>& v) {
    std::string s = "{";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "|" : "") + v[i].first + "=" + v[i].second;
    return s + "}";
}
static std::string Bundle(const okay::ProfileBundle& b) {
    const okay::PublicProfile& p = b.p;
    std::string s = "  found=" + B(b.found) + " reached=" + B(b.reached) + " failed=" + B(b.failed) + " showcase=" + B(b.showcaseSupported) +
                    " ach=" + B(b.achSupported) + " extras=" + B(b.extrasSupported) + " follow=" + B(b.followSupported) + "\n";
    s += "  id=[" + p.id + "] user=[" + p.username + "] display=[" + p.display + "] avatar=[" + p.avatar + "] bio=[" + p.bio + "] accent=" +
         std::to_string(p.accent) + " status=[" + p.customStatus + "] fav=[" + p.favoriteGame + "] loc=[" + p.location + "] playing=[" +
         p.playing + "] code=[" + p.sessionCode + "] seen=" + L(p.lastSeen) + " since=" + L(p.memberSince) + " online=" + B(p.online) + "\n";
    s += "  showAch=" + Join(p.showcaseAch) + " showGames=" + Join(p.showcaseGames) + " banner=" + std::to_string(p.bannerAccent) +
         " verified=" + B(p.verified) + " socials=" + Pairs(p.socials) + " badges=" + Join(b.badges) + "\n";
    for (const auto& e : b.playtime) s += "  play " + e.key + " [" + e.title + "] " + L(e.seconds) + " " + L(e.lastPlayed) + "\n";
    for (const auto& a : b.achievements) s += "  ach " + a.id + " [" + a.label + "] " + L(a.when) + " pts=" + std::to_string(a.points) + "\n";
    for (const auto& w : b.games) s += "  game " + L(w.id) + " [" + w.title + "] by [" + w.author + "] " + w.kind + " " + w.url + " dl=" + L(w.downloads) + "\n";
    s += "  friends=" + std::to_string(b.friends) + " followers=" + std::to_string(b.followers) + " following=" + std::to_string(b.following) +
         " iFollow=" + B(b.iFollow) + " followsMe=" + B(b.followsMe) + " isMe=" + B(b.isMe) + " row=" + L(b.friendRow) + " status=[" +
         b.friendStatus + "] incoming=" + B(b.incoming) + "\n";
    for (const auto& e : b.activity) s += "  act [" + e.actor + "] " + e.kind + " [" + e.detail + "] " + L(e.when) + "\n";
    return s;
}
static std::string FriendText(const okay::Friend& f) {
    return "  friend id=[" + f.id + "] user=[" + f.username + "] status=[" + f.status + "] in=" + B(f.incoming) + " online=" + B(f.online) +
           " seen=" + L(f.lastSeen) + " playing=[" + f.playing + "] code=[" + f.sessionCode + "] display=[" + f.display + "] avatar=[" +
           f.avatar + "] bio=[" + f.bio + "] accent=" + std::to_string(f.accent) + " status2=[" + f.customStatus + "] fav=[" +
           f.favoriteGame + "] loc=[" + f.location + "] row=" + L(f.rowId) + "\n";
}
static std::string OutcomeText(const okay::BlockList::Outcome& o) {
    return "=> ok=" + B(o.ok) + " missing=" + B(o.missing) + " offline=" + B(o.offline) + " msg=[" + o.message + "]\n";
}
static std::string InviteText(const okay::GameInvite& g) {
    return "  invite " + L(g.id) + " from=[" + g.senderId + "] to=[" + g.recipientId + "] " + g.kind + " [" + g.title + "] link=[" + g.link +
           "] ver=[" + g.gameVersion + "] target=[" + g.target + "] " + g.status + " " + L(g.created) + " " + L(g.expires) + "\n";
}
static std::string PartyText(const okay::Party& p) {
    std::string s = "  party id=[" + p.id + "] leader=[" + p.leader + "] game=[" + p.game + "] link=[" + p.joinLink + "] max=" +
                    std::to_string(p.maxSize) + "\n";
    for (const auto& m : p.members)
        s += "  member [" + m.uid + "] user=[" + m.username + "] display=[" + m.display + "] avatar=[" + m.avatar + "] ready=" + B(m.ready) +
             " online=" + B(m.online) + " playing=[" + m.playing + "] joined=" + L(m.joinedAt) + "\n";
    return s;
}
static const char* kFar = "2099-01-01T00:00:00Z";   // a last-seen time in the future: online, whatever today's date is

// ---- The scenarios, run through the headers' public functions ----------------------------------------------------------------
using golden::Scenario;

static std::vector<Scenario> Scenarios() {
    std::vector<Scenario> v;

    // ---- Profiles --------------------------------------------------------------------------------------------------------
    v.push_back({"profiles_load_by_name_full", [](const fs::path& d) {
        const std::string row = std::string("[{\"id\":\"u-2\",\"username\":\"bo\",\"display_name\":\"Bo \\\"B\\\"\",\"avatar_url\":\"https://a.example.test/b.png\","
                                "\"bio\":\"hi\\nthere\",\"accent\":4,\"status\":\"busy\",\"favorite_game\":\"Arena\",\"location\":\"Mars\",\"playing\":\"Arena\","
                                "\"session_code\":\"R-1\",\"last_seen\":\"") + kFar + "\",\"created_at\":\"2023-11-14T22:13:20Z\","
                                "\"showcase_achievements\":[\"a1\",\"a2\"],\"showcase_games\":[7,0,9],\"banner_accent\":2,"
                                "\"social_links\":{\"x\":\"@bo\",\"web\":\"https://bo.example.test\",\"n\":5},\"verified\":true}]";
        Fake f; f.queue = {Http(200, "[{\"id\":\"u-2\"}]"), Http(200, row),
                           Http(200, "[{\"ach_id\":\"a1\",\"label\":\"First\",\"unlocked_at\":\"2023-11-14T22:13:20Z\",\"points\":250},{\"ach_id\":\"a2\",\"label\":\"\"}]"),
                           Http(200, "[{\"id\":7,\"author_name\":\"bo\",\"title\":\"Game7\",\"description\":\"d\",\"kind\":\"game\",\"storage_path\":\"https://w.example.test/7\",\"downloads\":12},{\"id\":0,\"title\":\"skip\"}]"),
                           Http(200, "[{\"badge\":\"staff\"},{\"badge\":\"beta\"}]"),
                           Http(200, "[{\"game_key\":\"arena\",\"title\":\"Arena\",\"seconds\":3600,\"last_played\":\"2023-11-14T22:13:20Z\"},{\"game_key\":\"x\",\"seconds\":0}]"),
                           Http(200, "3\n"), Http(200, "[{\"followers\":5,\"following\":2}]"), Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, "[{\"follower\":\"u1\",\"followee\":\"u-2\"},{\"follower\":\"u-2\",\"followee\":\"u1\"}]"),
                           Http(200, "[{\"id\":41,\"status\":\"pending\",\"requester\":\"u-2\",\"addressee\":\"u1\"}]"),
                           Http(200, "[{\"actor_name\":\"old@mail.example.test\",\"kind\":\"online\",\"detail\":\"\",\"created_at\":\"2023-11-14T22:13:20Z\"}]")};
        auto a = SignedIn(d, f);
        okay::Profiles p(*a);
        std::string out = Bundle(p.Load("b o&x=1", true));
        return out + f.log;
    }});
    v.push_back({"profiles_load_fallbacks", [](const fs::path& d) {
        Fake f; f.queue = {Http(404, "{\"code\":\"PGRST205\"}"), Http(400, "{\"message\":\"column social_links does not exist\"}"),
                           Http(400, "{}"),
                           Http(200, "[{\"id\":\"u-3\",\"username\":\"cy\",\"display_name\":null,\"accent\":\"x\",\"last_seen\":\"2000-01-01T00:00:00Z\"}]"),
                           Http(400, "{}"), Http(404, "{}"), Http(500, "{}"), Http(404, "{}"), Http(404, "{}"),
                           Http(200, "[{\"id\":9,\"status\":\"accepted\",\"requester\":\"u1\",\"addressee\":\"u-3\"}]"), Http(200, "[]")};
        auto a = SignedIn(d, f);
        okay::Profiles p(*a);
        std::string out = Bundle(p.Load("u-3/../x", false));
        return out + f.log;
    }});
    v.push_back({"profiles_load_me_and_misses", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\",\"username\":\"alex\"}]"), Http(200, "[]"), Http(200, "[]"), Http(200, "[]"), Http(200, "[]"),
                           Http(200, "not a number"), Http(200, "[]"), Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, "[{\"id\":1,\"status\":\"accepted\"},{\"id\":2,\"status\":\"pending\"},{\"id\":3,\"status\":\"accepted\"}]"),
                           Http(200, "[]"),
                           // by name: not found, then a server error, then no answer
                           Http(200, "[]"), Http(500, "{}"), NoAnswer("x"),
                           // by id: the row is missing
                           Http(200, "[]")};
        auto a = SignedIn(d, f);
        okay::Profiles p(*a);
        std::string out = Bundle(p.Load("u1", false));
        out += Bundle(p.Load("nobody", true));
        out += Bundle(p.Load("err", true));
        out += Bundle(p.Load("gone", true));
        out += Bundle(p.Load("u-404", false));
        out += Bundle(p.Load("", false));
        return out + f.log;
    }});
    v.push_back({"profiles_writes", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"), Http(204, ""),                           // SaveShowcase (my id, then the patch)
                           Http(400, "{\"message\":\"column points does not exist\"}"), Http(201, ""), // SyncAchievements, then without points
                           Http(201, ""),                                                            // SyncAchievements once more
                           Http(200, "[{\"social_links\":{\"x\":\"@me\",\"gh\":\"me\"}}]"), Http(400, "{}"),   // MySocialLinks twice
                           Http(204, ""), Http(201, ""),                                             // SaveSocialLinks, SyncPlaytime
                           Http(201, ""), Http(409, "{}"), Http(204, ""), Http(204, ""),              // Follow x2, Unfollow, RemoveFollower
                           Http(204, ""),                                                            // SetFollowersSeeActivity
                           Http(201, ""), Http(204, ""), Http(204, "")};                             // AddFriend, Accept, Remove
        auto a = SignedIn(d, f);
        okay::Profiles p(*a);
        std::string out;
        out += "=> " + B(p.SaveShowcase({"a1", "a\"2", "a3", "a4", "a5", "a6", "a7"}, {7, 8, 9, 10, 11}, 3)) + "\n";
        okay::Achievement a1; a1.id = "first"; a1.label = "First \"one\""; a1.when = 1700000000; a1.points = 25;
        okay::Achievement a2; a2.id = "b\\2"; a2.label = "Two\nlines"; a2.when = 0; a2.points = 10;
        out += "=> " + B(p.SyncAchievements({a1, a2})) + "\n";
        out += "=> " + B(p.SyncAchievements({a2})) + "\n";
        out += "=> " + B(p.SyncAchievements({})) + "\n";
        bool sup = false;
        const auto links = p.MySocialLinks(&sup);
        out += "=> " + Pairs(links) + " supported=" + B(sup) + "\n";
        const auto none = p.MySocialLinks(&sup);
        out += "=> " + Pairs(none) + " supported=" + B(sup) + "\n";
        out += "=> " + B(p.SaveSocialLinks({{"x", "@me"}, {"empty", ""}, {"web", std::string(250, 'w')}})) + "\n";
        okay::PlaytimeEntry e1; e1.key = "arena"; e1.title = "Arena \"1\""; e1.seconds = 3600; e1.lastPlayed = 1700000000;
        okay::PlaytimeEntry e2; e2.key = "editor"; e2.title = "Editor"; e2.seconds = -5; e2.lastPlayed = 0;
        out += "=> " + B(p.SyncPlaytime({e1, e2})) + "\n";
        out += "=> " + B(p.Follow("u-2")) + "\n";
        out += "=> " + B(p.Follow("u \"3\"")) + "\n";
        out += "=> " + B(p.Unfollow("u-2&x")) + "\n";
        out += "=> " + B(p.RemoveFollower("u-4")) + "\n";
        out += "=> " + B(p.SetFollowersSeeActivity(true)) + "\n";
        out += "=> " + B(p.AddFriend("u-5")) + "\n";
        out += "=> " + B(p.AddFriend("")) + "\n";
        out += "=> " + B(p.Accept(12)) + "\n";
        out += "=> " + B(p.Accept(0)) + "\n";
        out += "=> " + B(p.Remove(13)) + "\n";
        out += "=> " + B(p.Remove(-1)) + "\n";
        return out + f.log;
    }});
    v.push_back({"profiles_follow_lists", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, "[{\"follower\":\"u-2\",\"followee\":\"u1\"},{\"follower\":\"u-3\",\"followee\":\"u1\"},{\"follower\":\"\"}]"),
                           Http(200, "[{\"id\":\"u-3\",\"username\":\"cy\",\"display_name\":\"\",\"avatar_url\":\"a\",\"last_seen\":\"2023-11-14T22:13:20Z\"},"
                                     "{\"id\":\"u-2\",\"username\":\"bo\",\"display_name\":\"Bo\",\"last_seen\":null}]"),
                           Http(200, "[]"),                                         // following of u-9: nobody
                           Http(404, "{}"),                                         // follows.sql not run
                           Http(200, "[{\"id\":\"u-7\"}]"), Http(201, ""),           // FollowByName: found, followed
                           Http(200, "[{\"id\":\"u1\"}]"),                           // FollowByName: that's me
                           Http(200, "[]"),                                         // FollowByName: nobody
                           Http(200, "[{\"id\":\"u-8\"}]"), Http(409, "{}")};        // FollowByName: refused
        auto a = SignedIn(d, f, "al ex+\xc3\xa9");
        okay::Profiles p(*a);
        bool sup = false;
        std::string out;
        for (const auto& e : p.FollowList("", true, &sup))
            out += "  follow id=[" + e.id + "] user=[" + e.username + "] display=[" + e.display + "] avatar=[" + e.avatar + "] seen=" + L(e.lastSeen) + "\n";
        out += "supported=" + B(sup) + "\n";
        const std::size_t n1 = p.FollowList("u-9", false, &sup).size();
        out += "=> " + std::to_string(n1) + " supported=" + B(sup) + "\n";
        const std::size_t n2 = p.FollowList("u-9", true, &sup).size();
        out += "=> " + std::to_string(n2) + " supported=" + B(sup) + "\n";
        for (const char* name : {"dee", "al ex+\xc3\xa9", "nobody", "eve"}) {
            std::string err;
            const bool ok = p.FollowByName(name, &err);
            out += "=> " + B(ok) + " err=[" + err + "]\n";
        }
        return out + f.log;
    }});
    v.push_back({"profiles_signed_out", [](const fs::path& d) {
        Fake f;
        okay::privacy::ProfilesViewState() = 0;
        auto a = Make(d, f);
        okay::Profiles p(*a);
        // (one call per statement: the operands of a chain of + on strings may be evaluated in any order)
        std::string out = B(p.Available());
        out += B(p.Load("x", false).found);
        out += B(p.SaveShowcase({}, {}, -1));
        out += B(p.SyncAchievements({okay::Achievement{}}));
        out += B(p.SaveSocialLinks({}));
        out += B(p.SyncPlaytime({okay::PlaytimeEntry{}}));
        out += B(p.AddFriend("u"));
        out += B(p.Follow("u"));
        out += B(p.Unfollow("u"));
        out += B(p.SetFollowersSeeActivity(false));
        out += std::to_string(p.FollowList("", true).size());
        return "=> " + out + "\n" + f.log;
    }});

    // ---- Friends ---------------------------------------------------------------------------------------------------------
    v.push_back({"friends_profile", [](const fs::path& d) {
        const std::string me = std::string("[{\"username\":\"alex\",\"friend_code\":\"\",\"playing\":\"Arena\",\"session_code\":\"R\",\"display_name\":\"Alex\","
                               "\"avatar_url\":\"https://a.example.test/a.png\",\"bio\":\"b\",\"accent\":3,\"status\":\"s\",\"favorite_game\":\"g\","
                               "\"location\":\"l\",\"last_seen\":\"") + kFar + "\"}]";
        Fake f; f.queue = {Http(201, ""),                                     // EnsureProfile: upsert
                           Http(200, "[{\"id\":\"u1\"}]"), Http(200, me),      // MyCode: my id, my row (no code yet)
                           Http(204, ""),                                     // the new code
                           Http(404, "{}"), Http(200, "[{\"username\":\"alex\",\"friend_code\":\"AB12CD\"}]"),   // MyCode: view gone, table
                           Http(200, "[{\"display_name\":\"Alex A\"}]"), Http(204, ""), Http(200, "[{\"avatar_url\":\"u\"}]"), Http(204, ""),
                           Http(200, "[{\"bio\":\"b\\\"q\"}]"), Http(204, ""), Http(200, me),
                           Http(200, "[{\"accent\":7}]"), Http(204, ""), Http(200, "[{\"status\":\"st\"}]"), Http(204, ""),
                           Http(200, "[{\"favorite_game\":\"fg\"}]"), Http(204, ""), Http(200, "[{\"location\":\"lo\"}]"), Http(204, "")};
        auto a = SignedIn(d, f);
        okay::Friends fr(*a);
        std::string out;
        fr.EnsureProfile();
        out += "=> code=[" + fr.MyCode() + "]\n";
        out += "=> [" + fr.MyDisplayName() + "]\n";
        out += "=> " + B(fr.SetDisplayName("Alex \"A\"")) + "\n";
        out += "=> [" + fr.MyAvatar() + "]\n";
        out += "=> " + B(fr.SetAvatar("https://a.example.test/a b.png")) + "\n";
        out += "=> [" + fr.MyBio() + "]\n";
        out += "=> " + B(fr.SetBio("line1\nline2\t\x01")) + "\n";
        out += FriendText(fr.MyProfile());
        out += "=> " + std::to_string(fr.MyAccent()) + "\n";
        out += "=> " + B(fr.SetAccent(-2)) + "\n";
        out += "=> [" + fr.MyStatus() + "]\n";
        out += "=> " + B(fr.SetStatus("away")) + "\n";
        out += "=> [" + fr.MyFavoriteGame() + "]\n";
        out += "=> " + B(fr.SetFavoriteGame("Arena")) + "\n";
        out += "=> [" + fr.MyLocation() + "]\n";
        out += "=> " + B(fr.SetLocation("Moon")) + "\n";
        return out + Steady(f.log);
    }});
    v.push_back({"friends_requests_list", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u-2\"}]"), Http(201, ""),                                        // Request(bo): found, sent
                           Http(200, "[]"),                                                                     // Request(nobody)
                           Http(200, "[{\"id\":\"u-3\"}]"), Http(400, "{\"message\":\"blocked_by_user\"}"),     // refused by a rule
                           Http(200, "[{\"id\":\"u-4\"}]"), Http(400, ""),                                      // refused, no words
                           Http(200, "[{\"id\":\"u-5\"}]"), Http(201, ""),                                      // by code
                           Http(204, ""), Http(204, ""),                                                        // Accept, Remove
                           Http(200, "[{\"id\":5,\"requester\":\"u1\",\"addressee\":\"u-2\",\"status\":\"accepted\"},"
                                     "{\"id\":6,\"requester\":\"u-3\",\"addressee\":\"u1\",\"status\":\"pending\"}]"),
                           Http(200, "[{\"username\":\"alex\"}]"), Http(200, "[{\"username\":\"bo\",\"last_seen\":\"2023-11-14T22:13:20Z\"}]"),
                           Http(200, "[{\"username\":\"cy\",\"display_name\":\"Cy\"}]"), Http(200, "[{\"username\":\"alex\"}]"),
                           Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, "[{\"addressee\":\"u1\",\"status\":\"pending\"},{\"addressee\":\"u-9\",\"status\":\"pending\"}]")};
        auto a = SignedIn(d, f);
        okay::Friends fr(*a);
        std::string out;
        for (const char* n : {"bo", "nobody", "cy", "dee"}) {
            std::string err;
            const bool ok = fr.Request(n, &err);
            out += "=> " + B(ok) + " err=[" + err + "]\n";
        }
        std::string err;
        const bool byCode = fr.RequestByCode("  ab1 2cd ", &err);
        out += "=> " + B(byCode) + " err=[" + err + "]\n";
        out += "=> " + B(fr.Accept(5)) + "\n";
        out += "=> " + B(fr.Remove(-3)) + "\n";
        for (const auto& x : fr.List()) out += FriendText(x);
        out += "=> pending " + std::to_string(fr.PendingRequests()) + "\n";
        return out + f.log;
    }});
    v.push_back({"friends_presence_feed", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"), Http(204, ""),     // SetPresence (my id, patch)
                           Http(204, ""), Http(204, ""), Http(204, ""),       // GoOffline, MarkPresence, Heartbeat
                           Http(201, ""),                                     // Post
                           Http(200, "[{\"actor\":\"u-2\",\"actor_name\":\"bo@mail.example.test\",\"kind\":\"playing\",\"detail\":\"Arena\",\"created_at\":\"2023-11-14T22:13:20Z\"},"
                                     "{\"actor\":\"u-3\",\"actor_name\":\"cy\",\"kind\":\"online\",\"detail\":\"\",\"created_at\":null},"
                                     "{\"actor\":\"u-2\",\"actor_name\":\"bo\",\"kind\":\"online\",\"detail\":\"\"},{\"actor\":\"\",\"actor_name\":\"x@y.example.test\"}]"),
                           Http(200, "[{\"id\":\"u-2\",\"username\":\"bo\"},{\"id\":\"u-3\",\"username\":\"\"}]"),
                           Http(500, "{}")};                                  // Feed: the server fails
        auto a = SignedIn(d, f);
        okay::Friends fr(*a);
        std::string out;
        fr.SetPresence("Playing \"Arena\"", "R-9");
        fr.GoOffline();
        fr.MarkPresence("In the launcher");
        fr.Heartbeat();
        fr.Post("achievement", "First \"win\"");
        for (const auto& e : fr.Feed(5)) out += "  ev [" + e.actor + "] id=[" + e.actorId + "] " + e.kind + " [" + e.detail + "] " + L(e.when) + "\n";
        out += "=> " + std::to_string(fr.Feed().size()) + "\n";
        return out + Steady(f.log);
    }});
    v.push_back({"friends_signed_out", [](const fs::path& d) {
        Fake f;
        okay::privacy::ProfilesViewState() = 0;
        auto a = Make(d, f);
        okay::Friends fr(*a);
        fr.EnsureProfile(); fr.SetPresence("x"); fr.GoOffline(); fr.Post("k", "d");
        std::string err;
        std::string out = B(fr.SetDisplayName("x"));
        out += B(fr.SetAccent(1));
        out += std::to_string(fr.List().size());
        out += std::to_string(fr.PendingRequests());
        out += std::to_string(fr.Feed().size());
        out += B(fr.Request("bo", &err));
        out += "[" + err + "]";
        return "=> " + out + "\n" + f.log;
    }});

    // ---- Privacy (ProfilesGet) and BlockList -----------------------------------------------------------------------------
    v.push_back({"privacy_profiles_get", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"a\"}]"), Http(500, "{}"), Http(404, "{}"), Http(200, "[]"), Http(200, "[{\"id\":\"c\"}]"),
                           NoAnswer("x")};
        auto a = SignedIn(d, f);
        std::string out;
        for (const char* q : {"select=id&id=eq.a", "select=id&id=eq.b", "select=id&id=eq.c", "select=id&id=eq.d", "select=id&id=eq.e"}) {
            const auto r = okay::privacy::ProfilesGet(*a, q);
            out += "=> ok=" + B(r.ok) + " reached=" + B(r.reached) + " status=" + L(r.status) + " body=[" + r.body + "] state=" +
                   std::to_string(okay::privacy::ProfilesViewState().load()) + "\n";
        }
        return out + f.log;
    }});
    v.push_back({"blocklist", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, "[{\"blocked\":\"u-2\",\"blocked_name\":\"bo\"},{\"blocked\":\"u-3\",\"blocked_name\":\"\"},{\"blocked\":\"\"}]"),
                           Http(201, ""), Http(400, "{\"message\":\"rate_limited\"}"), Http(204, ""), Http(401, "{}"),
                           Http(200, "[{\"dm_from\":\"friends\",\"profile_visibility\":\"nobody\",\"online_visibility\":\"everyone\",\"friend_requests\":\"nobody\",\"calls_from\":\"friends\"}]"),
                           Http(200, "[]"), Http(201, ""), Http(404, "{}"), Http(404, "{\"code\":\"PGRST205\"}"), NoAnswer("x"), Http(403, "{}")};
        auto a = SignedIn(d, f, "al ex+\xc3\xa9");
        okay::BlockList bl(*a);
        std::string out;
        std::set<std::string> mine; std::map<std::string, std::string> names;
        out += OutcomeText(bl.Fetch(mine, names));
        for (const auto& id : mine) out += "  blocked [" + id + "] name=[" + (names.count(id) ? names[id] : std::string("-")) + "]\n";
        out += OutcomeText(bl.Block("u-4", "Dee \"D\""));
        out += OutcomeText(bl.Block("u-5", ""));
        out += OutcomeText(bl.Unblock("u-4"));
        out += OutcomeText(bl.Unblock("u 5&x"));
        okay::privacy::Settings s; bool found = false;
        out += OutcomeText(bl.FetchSettings(s, found));
        out += "  found=" + B(found) + " " + okay::privacy::SettingsJson(s) + "\n";
        out += OutcomeText(bl.FetchSettings(s, found));
        out += "  found=" + B(found) + "\n";
        s.dmFrom = 2; s.callsFrom = 0;
        out += OutcomeText(bl.SaveSettings(s));
        out += OutcomeText(bl.SaveSettings(s));
        out += OutcomeText(bl.Block("u-6", "x"));
        out += OutcomeText(bl.Unblock("u-6"));
        out += OutcomeText(bl.Fetch(mine, names));
        out += "  support blocks=" + std::to_string((int)bl.BlocksSupport()) + " settings=" + std::to_string((int)bl.SettingsSupport()) + "\n";
        return out + f.log;
    }});
    v.push_back({"blocklist_no_id_signed_out", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[]")};
        auto a = SignedIn(d, f);
        okay::BlockList bl(*a);
        std::set<std::string> mine; std::map<std::string, std::string> names;
        std::string out = OutcomeText(bl.Fetch(mine, names));
        auto b = Make(d / "out", f);
        okay::BlockList bo(*b);
        okay::privacy::Settings s; bool found = true;
        out += OutcomeText(bo.Fetch(mine, names));
        out += OutcomeText(bo.Block("u", "n"));
        out += OutcomeText(bo.Unblock("u"));
        out += OutcomeText(bo.FetchSettings(s, found));
        out += OutcomeText(bo.SaveSettings(s));
        out += "  found=" + B(found) + "\n";
        return out + f.log;
    }});

    // ---- Invites ---------------------------------------------------------------------------------------------------------
    v.push_back({"invites", [](const fs::path& d) {
        const std::string row = "[{\"id\":31,\"sender\":\"u1\",\"recipient\":\"u-2\",\"kind\":\"game\",\"title\":\"Arena\",\"link\":\"okay://join/R-1\","
                                "\"game_version\":\"1.2\",\"target\":\"exported\",\"status\":\"pending\",\"created_at\":\"2023-11-14T22:13:20Z\","
                                "\"expires_at\":\"2023-11-14T23:13:20Z\"}]";
        Fake f; f.queue = {Http(201, row), Http(404, "{\"code\":\"PGRST205\"}"), Http(400, "{\"message\":\"blocked_by_user\"}"),
                           Http(200, row), Http(200, "[]"), Http(200, row), Http(200, "[]"),
                           Http(200, "\"accepted\"\n"), Http(400, "{\"message\":\"rate_limited\"}"), Http(404, "{\"code\":\"PGRST202\"}"),
                           Http(200, "\"cancelled\"")};
        auto a = SignedIn(d, f);
        okay::Invites in(*a);
        std::string out;
        okay::GameInvite g; g.kind = "game"; g.title = "Arena \"1\""; g.link = "okay://join/R-1"; g.gameVersion = "1.2"; g.target = "exported";
        g.created = 1000; g.expires = 4600;
        auto send = [&](const std::string& to) {
            std::string err;
            const long id = in.Send(to, g, &err);
            out += "=> " + L(id) + " err=[" + err + "] support=" + std::to_string((int)in.SupportState()) + "\n";
        };
        send("u-2");
        g.expires = 0;
        send("u-2");
        send("u-3");
        g.kind = "bad";
        send("u-3");
        for (const auto& x : in.Incoming(5, 10)) out += InviteText(x);
        out += "=> " + std::to_string(in.Incoming().size()) + "\n";
        for (const auto& x : in.Sent(-2)) out += InviteText(x);
        out += InviteText(in.Get(31));
        out += "=> [" + in.Respond(31, true) + "]\n";
        out += "=> [" + in.Respond(32, false) + "]\n";
        const std::string cancelled = in.Cancel(33);
        out += "=> [" + cancelled + "] support=" + std::to_string((int)in.SupportState()) + "\n";
        out += "=> [" + in.Cancel(-1) + "]\n";
        return out + Steady(f.log);
    }});

    // ---- Parties ---------------------------------------------------------------------------------------------------------
    v.push_back({"parties_mine", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"u1\"}]"),
                           Http(200, "[{\"party_id\":\"p-1\"}]"),
                           Http(200, "[{\"id\":\"p-1\",\"leader\":\"u1\",\"game\":\"Arena\",\"join_link\":\"okay://join/R\",\"max_size\":0}]"),
                           Http(200, "[{\"member\":\"u1\",\"ready\":true,\"joined_at\":\"2023-11-14T22:13:20Z\"},{\"member\":\"u-2\",\"ready\":false},{\"member\":\"\"}]"),
                           Http(200, std::string("[{\"id\":\"u-2\",\"username\":\"bo\",\"display_name\":\"Bo\",\"avatar_url\":\"a\",\"last_seen\":\"") + kFar +
                                     "\",\"playing\":\"Arena\"},{\"id\":\"u1\",\"username\":\"alex@mail.example.test\"}]"),
                           // not in a party; then the first request fails
                           Http(200, "[]"), Http(500, "{}")};
        auto a = SignedIn(d, f, "al ex+\xc3\xa9");
        okay::Parties ps(*a);
        okay::Party p;
        const bool mine = ps.Mine(p);
        std::string out = "=> " + B(mine) + "\n" + PartyText(p);
        const bool none = ps.Mine(p);
        out += "=> " + B(none) + " valid=" + B(p.Valid()) + "\n";   // (none is read first: p is not changed by this line)
        const bool failed = ps.Mine(p);
        out += "=> " + B(failed) + "\n";
        return out + f.log;
    }});
    v.push_back({"parties_actions", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "\"p-1\""), Http(404, "{}"), Http(400, "{\"message\":\"Party is full\"}"), NoAnswer("x"), Http(500, "{}"),
                           Http(204, ""), Http(204, ""),
                           Http(200, "[{\"id\":\"u1\"}]"), Http(204, ""), Http(204, ""), Http(204, "")};
        auto a = SignedIn(d, f);
        okay::Parties ps(*a);
        std::string out;
        auto step = [&](bool ok, const std::string& err) { out += "=> " + B(ok) + " err=[" + err + "]\n"; };
        { std::string e; const bool ok = ps.Create(&e); step(ok, e); }
        { std::string e; const bool ok = ps.Join("p-2", &e); step(ok, e); }
        { std::string e; const bool ok = ps.Join("p \"3\"", &e); step(ok, e); }
        { std::string e; const bool ok = ps.Join("", &e); step(ok, e); }
        { std::string e; const bool ok = ps.Leave(&e); step(ok, e); }
        { std::string e; const bool ok = ps.Kick("u-2", &e); step(ok, e); }
        { std::string e; const bool ok = ps.Promote("u-3\n", &e); step(ok, e); }
        { std::string e; const bool ok = ps.Promote("u-4", &e); step(ok, e); }
        out += "=> " + B(ps.SetReady("p-1", true)) + "\n";
        out += "=> " + B(ps.SetReady("p 1&x", false)) + "\n";
        out += "=> " + B(ps.SetReady("", false)) + "\n";
        out += "=> " + B(ps.SetSession("p-1", "Arena \"1\"", "okay://join/R\n")) + "\n";
        out += "=> " + B(ps.SetSession("", "g", "l")) + "\n";
        return out + f.log;
    }});
    // Ids holding PostgREST's reserved characters (a comma, brackets, a quote) inside or=(...) and in.(...): the moved sites
    // must send them exactly as before (percent-encoded but not quoted in Profiles and Friends, unchanged in Parties). Normal
    // ids are user ids and never hold them; this pins the bytes anyway (docs/platform/okay_backend.md section 18).
    v.push_back({"reserved_characters_in_lists", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[{\"id\":\"a,b(c)\\\"d\",\"username\":\"q\"}]"),                  // Load(id): the profile row
                           Http(404, "{}"), Http(404, "{}"), Http(404, "{}"), Http(404, "{}"), Http(404, "{}"), Http(404, "{}"),
                           Http(200, "[]"), Http(200, "[{\"id\":\"u1\"}]"),                          // follow counts, MyId
                           Http(200, "[]"), Http(200, "[]"), Http(200, "[]"),                            // follows, friendships, activity
                           Http(200, "[{\"follower\":\"a,b\"},{\"follower\":\"c(d)\"},{\"follower\":\" e\\\"\"}]"), Http(200, "[]"),
                           Http(200, "[{\"actor\":\"x,y\",\"actor_name\":\"x\"},{\"actor\":\"(z)\",\"actor_name\":\"z\"}]"), Http(200, "[]"),
                           Http(200, "[{\"id\":\"u1\"}]"), Http(200, "[{\"party_id\":\"p,1\"}]"), Http(200, "[{\"id\":\"p,1\"}]"),
                           Http(200, "[{\"member\":\"m,1\"},{\"member\":\"m(2)\"}]"), Http(200, "[]")};
        auto a = SignedIn(d, f);
        std::string out;
        okay::Profiles pr(*a);
        out += Bundle(pr.Load("a,b(c)\"d", false));
        out += "=> " + std::to_string(pr.FollowList("", true).size()) + "\n";
        okay::Friends fr(*a);
        out += "=> " + std::to_string(fr.Feed(2).size()) + "\n";
        okay::Parties ps(*a);
        okay::Party p;
        const bool mine = ps.Mine(p);
        out += "=> " + B(mine) + "\n" + PartyText(p);
        return out + f.log;
    }});
    return v;
}

// ---- The same requests through the Supabase provider directly (okay::backend::supabase::Database, RecordingTransport) ------
// Each flow sends, in order, the requests its scenario of the same name sent (the user-id lookups included), built only with
// the Query builder: a builder or provider change that alters one byte fails here even when a header still matches.
struct ProviderRun : golden::Recorder {
    okay::backend::supabase::Database Db() { return okay::backend::supabase::Database(kUrl, kKey, rec, Tok(1)); }
};
using ProviderFlow = std::function<void(ProviderRun&)>;
static std::vector<std::pair<const char*, ProviderFlow>> ProviderFlows() {
    namespace ob = okay::backend;
    using Q = ob::Query;
    const auto unquoted = ob::ValueText::EncodedUnquoted;
    const auto asIs = ob::ValueText::AsIs;
    static const std::string kBase = "id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at";
    static const std::string kFriendRow = "username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen";
    std::vector<std::pair<const char*, ProviderFlow>> v;
    v.push_back({"profiles_load_by_name_full", [=](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "b o&x=1").Get();
        p.Db().From("profiles_visible").Select(kBase + ",showcase_achievements,showcase_games,banner_accent,social_links,verified").Eq("id", "u-2").Get();
        p.Db().From("user_achievements").Select("ach_id,label,unlocked_at,points").Eq("owner", "u-2").Order("unlocked_at", true).Get();
        p.Db().From("workshop_items").Select("id,author_name,title,description,kind,storage_path,downloads").Eq("author", "u-2").Order("downloads", true).Get();
        p.Db().From("profile_badges").Select("badge").Eq("owner", "u-2").Order("awarded_at").Get();
        p.Db().From("user_playtime").Select("game_key,title,seconds,last_played").Eq("owner", "u-2").Order("seconds", true).Limit(30).Get();
        p.Db().Rpc("profile_friend_count", "{\"p_uid\":\"u-2\"}");
        p.Db().Rpc("follow_counts", "{\"p_uid\":\"u-2\"}");
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("follows").Select("follower,followee")
            .Or({Q::And({Q::Cond("follower", "eq", "u1", unquoted), Q::Cond("followee", "eq", "u-2", unquoted)}),
                 Q::And({Q::Cond("follower", "eq", "u-2", unquoted), Q::Cond("followee", "eq", "u1", unquoted)})}).Get();
        p.Db().From("friendships").Select("id,status,requester,addressee")
            .Or({Q::Cond("requester", "eq", "u-2", unquoted), Q::Cond("addressee", "eq", "u-2", unquoted)}).Get();
        p.Db().From("activity").Select("actor_name,kind,detail,created_at").Eq("actor", "u-2").Order("created_at", true).Limit(8).Get();
    }});
    v.push_back({"profiles_writes", [](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("profiles").Eq("id", "u1").Update("{\"showcase_achievements\":[\"a1\",\"a\\\"2\",\"a3\",\"a4\",\"a5\",\"a6\"],\"showcase_games\":[7,8,9,10],\"banner_accent\":3}");
        p.Db().From("user_achievements").OnConflict("owner,ach_id").Upsert("[{\"ach_id\":\"first\",\"label\":\"First \\\"one\\\"\",\"unlocked_at\":\"2023-11-14T22:13:20Z\",\"points\":25},{\"ach_id\":\"b\\\\2\",\"label\":\"Two\\nlines\",\"points\":10}]");
        p.Db().From("user_achievements").OnConflict("owner,ach_id").Upsert("[{\"ach_id\":\"first\",\"label\":\"First \\\"one\\\"\",\"unlocked_at\":\"2023-11-14T22:13:20Z\"},{\"ach_id\":\"b\\\\2\",\"label\":\"Two\\nlines\"}]");
        p.Db().From("user_achievements").OnConflict("owner,ach_id").Upsert("[{\"ach_id\":\"b\\\\2\",\"label\":\"Two\\nlines\",\"points\":10}]");
        for (int i = 0; i < 2; ++i) p.Db().From("profiles").Select("social_links").Eq("id", "u1").Get();
        p.Db().From("profiles").Eq("id", "u1").Update("{\"social_links\":{\"x\":\"@me\",\"web\":\"" + std::string(200, 'w') + "\"}}");
        p.Db().From("user_playtime").OnConflict("owner,game_key").Upsert("[{\"game_key\":\"arena\",\"title\":\"Arena \\\"1\\\"\",\"seconds\":3600,\"last_played\":\"2023-11-14T22:13:20Z\"},{\"game_key\":\"editor\",\"title\":\"Editor\",\"seconds\":0}]");
        p.Db().From("follows").Insert("{\"followee\":\"u-2\"}");
        p.Db().From("follows").Insert("{\"followee\":\"u \\\"3\\\"\"}");
        p.Db().From("follows").Eq("follower", "u1").Eq("followee", "u-2&x").Delete();
        p.Db().From("follows").Eq("follower", "u-4").Eq("followee", "u1").Delete();
        p.Db().From("profiles").Eq("id", "u1").Update("{\"followers_see_activity\":true}");
        p.Db().From("friendships").Insert("{\"addressee\":\"u-5\"}");
        p.Db().From("friendships").Eq("id", "12").Update("{\"status\":\"accepted\"}");
        p.Db().From("friendships").Eq("id", "13").Delete();
    }});
    v.push_back({"profiles_follow_lists", [=](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "al ex+\xc3\xa9").Get();
        p.Db().From("follows").Select("follower,followee").Eq("followee", "u1").Order("created_at", true).Limit(200).Get();
        p.Db().From("profiles_visible").Select("id,username,display_name,avatar_url,last_seen").In("id", {"u-2", "u-3"}, unquoted).Get();
        p.Db().From("follows").Select("follower,followee").Eq("follower", "u-9").Order("created_at", true).Limit(200).Get();
        p.Db().From("follows").Select("follower,followee").Eq("followee", "u-9").Order("created_at", true).Limit(200).Get();
        p.Db().From("profiles").Select("id").Eq("username", "dee").Get();
        p.Db().From("follows").Insert("{\"followee\":\"u-7\"}");
        for (const char* n : {"al ex+\xc3\xa9", "nobody", "eve"}) p.Db().From("profiles").Select("id").Eq("username", n).Get();
        p.Db().From("follows").Insert("{\"followee\":\"u-8\"}");
    }});
    v.push_back({"friends_profile", [=](ProviderRun& p) {
        p.Db().From("profiles").OnConflict("id").Upsert("{\"username\":\"alex\"}");
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("profiles_visible").Select(kFriendRow).Eq("id", "u1").Get();
        p.Db().From("profiles").Eq("id", "u1").Update("{\"friend_code\":\"<CODE>\"}");   // (the scenario's code, as the test writes it)
        p.Db().From("profiles_visible").Select(kFriendRow).Eq("id", "u1").Get();
        for (const char* body : {"{\"display_name\":\"Alex \\\"A\\\"\"}", "{\"avatar_url\":\"https://a.example.test/a b.png\"}",
                                 "{\"bio\":\"line1\\nline2\\t\\u0001\"}"}) {
            if (std::string(body).find("display") != std::string::npos) p.Db().From("profiles").Select(kFriendRow).Eq("id", "u1").Get();
            p.Db().From("profiles").Select(kFriendRow).Eq("id", "u1").Get();
            p.Db().From("profiles").Eq("id", "u1").Update(body);
        }
        p.Db().From("profiles").Select(kFriendRow).Eq("id", "u1").Get();
        for (const char* body : {"{\"accent\":-2}", "{\"status\":\"away\"}", "{\"favorite_game\":\"Arena\"}", "{\"location\":\"Moon\"}"}) {
            p.Db().From("profiles").Select(kFriendRow).Eq("id", "u1").Get();
            p.Db().From("profiles").Eq("id", "u1").Update(body);
        }
    }});
    v.push_back({"friends_requests_list", [=](ProviderRun& p) {
        const char* uids[] = {"u-2", "", "u-3", "u-4"};
        const char* names[] = {"bo", "nobody", "cy", "dee"};
        for (int i = 0; i < 4; ++i) {
            p.Db().From("profiles").Select("id").Eq("username", names[i]).Get();
            if (*uids[i]) p.Db().From("friendships").Insert("{\"addressee\":\"" + std::string(uids[i]) + "\"}");
        }
        p.Db().From("profiles").Select("id").Eq("friend_code", "AB1 2CD").Get();
        p.Db().From("friendships").Insert("{\"addressee\":\"u-5\"}");
        p.Db().From("friendships").Eq("id", "5").Update("{\"status\":\"accepted\"}");
        p.Db().From("friendships").Eq("id", "-3").Delete();
        p.Db().From("friendships").Select("id,requester,addressee,status").Get();
        for (const char* id : {"u1", "u-2", "u-3", "u1"}) p.Db().From("profiles_visible").Select(kFriendRow).Eq("id", id).Get();
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("friendships").Select("addressee,status").Eq("status", "pending").Get();
    }});
    v.push_back({"friends_presence_feed", [=](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        for (const char* body : {"{\"last_seen\":\"<NOW>\",\"playing\":\"Playing \\\"Arena\\\"\",\"session_code\":\"R-9\"}",
                                 "{\"last_seen\":\"2000-01-01T00:00:00Z\",\"playing\":null,\"session_code\":null}",
                                 "{\"last_seen\":\"<NOW>\",\"playing\":\"In the launcher\",\"session_code\":\"\"}",
                                 "{\"last_seen\":\"<NOW>\",\"playing\":\"In the launcher\",\"session_code\":\"\"}"})
            p.Db().From("profiles").Eq("id", "u1").Update(body);
        p.Db().From("activity").Insert("{\"kind\":\"achievement\",\"detail\":\"First \\\"win\\\"\",\"actor_name\":\"alex\"}");
        p.Db().From("activity").Select("actor,actor_name,kind,detail,created_at").Neq("actor", "u1").Order("created_at", true).Limit(5).Get();
        p.Db().From("profiles").Select("id,username").In("id", {"u-2", "u-3"}, unquoted).Get();
        p.Db().From("activity").Select("actor,actor_name,kind,detail,created_at").Neq("actor", "u1").Order("created_at", true).Limit(20).Get();
    }});
    v.push_back({"blocklist", [=](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "al ex+\xc3\xa9").Get();
        p.Db().From("blocks").Select("blocked,blocked_name").Eq("blocker", "u1", asIs).Get();
        auto block = [&p](const std::string& body) {
            p.Db().From("blocks").OnConflict("blocker,blocked").Prefer("resolution=ignore-duplicates").Insert(body);
        };
        block("{\"blocked\":\"u-4\",\"blocked_name\":\"Dee \\\"D\\\"\"}");
        block("{\"blocked\":\"u-5\",\"blocked_name\":\"\"}");
        p.Db().From("blocks").Eq("blocked", "u-4", asIs).Delete();
        p.Db().From("blocks").Eq("blocked", "u 5&x", asIs).Delete();
        for (int i = 0; i < 2; ++i) p.Db().From("privacy_settings").Select("dm_from,profile_visibility,online_visibility,friend_requests,calls_from").Get();
        for (int i = 0; i < 2; ++i)
            p.Db().From("privacy_settings").OnConflict("user_id").Upsert("{\"dm_from\":\"nobody\",\"profile_visibility\":\"nobody\",\"online_visibility\":\"everyone\",\"friend_requests\":\"nobody\",\"calls_from\":\"everyone\"}");
        block("{\"blocked\":\"u-6\",\"blocked_name\":\"x\"}");
        p.Db().From("blocks").Eq("blocked", "u-6", asIs).Delete();
        p.Db().From("blocks").Select("blocked,blocked_name").Eq("blocker", "u1", asIs).Get();
    }});
    v.push_back({"invites", [](ProviderRun& p) {
        okay::GameInvite g; g.kind = "game"; g.title = "Arena \"1\""; g.link = "okay://join/R-1"; g.gameVersion = "1.2"; g.target = "exported";
        g.created = 1000; g.expires = 4600;
        auto send = [&p](const std::string& body) { p.Db().From("game_invites").Returning(ob::Return::Representation).Insert(Steady(body)); };
        send(okay::Invites::SendBody("u-2", g));
        g.expires = 0;
        send(okay::Invites::SendBody("u-2", g));
        send(okay::Invites::SendBody("u-3", g));
        auto list = [&p](const std::function<void(Q&)>& f, long long limit) {
            auto db = p.Db();
            Q q = db.From("game_invites").Select("*");
            f(q);
            q.Order("id", true).Limit(limit).Get();
        };
        list([](Q& q) { q.Eq("recipient", "u1").Gt("id", "5"); }, 10);
        list([](Q& q) { q.Eq("recipient", "u1").Gt("id", "0"); }, 30);
        list([](Q& q) { q.Eq("sender", "u1"); }, -2);
        list([](Q& q) { q.Eq("id", "31"); }, 1);
        p.Db().Rpc("respond_invite", "{\"p_id\":31,\"p_accept\":true}");
        p.Db().Rpc("respond_invite", "{\"p_id\":32,\"p_accept\":false}");
        p.Db().Rpc("cancel_invite", "{\"p_id\":33}");
        p.Db().Rpc("cancel_invite", "{\"p_id\":-1}");
    }});
    v.push_back({"parties_mine", [=](ProviderRun& p) {
        p.Db().From("profiles").Select("id").Eq("username", "al ex+\xc3\xa9").Get();
        p.Db().From("party_members").Select("party_id").Eq("member", "u1", asIs).Get();
        p.Db().From("parties").Select("id,leader,game,join_link,max_size").Eq("id", "p-1", asIs).Get();
        p.Db().From("party_members").Select("member,ready,joined_at").Eq("party_id", "p-1", asIs).Order("joined_at").Get();
        p.Db().From("profiles_visible").Select("id,username,display_name,avatar_url,last_seen,playing").In("id", {"u1", "u-2"}, asIs).Get();
        for (int i = 0; i < 2; ++i) p.Db().From("party_members").Select("party_id").Eq("member", "u1", asIs).Get();
    }});
    v.push_back({"parties_actions", [=](ProviderRun& p) {
        p.Db().Rpc("create_party", "{}");
        p.Db().Rpc("join_party", "{\"p_party\":\"p-2\"}");
        p.Db().Rpc("join_party", "{\"p_party\":\"p \\\"3\\\"\"}");
        p.Db().Rpc("leave_party", "{}");
        p.Db().Rpc("kick_from_party", "{\"p_member\":\"u-2\"}");
        p.Db().Rpc("promote_party_leader", "{\"p_member\":\"u-3\\n\"}");
        p.Db().Rpc("promote_party_leader", "{\"p_member\":\"u-4\"}");
        p.Db().From("profiles").Select("id").Eq("username", "alex").Get();
        p.Db().From("party_members").Eq("party_id", "p-1", asIs).Eq("member", "u1", asIs).Update("{\"ready\":true}");
        p.Db().From("party_members").Eq("party_id", "p 1&x", asIs).Eq("member", "u1", asIs).Update("{\"ready\":false}");
        p.Db().From("parties").Eq("id", "p-1", asIs).Update("{\"game\":\"Arena \\\"1\\\"\",\"join_link\":\"okay://join/R\\n\"}");
    }});
    return v;
}

#include "test_okay_backend_golden_community1.inc"   // GOLDEN: the texts recorded from the code before the move

int main() {
    RUN_SUITE("okay_backend_golden_community1");
    const fs::path root = fs::temp_directory_path() / ("okay_backend_golden_community1_" + acc::detail::RandomHex(8));
    const bool print = std::getenv("OKAY_GOLDEN_PRINT") != nullptr;
    const std::vector<Scenario> all = Scenarios();

    // 1. The headers' own functions send exactly the golden requests and make the same of the answers.
    golden::RunScenarios("tests/test_okay_backend_golden_community1.cpp", all, kGolden, root, print);
    if (print) return 0;
    CHECK(g_badSteady == 0);   // the presence time, the invite expiry and the new friend code were what they should be

    // 2. The Supabase provider called directly sends the same bytes.
    const auto flows = ProviderFlows();
    golden::RunProviderFlows<ProviderRun>(flows, kGolden, [](const char*, const char*, ProviderRun& p) { CHECK(!p.rec.sent.empty()); });
    CHECK(flows.size() >= 10);

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
