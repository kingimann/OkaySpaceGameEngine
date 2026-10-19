#pragma once
// Friends over Supabase (profiles + friendships tables; see docs/sql/social.sql).
// Drives everything through an okay::account::AccountService instance (the same
// one the launcher and engine already own), so it reuses the signed-in session,
// the anon key and the bearer token, and needs no HTTP library of its own.
// Account::Api() auto-adds the apikey + Authorization headers; the tables
// DEFAULT requester/id to auth.uid(), so the client never sends its own uuid.
//
// Everything gracefully no-ops when signed out, on a local (offline) backend,
// or when the tables don't exist yet — Api() returns a non-ok response and the
// calls below fall through — so nothing here breaks a build that hasn't run
// social.sql.
#include "okay/Platform/Account/AccountService.hpp"
#include <string>
#include <vector>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace okay {

struct Friend {
    std::string id;        // profile/user uuid of the other person
    std::string username;
    std::string status;    // "accepted" | "pending"
    bool incoming = false; // pending request sent TO me (I can accept/decline)
    bool online   = false; // last_seen within a few minutes
    long rowId    = 0;     // friendships.id (for accept/remove)
};

class Friends {
public:
    explicit Friends(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    // Mirror my username into profiles (the signup trigger does this for new
    // accounts; call once after login so pre-existing accounts get a row too).
    void EnsureProfile() {
        if (!acct_.IsLoggedIn()) return;
        std::string body = "{\"username\":" + JStr(Me()) + "}";
        acct_.Api("/rest/v1/profiles?on_conflict=id", "POST", body,
                  "Prefer: resolution=merge-duplicates");
    }

    // Send a friend request to a username. Returns true on a 2xx.
    bool Request(const std::string& username, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return false; }
        std::string uid = IdForUsername(username);
        if (uid.empty()) { if (err) *err = "No player named '" + username + "'."; return false; }
        auto r = acct_.Api("/rest/v1/friendships", "POST",
                           "{\"addressee\":" + JStr(uid) + "}");
        if (!r.ok && err) *err = r.body.empty() ? "Request failed." : r.body;
        return r.ok;
    }

    bool Accept(long rowId) {
        return acct_.Api("/rest/v1/friendships?id=eq." + std::to_string(rowId),
                         "PATCH", "{\"status\":\"accepted\"}").ok;
    }
    bool Remove(long rowId) {   // decline a request or unfriend
        return acct_.Api("/rest/v1/friendships?id=eq." + std::to_string(rowId),
                         "DELETE").ok;
    }

    // Everyone I'm connected to or have a pending request with. RLS returns only
    // rows involving me; I resolve the OTHER side's username via profiles.
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
            // Map both uuids to usernames; the friend is the one that isn't me.
            std::string reqName = UsernameFor(req), addName = UsernameFor(add);
            if (reqName == me) { f.id = add; f.username = addName; f.incoming = false; }
            else               { f.id = req; f.username = reqName; f.incoming = true;  }
            f.online = LastSeenOnline(f.id);
            if (!f.username.empty()) out.push_back(f);
        }
        return out;
    }

    // Heartbeat: stamp my last_seen so friends see me online. Call every ~60s.
    void Heartbeat() {
        if (!acct_.IsLoggedIn()) return;
        char iso[32]; std::time_t t = std::time(nullptr); std::tm* g = std::gmtime(&t);
        if (g) { std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", g);
                 acct_.Api("/rest/v1/profiles?username=eq." + Url(Me()),
                           "PATCH", std::string("{\"last_seen\":\"") + iso + "\"}"); }
    }

private:
    okay::account::AccountService& acct_;

    std::string Me() const { return acct_.CurrentSession().username; }

    std::string IdForUsername(const std::string& name) {
        auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(name), "GET");
        if (!r.ok) return "";
        auto objs = SplitObjects(r.body);
        return objs.empty() ? "" : Field(objs[0], "id");
    }
    std::string UsernameFor(const std::string& uid) {
        if (uid.empty()) return "";
        auto r = acct_.Api("/rest/v1/profiles?select=username&id=eq." + Url(uid), "GET");
        if (!r.ok) return "";
        auto objs = SplitObjects(r.body);
        return objs.empty() ? "" : Field(objs[0], "username");
    }
    bool LastSeenOnline(const std::string& uid) {
        if (uid.empty()) return false;
        auto r = acct_.Api("/rest/v1/profiles?select=last_seen&id=eq." + Url(uid), "GET");
        if (!r.ok) return false;
        auto objs = SplitObjects(r.body);
        if (objs.empty()) return false;
        std::string ls = Field(objs[0], "last_seen");
        if (ls.size() < 19) return false;
        std::tm tm{}; if (sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d",
                        &tm.tm_year,&tm.tm_mon,&tm.tm_mday,&tm.tm_hour,&tm.tm_min,&tm.tm_sec) != 6) return false;
        tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
        std::time_t seen = _mkgmtime(&tm);
#else
        std::time_t seen = timegm(&tm);
#endif
        return std::difftime(std::time(nullptr), seen) < 180.0;   // seen in the last 3 min
    }

    // ---- tiny JSON helpers ----
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; std::size_t start = std::string::npos;
        for (std::size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (c == '{') { if (depth == 0) start = i; ++depth; }
            else if (c == '}') { if (--depth == 0 && start != std::string::npos) { out.push_back(json.substr(start, i - start + 1)); start = std::string::npos; } }
        }
        return out;
    }
    static std::string Field(const std::string& obj, const std::string& key) {
        std::string k = "\"" + key + "\"";
        std::size_t p = obj.find(k); if (p == std::string::npos) return "";
        p = obj.find(':', p + k.size()); if (p == std::string::npos) return "";
        ++p; while (p < obj.size() && (obj[p] == ' ')) ++p;
        if (p >= obj.size()) return "";
        if (obj[p] == '"') { std::size_t e = obj.find('"', p + 1); return e == std::string::npos ? "" : obj.substr(p + 1, e - p - 1); }
        std::size_t e = p; while (e < obj.size() && obj[e] != ',' && obj[e] != '}') ++e;
        std::string v = obj.substr(p, e - p);
        while (!v.empty() && (v.back() == ' ')) v.pop_back();
        return v == "null" ? "" : v;
    }
    static std::string JStr(const std::string& s) {   // minimal JSON string literal
        std::string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } o += "\""; return o;
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
