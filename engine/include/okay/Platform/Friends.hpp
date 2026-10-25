#pragma once
// Friends over Supabase (profiles + friendships + activity tables; see
// docs/sql/social.sql). Drives everything through an okay::account::
// AccountService instance (the same one the launcher and engine already own),
// so it reuses the signed-in session, the anon key and the bearer token, and
// needs no HTTP library of its own. Account::Api() auto-adds the apikey +
// Authorization headers; the tables DEFAULT the "me" columns to auth.uid(), so
// the client never sends its own uuid.
//
// Features: add by username OR by a short shareable friend code; accept/decline
// requests; online/offline + "what are they playing" presence via a heartbeat;
// join a friend's hosted game by its relay session code; and a small activity
// feed (came online / started a game / unlocked an achievement / published).
//
// Everything gracefully no-ops when signed out, on a local (offline) backend,
// or when the tables don't exist yet — Api() returns a non-ok response and the
// calls below fall through — so nothing here breaks a build that hasn't run
// social.sql.
#include "okay/Platform/Account/AccountService.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace okay {

struct Friend {
    std::string id;          // profile/user uuid of the other person
    std::string username;
    std::string status;      // "accepted" | "pending"
    bool incoming = false;   // pending request sent TO me (I can accept/decline)
    bool online   = false;   // last_seen within a few minutes
    long lastSeen = 0;       // last presence heartbeat (unix seconds; 0 = never / hidden)
    std::string playing;     // presence text ("Playing Asteroids", "In the launcher", ...)
    std::string sessionCode; // relay room code if they're hosting a joinable game
    std::string display;     // friendly display name (falls back to username)
    std::string avatar;      // public URL of the player's avatar image (may be empty)
    std::string bio;         // short "about me"
    int         accent = 0;  // profile theme-color index (client palette)
    std::string customStatus;// free-form status / tagline
    std::string favoriteGame;// a game they want to show off
    std::string location;    // optional free-form location
    long rowId    = 0;       // friendships.id (for accept/remove)
};

struct FriendEvent {
    std::string actor;       // who did it (username)
    std::string actorId;     // their user id (profiles.id)
    std::string kind;        // online | playing | achievement | publish
    std::string detail;
    long        when = 0;    // unix time
};

class Friends {
public:
    explicit Friends(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    // Mirror my username into profiles and make sure I have a friend code.
    // Safe to call repeatedly; it only writes a code the first time.
    void EnsureProfile() {
        if (!acct_.IsLoggedIn()) return;
        std::string body = "{\"username\":" + JStr(Me()) + "}";
        acct_.Api("/rest/v1/profiles?on_conflict=id", "POST", body,
                  "Prefer: resolution=merge-duplicates");
        if (MyCode().empty()) {
            std::string code = MakeCode();
            acct_.Api("/rest/v1/profiles?id=eq." + Url(MyId()), "PATCH",
                      "{\"friend_code\":" + JStr(code) + "}");
        }
    }

    // My shareable friend code (empty until EnsureProfile() has run once).
    std::string MyCode() {
        auto p = ProfileOf(MyId());
        return p.friendCode;
    }
    // Friendly display name (what others see); empty until set.
    std::string MyDisplayName() { return ProfileOf(MyId()).displayName; }
    bool SetDisplayName(const std::string& name) {
        if (!acct_.IsLoggedIn()) return false;
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(MyId()), "PATCH",
                         "{\"display_name\":" + JStr(name) + "}").ok;
    }
    // Avatar image URL (public). Empty until set.
    std::string MyAvatar() { return ProfileOf(MyId()).avatar; }
    bool SetAvatar(const std::string& url) {
        if (!acct_.IsLoggedIn()) return false;
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(MyId()), "PATCH",
                         "{\"avatar_url\":" + JStr(url) + "}").ok;
    }
    // Short "about me" bio shown on the profile card.
    std::string MyBio() { return ProfileOf(MyId()).bio; }
    bool SetBio(const std::string& bio) {
        if (!acct_.IsLoggedIn()) return false;
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(MyId()), "PATCH",
                         "{\"bio\":" + JStr(bio) + "}").ok;
    }

    // ---- Steam-style profile customization --------------------------------
    // Fetch my whole customizable profile in one go (for the editor).
    Friend MyProfile() {
        Friend f; Profile p = ProfileOf(MyId());
        SetFrom(f, p); f.id = MyId();
        return f;
    }
    int  MyAccent()        { return ProfileOf(MyId()).accent; }
    bool SetAccent(int a)  { return PatchMe("{\"accent\":" + std::to_string(a) + "}"); }
    std::string MyStatus() { return ProfileOf(MyId()).status; }
    bool SetStatus(const std::string& s)       { return PatchMe("{\"status\":" + JStr(s) + "}"); }
    std::string MyFavoriteGame() { return ProfileOf(MyId()).favoriteGame; }
    bool SetFavoriteGame(const std::string& g) { return PatchMe("{\"favorite_game\":" + JStr(g) + "}"); }
    std::string MyLocation() { return ProfileOf(MyId()).location; }
    bool SetLocation(const std::string& l)     { return PatchMe("{\"location\":" + JStr(l) + "}"); }

    // ---- requests ---------------------------------------------------------
    bool Request(const std::string& username, std::string* err = nullptr) {
        return RequestUid(IdForUsername(username),
                          "No player named '" + username + "'.", err);
    }
    bool RequestByCode(const std::string& code, std::string* err = nullptr) {
        return RequestUid(IdForCode(code),
                          "No player with code '" + code + "'.", err);
    }

    bool Accept(long rowId) {
        return acct_.Api("/rest/v1/friendships?id=eq." + std::to_string(rowId),
                         "PATCH", "{\"status\":\"accepted\"}").ok;
    }
    bool Remove(long rowId) {   // decline a request or unfriend
        return acct_.Api("/rest/v1/friendships?id=eq." + std::to_string(rowId),
                         "DELETE").ok;
    }

    // ---- the friend list --------------------------------------------------
    std::vector<Friend> List() {
        std::vector<Friend> out;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Api("/rest/v1/friendships?select=id,requester,addressee,status", "GET");
        if (!r.ok) return out;
        std::string me = Me();
        for (const auto& obj : SplitObjects(r.body)) {
            Friend f;
            f.rowId  = std::atol(Field(obj, "id").c_str());
            f.status = Field(obj, "status");
            std::string req = Field(obj, "requester"), add = Field(obj, "addressee");
            Profile rp = ProfileOf(req), ap = ProfileOf(add);
            if (rp.username == me) { f.id = add; SetFrom(f, ap); f.incoming = false; }
            else                   { f.id = req; SetFrom(f, rp); f.incoming = true;  }
            if (!f.username.empty()) out.push_back(f);
        }
        return out;
    }

    // Cheap count of incoming pending requests (for a nav badge). One GET; RLS
    // already limits rows to ones involving me, so I just count those addressed
    // to me that are still pending.
    int PendingRequests() {
        if (!acct_.IsLoggedIn()) return 0;
        std::string mine = MyId();
        if (mine.empty()) return 0;
        auto r = acct_.Api("/rest/v1/friendships?select=addressee,status&status=eq.pending", "GET");
        if (!r.ok) return 0;
        int n = 0;
        for (const auto& obj : SplitObjects(r.body))
            if (Field(obj, "addressee") == mine) ++n;
        return n;
    }

    // ---- presence ---------------------------------------------------------
    // Stamp last_seen + what I'm doing. Call every ~60s and whenever my state
    // changes (opened the launcher, launched a game, started hosting).
    void SetPresence(const std::string& playing, const std::string& sessionCode = "") {
        if (!acct_.IsLoggedIn()) return;
        char iso[32]; std::time_t t = std::time(nullptr); std::tm* g = std::gmtime(&t);
        std::string now = (g && std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", g)) ? iso : "";
        std::string body = "{\"last_seen\":" + JStr(now) +
                           ",\"playing\":" + JStr(playing) +
                           ",\"session_code\":" + JStr(sessionCode) + "}";
        acct_.Api("/rest/v1/profiles?id=eq." + Url(MyId()), "PATCH", body);
    }
    void Heartbeat() { SetPresence(lastPresence_.empty() ? "In OkaySpace" : lastPresence_,
                                   lastSession_); }
    // Appear offline to friends immediately: backdate last_seen well past the
    // online window and clear presence. Used by the "Invisible" status.
    void GoOffline() {
        if (!acct_.IsLoggedIn()) return;
        acct_.Api("/rest/v1/profiles?id=eq." + Url(MyId()), "PATCH",
                  "{\"last_seen\":\"2000-01-01T00:00:00Z\",\"playing\":null,\"session_code\":null}");
    }
    // Remember the last presence so a plain Heartbeat() keeps it.
    void MarkPresence(const std::string& playing, const std::string& sessionCode = "") {
        lastPresence_ = playing; lastSession_ = sessionCode; SetPresence(playing, sessionCode);
    }

    // ---- activity feed ----------------------------------------------------
    void Post(const std::string& kind, const std::string& detail) {
        if (!acct_.IsLoggedIn()) return;
        acct_.Api("/rest/v1/activity", "POST",
                  "{\"kind\":" + JStr(kind) + ",\"detail\":" + JStr(detail) +
                  ",\"actor_name\":" + JStr(Me()) + "}");
    }
    std::vector<FriendEvent> Feed(int limit = 20) {
        std::vector<FriendEvent> out;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Api("/rest/v1/activity?select=actor,actor_name,kind,detail,created_at"
                           "&order=created_at.desc&limit=" + std::to_string(limit), "GET");
        if (!r.ok) return out;
        std::vector<std::string> ids;
        for (const auto& obj : SplitObjects(r.body)) {
            FriendEvent e;
            e.actorId = Field(obj, "actor");
            e.actor  = Field(obj, "actor_name");
            e.kind   = Field(obj, "kind");
            e.detail = Field(obj, "detail");
            e.when   = ParseIso(Field(obj, "created_at"));
            if (!e.actorId.empty() && std::find(ids.begin(), ids.end(), e.actorId) == ids.end()) ids.push_back(e.actorId);
            out.push_back(e);
        }
        ResolveActorNames(out, ids);
        return out;
    }

    /// actor_name is whatever the poster's client wrote at the time (older
    /// clients wrote the sign-in email). Show the account's current username.
    void ResolveActorNames(std::vector<FriendEvent>& evs, const std::vector<std::string>& ids) {
        if (!ids.empty()) {
            std::string in;
            for (const auto& id : ids) { if (!in.empty()) in += ","; in += Url(id); }
            auto r = acct_.Api("/rest/v1/profiles?select=id,username&id=in.(" + in + ")", "GET");
            if (r.ok)
                for (const auto& o : SplitObjects(r.body)) {
                    const std::string id = Field(o, "id"), un = Field(o, "username");
                    if (un.empty()) continue;
                    for (auto& e : evs) if (e.actorId == id) e.actor = un;
                }
        }
        for (auto& e : evs) {   // never show a raw email, even when the profile lookup failed
            const auto at = e.actor.find('@');
            if (at != std::string::npos && at > 0) e.actor = e.actor.substr(0, at);
        }
    }

private:
    okay::account::AccountService& acct_;
    std::string lastPresence_, lastSession_;

    struct Profile { std::string username, friendCode, playing, sessionCode, displayName, avatar, bio, status, favoriteGame, location; int accent = 0; long lastSeen = 0; };

    // PATCH my own profile row with a JSON object body.
    bool PatchMe(const std::string& body) {
        if (!acct_.IsLoggedIn()) return false;
        return acct_.Api("/rest/v1/profiles?id=eq." + Url(MyId()), "PATCH", body).ok;
    }

    std::string Me()   const { return acct_.CurrentSession().username; }
    // We don't know our own uuid directly, so look it up by username (cached by
    // the server-side RLS select). Falls back to empty when offline.
    std::string MyId() {
        if (myId_.empty()) {
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(Me()), "GET");
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myId_ = Field(o[0], "id"); }
        }
        return myId_;
    }
    std::string myId_;

    bool RequestUid(const std::string& uid, const std::string& notFound, std::string* err) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return false; }
        if (uid.empty())         { if (err) *err = notFound;         return false; }
        auto r = acct_.Api("/rest/v1/friendships", "POST", "{\"addressee\":" + JStr(uid) + "}");
        if (!r.ok && err) *err = r.body.empty() ? "Request failed." : r.body;
        return r.ok;
    }

    static void SetFrom(Friend& f, const Profile& p) {
        f.username = p.username;
        f.playing  = p.playing;
        f.sessionCode = p.sessionCode;
        f.display  = p.displayName.empty() ? p.username : p.displayName;
        f.avatar   = p.avatar;
        f.bio      = p.bio;
        f.accent       = p.accent;
        f.customStatus = p.status;
        f.favoriteGame = p.favoriteGame;
        f.location     = p.location;
        f.online = p.lastSeen > 0 && std::difftime(std::time(nullptr), (std::time_t)p.lastSeen) < 180.0;
        f.lastSeen = p.lastSeen > 978307200 ? p.lastSeen : 0;   // "Invisible" backdates to 2000
    }

    Profile ProfileOf(const std::string& uid) {
        Profile p;
        if (uid.empty()) return p;
        auto r = acct_.Api("/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen"
                           "&id=eq." + Url(uid), "GET");
        if (!r.ok) return p;
        auto objs = SplitObjects(r.body);
        if (objs.empty()) return p;
        p.username    = Field(objs[0], "username");
        p.friendCode  = Field(objs[0], "friend_code");
        p.playing     = Field(objs[0], "playing");
        p.sessionCode = Field(objs[0], "session_code");
        p.displayName = Field(objs[0], "display_name");
        p.avatar      = Field(objs[0], "avatar_url");
        p.bio         = Field(objs[0], "bio");
        p.accent      = (int)std::atol(Field(objs[0], "accent").c_str());
        p.status      = Field(objs[0], "status");
        p.favoriteGame= Field(objs[0], "favorite_game");
        p.location    = Field(objs[0], "location");
        p.lastSeen    = ParseIso(Field(objs[0], "last_seen"));
        return p;
    }
    std::string IdForUsername(const std::string& name) {
        auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(name), "GET");
        if (!r.ok) return "";
        auto objs = SplitObjects(r.body);
        return objs.empty() ? "" : Field(objs[0], "id");
    }
    std::string IdForCode(const std::string& code) {
        std::string c = Upper(Trim(code));
        auto r = acct_.Api("/rest/v1/profiles?select=id&friend_code=eq." + Url(c), "GET");
        if (!r.ok) return "";
        auto objs = SplitObjects(r.body);
        return objs.empty() ? "" : Field(objs[0], "id");
    }

    // A readable 6-char code from a time+username seed (no ambiguous chars).
    std::string MakeCode() {
        static const char* A = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no 0/O/1/I
        unsigned long h = 1469598103u;
        std::string seed = Me() + std::to_string((unsigned long)std::time(nullptr));
        for (char c : seed) { h ^= (unsigned char)c; h *= 16777619u; }
        std::string out; for (int i = 0; i < 6; ++i) { out += A[h % 31]; h /= 31; h ^= (h << 7); }
        return out;
    }

    static long ParseIso(const std::string& ls) {
        if (ls.size() < 19) return 0;
        std::tm tm{};
        if (sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d",
                   &tm.tm_year,&tm.tm_mon,&tm.tm_mday,&tm.tm_hour,&tm.tm_min,&tm.tm_sec) != 6) return 0;
        tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
        return (long)_mkgmtime(&tm);
#else
        return (long)timegm(&tm);
#endif
    }

    // ---- tiny string/JSON helpers ----
    static std::string Trim(const std::string& s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b-1])) --b;
        return s.substr(a, b - a);
    }
    static std::string Upper(const std::string& s) {
        std::string o = s; for (char& c : o) c = (char)std::toupper((unsigned char)c); return o;
    }
public:
    // Tiny JSON helpers (public so the tests can reach them). Profile text is
    // free-form (a multi-line bio, quotes, braces), so strings are read and
    // written with their escapes.
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; std::size_t start = std::string::npos;
        bool inStr = false;
        for (std::size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true;
            else if (c == '{') { if (depth == 0) start = i; ++depth; }
            else if (c == '}' && depth > 0) { if (--depth == 0 && start != std::string::npos) { out.push_back(json.substr(start, i - start + 1)); start = std::string::npos; } }
        }
        return out;
    }
    static std::string Field(const std::string& obj, const std::string& key) {
        std::string k = "\"" + key + "\"";
        std::size_t p = obj.find(k); if (p == std::string::npos) return "";
        p = obj.find(':', p + k.size()); if (p == std::string::npos) return "";
        ++p; while (p < obj.size() && (obj[p] == ' ')) ++p;
        if (p >= obj.size()) return "";
        if (obj[p] == '"') { std::string v; okay::account::detail::JsonReadString(obj, p, v); return v; }
        std::size_t e = p; while (e < obj.size() && obj[e] != ',' && obj[e] != '}') ++e;
        std::string v = obj.substr(p, e - p);
        while (!v.empty() && (v.back() == ' ')) v.pop_back();
        return v == "null" ? "" : v;
    }
    static std::string JStr(const std::string& s) {   // JSON string literal (control characters escaped)
        std::string o = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
            else if (c == '\n') o += "\\n";
            else if (c == '\t') o += "\\t";
            else if (c == '\r') o += "\\r";
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += (char)c;
        }
        o += "\""; return o;
    }
    static std::string Url(const std::string& s) {     // minimal URL-encode for query values
        std::string o; char b[4];
        for (unsigned char c : s) {
            if (std::isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') o += (char)c;
            else { std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
        }
        return o;
    }
};

} // namespace okay
