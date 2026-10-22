#pragma once
// Public (Steam-style) player profiles over Supabase. Reads one player's
// public profile row, their showcase picks, publicly mirrored achievements,
// published Workshop games, friend count and (when we're friends) recent
// activity, plus my relationship to them. Also saves MY showcase and mirrors
// my local achievements so others can see them.
//
// Tables: profiles, friendships, activity, workshop_items (docs/sql/social.sql)
// and the optional showcase additions in docs/sql/profile_showcase.sql
// (profiles.showcase_* columns, user_achievements, profile_friend_count()).
// Everything degrades gracefully when that migration hasn't been run: the
// showcase/achievement fields simply come back "unsupported" and the caller
// hides them (or falls back to local data for the signed-in player).
//
// Like Friends/Workshop this drives an okay::account::AccountService (reusing
// the session + anon key + bearer token). Calls block on HTTP, so the launcher
// runs them on a worker thread.
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Achievements.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Workshop.hpp"
#include <string>
#include <vector>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace okay {

struct PublicProfile {
    std::string id, username, display, avatar, bio, customStatus, favoriteGame, location;
    std::string playing, sessionCode;
    int  accent = 0;
    int  bannerAccent = -1;          // -1 = follow accent
    bool online = false;
    long lastSeen = 0, memberSince = 0;
    std::vector<std::string> showcaseAch;   // achievement ids, in display order
    std::vector<long>        showcaseGames; // workshop_items ids, in display order
    bool verified = false;                  // staff-set blue check (profile_extras.sql)
    std::vector<std::pair<std::string, std::string>> socials;   // ("x", "@handle" or url), in display order
};

// Someone in a followers / following list.
struct FollowEntry { std::string id, username, display, avatar; long lastSeen = 0; bool online = false; };

// Time played in one game (or "editor" for time in the editor).
struct PlaytimeEntry { std::string key, title; long seconds = 0, lastPlayed = 0; };

struct ProfileBundle {
    bool found = false;              // the profile row was read
    bool reached = false;            // the server answered at all
    bool failed = false;             // it answered with an error (not "no such player")
    PublicProfile p;
    bool showcaseSupported = false;  // profiles.showcase_* columns exist
    bool achSupported = false;       // user_achievements table exists
    bool extrasSupported = false;    // profile_extras.sql ran (socials, verified, badges, playtime)
    std::vector<std::string> badges;         // staff-awarded: staff, developer, moderator, beta, partner
    std::vector<PlaytimeEntry> playtime;     // most played first
    std::vector<Achievement> achievements;   // their public unlocks (newest first)
    std::vector<WorkshopItem> games;         // their published Workshop items
    int  friends = -1;               // -1 = unknown
    // Follows (docs/sql/follows.sql): one-way, no request.
    bool followSupported = false;
    int  followers = -1, following = -1;
    bool iFollow = false, followsMe = false;
    std::vector<FriendEvent> activity;       // visible only to friends (RLS)
    // My relationship to them.
    bool isMe = false;
    long friendRow = 0;              // friendships.id (0 = none)
    std::string friendStatus;        // "" | "pending" | "accepted"
    bool incoming = false;           // pending request they sent me
};

class Profiles {
public:
    explicit Profiles(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    // Load a whole profile page. `target` is a user uuid, or a username when
    // byUsername is set (forum authors only carry the name).
    ProfileBundle Load(const std::string& target, bool byUsername) {
        ProfileBundle b;
        if (!acct_.IsLoggedIn() || target.empty()) return b;
        std::string uid = target;
        if (byUsername) {
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(target), "GET");
            b.reached = r.reached;
            if (!r.ok) { b.failed = r.reached; return b; }
            auto o = SplitObjects(r.body);
            if (o.empty()) return b;
            uid = Field(o[0], "id");
        }
        static const char* kBase =
            "id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,"
            "playing,session_code,last_seen,created_at";
        auto r = acct_.Api(std::string("/rest/v1/profiles?select=") + kBase +
                           ",showcase_achievements,showcase_games,banner_accent,social_links,verified&id=eq." + Url(uid), "GET");
        b.reached = b.reached || r.reached;
        if (r.ok) { b.showcaseSupported = true; b.extrasSupported = true; }
        else {
            r = acct_.Api(std::string("/rest/v1/profiles?select=") + kBase +
                          ",showcase_achievements,showcase_games,banner_accent&id=eq." + Url(uid), "GET");
            if (r.ok) b.showcaseSupported = true;
            else      r = acct_.Api(std::string("/rest/v1/profiles?select=") + kBase + "&id=eq." + Url(uid), "GET");
        }
        if (!r.ok) { b.failed = r.reached; return b; }
        auto objs = SplitObjects(r.body);
        if (objs.empty()) return b;
        const std::string& o = objs[0];
        PublicProfile& p = b.p;
        p.id           = Field(o, "id");
        p.username     = Field(o, "username");
        p.display      = Field(o, "display_name");
        if (p.display.empty()) p.display = p.username;
        p.avatar       = Field(o, "avatar_url");
        p.bio          = Field(o, "bio");
        p.accent       = (int)std::atol(Field(o, "accent").c_str());
        p.customStatus = Field(o, "status");
        p.favoriteGame = Field(o, "favorite_game");
        p.location     = Field(o, "location");
        p.playing      = Field(o, "playing");
        p.sessionCode  = Field(o, "session_code");
        p.lastSeen     = ParseIso(Field(o, "last_seen"));
        p.memberSince  = ParseIso(Field(o, "created_at"));
        p.online = p.lastSeen > 0 && std::difftime(std::time(nullptr), (std::time_t)p.lastSeen) < 180.0;
        if (b.showcaseSupported) {
            p.showcaseAch = Array(o, "showcase_achievements");
            for (const auto& s : Array(o, "showcase_games")) { long id = std::atol(s.c_str()); if (id > 0) p.showcaseGames.push_back(id); }
            std::string ba = Field(o, "banner_accent");
            p.bannerAccent = ba.empty() ? -1 : (int)std::atol(ba.c_str());
        }
        if (b.extrasSupported) {
            p.verified = Field(o, "verified") == "true";
            p.socials = FlatObject(RawField(o, "social_links"));
        }
        b.found = true;
        b.isMe = (p.id == acct_.UserId()) || (p.username == acct_.CurrentSession().username);

        // Public achievements mirror (optional table).
        auto ra = acct_.Api("/rest/v1/user_achievements?select=ach_id,label,unlocked_at,points"
                            "&owner=eq." + Url(uid) + "&order=unlocked_at.desc", "GET");
        if (!ra.ok) ra = acct_.Api("/rest/v1/user_achievements?select=ach_id,label,unlocked_at"
                                   "&owner=eq." + Url(uid) + "&order=unlocked_at.desc", "GET");
        if (ra.ok) {
            b.achSupported = true;
            for (const auto& x : SplitObjects(ra.body)) {
                Achievement a; a.id = Field(x, "ach_id"); a.label = Field(x, "label");
                if (a.label.empty()) a.label = a.id;
                a.when = ParseIso(Field(x, "unlocked_at"));
                const std::string pt = Field(x, "points");
                if (!pt.empty()) a.points = Achievements::ClampPoints((int)std::atol(pt.c_str()));
                if (!a.id.empty()) b.achievements.push_back(a);
            }
        }
        // Published Workshop games.
        auto rg = acct_.Api("/rest/v1/workshop_items?select=id,author_name,title,description,kind,storage_path,downloads"
                            "&author=eq." + Url(uid) + "&order=downloads.desc", "GET");
        if (rg.ok) {
            for (const auto& x : SplitObjects(rg.body)) {
                WorkshopItem w;
                w.id = std::atol(Field(x, "id").c_str()); w.author = Field(x, "author_name");
                w.title = Field(x, "title"); w.description = Field(x, "description");
                w.kind = Field(x, "kind"); w.url = Field(x, "storage_path");
                w.downloads = std::atol(Field(x, "downloads").c_str());
                if (w.id > 0 && !w.title.empty()) b.games.push_back(w);
            }
        }
        if (b.extrasSupported) {   // staff badges + time played
            auto rb = acct_.Api("/rest/v1/profile_badges?select=badge&owner=eq." + Url(uid) + "&order=awarded_at.asc", "GET");
            if (rb.ok) for (const auto& x : SplitObjects(rb.body)) { std::string bd = Field(x, "badge"); if (!bd.empty()) b.badges.push_back(bd); }
            auto rp = acct_.Api("/rest/v1/user_playtime?select=game_key,title,seconds,last_played&owner=eq." + Url(uid) +
                                "&order=seconds.desc&limit=30", "GET");
            if (rp.ok) for (const auto& x : SplitObjects(rp.body)) {
                PlaytimeEntry e; e.key = Field(x, "game_key"); e.title = Field(x, "title");
                e.seconds = std::atol(Field(x, "seconds").c_str()); e.lastPlayed = ParseIso(Field(x, "last_played"));
                if (!e.key.empty() && e.seconds > 0) b.playtime.push_back(e);
            }
        }
        // Friend count (SECURITY DEFINER rpc; my own count works without it).
        auto rc = acct_.Api("/rest/v1/rpc/profile_friend_count", "POST", "{\"p_uid\":" + JStr(uid) + "}");
        if (rc.ok) {
            std::string t = Trim(rc.body);
            if (!t.empty() && std::isdigit((unsigned char)t[0])) b.friends = (int)std::atol(t.c_str());
        }
        // Follows: counts, and whether we follow each other.
        auto rfc = acct_.Api("/rest/v1/rpc/follow_counts", "POST", "{\"p_uid\":" + JStr(uid) + "}");
        if (rfc.ok) {
            b.followSupported = true;
            auto fo = SplitObjects(rfc.body);
            if (!fo.empty()) {
                b.followers = (int)std::atol(Field(fo[0], "followers").c_str());
                b.following = (int)std::atol(Field(fo[0], "following").c_str());
            }
            const std::string me = MyId();
            if (!b.isMe && !me.empty()) {
                auto rr = acct_.Api("/rest/v1/follows?select=follower,followee&or=(and(follower.eq." + Url(me) + ",followee.eq." + Url(uid) +
                                    "),and(follower.eq." + Url(uid) + ",followee.eq." + Url(me) + "))", "GET");
                if (rr.ok) for (const auto& x : SplitObjects(rr.body)) {
                    if (Field(x, "follower") == me) b.iFollow = true; else b.followsMe = true;
                }
            }
        }
        // Relationship (RLS only returns friendships that involve me).
        auto rf = acct_.Api("/rest/v1/friendships?select=id,status,requester,addressee"
                            "&or=(requester.eq." + Url(uid) + ",addressee.eq." + Url(uid) + ")", "GET");
        if (rf.ok) {
            int accepted = 0;
            for (const auto& x : SplitObjects(rf.body)) {
                std::string st = Field(x, "status");
                if (b.isMe) { if (st == "accepted") ++accepted; continue; }
                b.friendRow = std::atol(Field(x, "id").c_str());
                b.friendStatus = st;
                b.incoming = Field(x, "requester") == uid;
            }
            if (b.isMe && b.friends < 0) b.friends = accepted;
        }
        // Recent activity (readable when they're me or a friend).
        auto rv = acct_.Api("/rest/v1/activity?select=actor_name,kind,detail,created_at"
                            "&actor=eq." + Url(uid) + "&order=created_at.desc&limit=8", "GET");
        if (rv.ok) {
            for (const auto& x : SplitObjects(rv.body)) {
                FriendEvent e; e.actor = Field(x, "actor_name"); e.kind = Field(x, "kind");
                if (!b.p.username.empty()) e.actor = b.p.username;   // never the old email
                e.detail = Field(x, "detail"); e.when = ParseIso(Field(x, "created_at"));
                b.activity.push_back(e);
            }
        }
        return b;
    }

    // Save MY showcase picks (needs profile_showcase.sql). bannerAccent -1
    // clears the override. Returns false when the columns don't exist.
    bool SaveShowcase(const std::vector<std::string>& ach, const std::vector<long>& games, int bannerAccent) {
        if (!acct_.IsLoggedIn()) return false;
        std::string id = MyId();
        if (id.empty()) return false;
        std::string a = "[", g = "[";
        for (std::size_t i = 0; i < ach.size() && i < 6; ++i)   { if (i) a += ","; a += JStr(ach[i]); }
        for (std::size_t i = 0; i < games.size() && i < 4; ++i) { if (i) g += ","; g += std::to_string(games[i]); }
        a += "]"; g += "]";
        std::string body = "{\"showcase_achievements\":" + a + ",\"showcase_games\":" + g +
                           ",\"banner_accent\":" + (bannerAccent < 0 ? std::string("null") : std::to_string(bannerAccent)) + "}";
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(id), "PATCH", body).ok;
    }

    // Mirror my local achievements into user_achievements (upsert; owner
    // defaults to auth.uid()). Returns false when the table doesn't exist.
    bool SyncAchievements(const std::vector<Achievement>& all) {
        if (!acct_.IsLoggedIn() || all.empty()) return false;
        std::string body = "[";
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (i) body += ",";
            body += "{\"ach_id\":" + JStr(all[i].id) + ",\"label\":" + JStr(all[i].label);
            if (all[i].when > 0) body += ",\"unlocked_at\":" + JStr(Iso(all[i].when));
            body += ",\"points\":" + std::to_string(all[i].points);
            body += "}";
        }
        body += "]";
        if (acct_.Api("/rest/v1/user_achievements?on_conflict=owner,ach_id", "POST", body,
                      "Prefer: resolution=merge-duplicates").ok) return true;
        // Before profile_extras.sql there's no points column: send without it.
        std::string plain; std::size_t at = 0;
        const std::string key = ",\"points\":";
        for (std::size_t k; (k = body.find(key, at)) != std::string::npos; ) {
            plain += body.substr(at, k - at);
            at = body.find('}', k);
        }
        plain += body.substr(at);
        return acct_.Api("/rest/v1/user_achievements?on_conflict=owner,ach_id", "POST", plain,
                         "Prefer: resolution=merge-duplicates").ok;
    }

    // MY social links; *supported = false before profile_extras.sql ran.
    std::vector<std::pair<std::string, std::string>> MySocialLinks(bool* supported) {
        if (supported) *supported = false;
        if (!acct_.IsLoggedIn()) return {};
        std::string id = MyId(); if (id.empty()) return {};
        auto r = acct_.Api("/rest/v1/profiles?select=social_links&id=eq." + Url(id), "GET");
        if (!r.ok) return {};
        if (supported) *supported = true;
        auto o = SplitObjects(r.body);
        return o.empty() ? std::vector<std::pair<std::string, std::string>>() : FlatObject(RawField(o[0], "social_links"));
    }
    // Save MY social links ({"x": "...", ...}; needs profile_extras.sql).
    bool SaveSocialLinks(const std::vector<std::pair<std::string, std::string>>& links) {
        if (!acct_.IsLoggedIn()) return false;
        std::string id = MyId(); if (id.empty()) return false;
        std::string o = "{";
        bool first = true;
        for (const auto& kv : links) {
            if (kv.second.empty()) continue;
            if (!first) o += ",";
            o += JStr(kv.first) + ":" + JStr(kv.second.substr(0, 200)); first = false;
        }
        o += "}";
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(id), "PATCH", "{\"social_links\":" + o + "}").ok;
    }
    // Upsert my playtime totals (needs profile_extras.sql).
    bool SyncPlaytime(const std::vector<PlaytimeEntry>& all) {
        if (!acct_.IsLoggedIn() || all.empty()) return false;
        std::string body = "[";
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (i) body += ",";
            body += "{\"game_key\":" + JStr(all[i].key.substr(0, 200)) + ",\"title\":" + JStr(all[i].title.substr(0, 120)) +
                    ",\"seconds\":" + std::to_string(std::max(0L, all[i].seconds));
            if (all[i].lastPlayed > 0) body += ",\"last_played\":" + JStr(Iso(all[i].lastPlayed));
            body += "}";
        }
        body += "]";
        return acct_.Api("/rest/v1/user_playtime?on_conflict=owner,game_key", "POST", body,
                         "Prefer: resolution=merge-duplicates").ok;
    }

    // Relationship actions (RLS scopes them to rows involving me).
    // ---- follows -----------------------------------------------------------
    bool Follow(const std::string& uid) {
        return acct_.Api("/rest/v1/follows", "POST", "{\"followee\":" + JStr(uid) + "}").ok;
    }
    bool Unfollow(const std::string& uid) {
        const std::string me = MyId(); if (me.empty()) return false;
        return acct_.Api("/rest/v1/follows?follower=eq." + Url(me) + "&followee=eq." + Url(uid), "DELETE").ok;
    }
    bool RemoveFollower(const std::string& uid) {
        const std::string me = MyId(); if (me.empty()) return false;
        return acct_.Api("/rest/v1/follows?follower=eq." + Url(uid) + "&followee=eq." + Url(me), "DELETE").ok;
    }
    /// The people following `uid` (followers=true) or that `uid` follows.
    /// Empty `uid` = me. `supported` reports whether follows.sql has run.
    std::vector<FollowEntry> FollowList(std::string uid, bool followers, bool* supported = nullptr) {
        std::vector<FollowEntry> out;
        if (supported) *supported = false;
        if (uid.empty()) uid = MyId();
        if (uid.empty()) return out;
        auto r = acct_.Api(std::string("/rest/v1/follows?select=follower,followee&") + (followers ? "followee" : "follower") +
                           "=eq." + Url(uid) + "&order=created_at.desc&limit=200", "GET");
        if (!r.ok) return out;
        if (supported) *supported = true;
        std::vector<std::string> ids;
        for (const auto& x : SplitObjects(r.body)) {
            std::string id = Field(x, followers ? "follower" : "followee");
            if (!id.empty()) ids.push_back(id);
        }
        if (ids.empty()) return out;
        std::string in;
        for (const auto& id : ids) { if (!in.empty()) in += ","; in += Url(id); }
        auto rp = acct_.Api("/rest/v1/profiles?select=id,username,display_name,avatar_url,last_seen&id=in.(" + in + ")", "GET");
        if (!rp.ok) return out;
        const auto objs = SplitObjects(rp.body);
        for (const auto& id : ids)   // keep newest-first order
            for (const auto& o : objs) {
                if (Field(o, "id") != id) continue;
                FollowEntry e; e.id = id; e.username = Field(o, "username");
                e.display = Field(o, "display_name"); if (e.display.empty()) e.display = e.username;
                e.avatar = Field(o, "avatar_url"); e.lastSeen = ParseIso(Field(o, "last_seen"));
                e.online = e.lastSeen > 0 && std::difftime(std::time(nullptr), (std::time_t)e.lastSeen) < 180.0;
                out.push_back(e);
            }
        return out;
    }
    /// Follow by username (the Friends page's "Add" box).
    bool FollowByName(const std::string& username, std::string* err = nullptr) {
        auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(username), "GET");
        auto o = r.ok ? SplitObjects(r.body) : std::vector<std::string>{};
        if (o.empty()) { if (err) *err = "No player with that username."; return false; }
        const std::string id = Field(o[0], "id");
        if (id == MyId()) { if (err) *err = "That's you!"; return false; }
        if (!Follow(id)) { if (err) *err = "Couldn't follow them (already following?)."; return false; }
        return true;
    }
    /// Whether my followers see my activity feed (profiles.followers_see_activity).
    bool SetFollowersSeeActivity(bool on) {
        const std::string me = MyId(); if (me.empty()) return false;
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(me), "PATCH",
                         std::string("{\"followers_see_activity\":") + (on ? "true" : "false") + "}").ok;
    }

    bool AddFriend(const std::string& uid) {
        if (!acct_.IsLoggedIn() || uid.empty()) return false;
        return acct_.Api("/rest/v1/friendships", "POST", "{\"addressee\":" + JStr(uid) + "}").ok;
    }
    bool Accept(long row) {
        return row > 0 && acct_.Api("/rest/v1/friendships?id=eq." + std::to_string(row), "PATCH",
                                    "{\"status\":\"accepted\"}").ok;
    }
    bool Remove(long row) {
        return row > 0 && acct_.Api("/rest/v1/friendships?id=eq." + std::to_string(row), "DELETE").ok;
    }

private:
    okay::account::AccountService& acct_;
    std::string myId_;

    std::string MyId() {
        if (myId_.empty()) {
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(acct_.CurrentSession().username), "GET");
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myId_ = Field(o[0], "id"); }
        }
        return myId_;
    }

    static std::string Iso(long t) {
        char iso[32]; std::time_t tt = (std::time_t)t; std::tm* g = std::gmtime(&tt);
        return (g && std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", g)) ? iso : "";
    }
    static long ParseIso(const std::string& ls) {
        if (ls.size() < 19) return 0;
        std::tm tm{};
        if (std::sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d",
                        &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return 0;
        tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
        return (long)_mkgmtime(&tm);
#else
        return (long)timegm(&tm);
#endif
    }
    static std::string Trim(const std::string& s) {
        std::size_t a = 0, e = s.size();
        while (a < e && std::isspace((unsigned char)s[a])) ++a;
        while (e > a && std::isspace((unsigned char)s[e - 1])) --e;
        return s.substr(a, e - a);
    }
    // Top-level JSON objects, skipping braces inside strings.
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; bool str = false;
        std::size_t start = std::string::npos;
        for (std::size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (str) { if (c == '\\') ++i; else if (c == '"') str = false; continue; }
            if (c == '"') str = true;
            else if (c == '{') { if (depth++ == 0) start = i; }
            else if (c == '}' && depth > 0) {
                if (--depth == 0 && start != std::string::npos) { out.push_back(json.substr(start, i - start + 1)); start = std::string::npos; }
            }
        }
        return out;
    }
    static std::size_t ValueAt(const std::string& obj, const std::string& key) {
        std::string k = "\"" + key + "\"";
        std::size_t p = obj.find(k); if (p == std::string::npos) return p;
        p = obj.find(':', p + k.size()); if (p == std::string::npos) return p;
        ++p; while (p < obj.size() && std::isspace((unsigned char)obj[p])) ++p;
        return p < obj.size() ? p : std::string::npos;
    }
    static std::string ReadString(const std::string& s, std::size_t& p) {   // s[p] == '"'
        std::string v; ++p;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                char e = s[++p];
                v += (e == 'n' ? '\n' : e == 't' ? '\t' : e);
            } else v += s[p];
            ++p;
        }
        ++p;
        return v;
    }
    static std::string Field(const std::string& obj, const std::string& key) {
        std::size_t p = ValueAt(obj, key); if (p == std::string::npos) return "";
        if (obj[p] == '"') return ReadString(obj, p);
        std::size_t e = p; while (e < obj.size() && obj[e] != ',' && obj[e] != '}') ++e;
        std::string v = Trim(obj.substr(p, e - p));
        return v == "null" ? "" : v;
    }
    // The raw text of an object-valued field ({...}), or "".
    static std::string RawField(const std::string& obj, const std::string& key) {
        std::size_t p = ValueAt(obj, key);
        if (p == std::string::npos || obj[p] != '{') return "";
        int depth = 0; bool str = false;
        for (std::size_t i = p; i < obj.size(); ++i) {
            const char c = obj[i];
            if (str) { if (c == '\\') ++i; else if (c == '"') str = false; continue; }
            if (c == '"') str = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) return obj.substr(p, i - p + 1);
        }
        return "";
    }
    // {"k": "v", ...} with string values -> pairs (other values skipped).
    static std::vector<std::pair<std::string, std::string>> FlatObject(const std::string& o) {
        std::vector<std::pair<std::string, std::string>> out;
        std::size_t p = o.empty() ? 0 : 1;
        while (p < o.size()) {
            if (o[p] != '"') { ++p; continue; }
            std::string k = ReadString(o, p);
            while (p < o.size() && (o[p] == ':' || std::isspace((unsigned char)o[p]))) ++p;
            if (p < o.size() && o[p] == '"') { std::string v = ReadString(o, p); if (!k.empty() && !v.empty()) out.emplace_back(k, v); }
            else while (p < o.size() && o[p] != ',' && o[p] != '}') ++p;
        }
        return out;
    }
    // A flat JSON array of strings or numbers -> strings.
    static std::vector<std::string> Array(const std::string& obj, const std::string& key) {
        std::vector<std::string> out;
        std::size_t p = ValueAt(obj, key);
        if (p == std::string::npos || obj[p] != '[') return out;
        ++p;
        while (p < obj.size() && obj[p] != ']') {
            char c = obj[p];
            if (c == '"') { out.push_back(ReadString(obj, p)); continue; }
            if (c == ',' || std::isspace((unsigned char)c)) { ++p; continue; }
            std::size_t e = p; while (e < obj.size() && obj[e] != ',' && obj[e] != ']') ++e;
            std::string v = Trim(obj.substr(p, e - p));
            if (!v.empty() && v != "null") out.push_back(v);
            p = e;
        }
        return out;
    }
    static std::string JStr(const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else if (c == '\t') o += "\\t";
            else if (c != '\r') o += c;
        }
        return o + "\"";
    }
    static std::string Url(const std::string& s) {
        std::string o; char b[4];
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
            else { std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
        }
        return o;
    }
};

} // namespace okay
