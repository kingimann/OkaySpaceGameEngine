#pragma once
// Parties over Supabase (parties / party_members; see docs/sql/parties.sql): a
// group of friends that plays together. One party at a time; the leader's game
// session is stored on the party so members' launchers can follow it. Creating,
// joining, leaving, kicking and promoting go through SECURITY DEFINER functions.
// Drives an okay::account::AccountService; no-ops when signed out / offline.
#include "okay/Platform/Account/AccountService.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace okay {

struct PartyMember {
    std::string uid, username, display, avatar;
    bool ready = false;
    bool online = false;
    std::string playing;        // their presence text
    long joinedAt = 0;
    std::string Name() const { return display.empty() ? username : display; }
};

struct Party {
    std::string id;             // "" = not in a party
    std::string leader;         // leader's uid
    std::string game;           // what the leader is playing ("" = nothing)
    std::string joinLink;       // the leader's session; members follow it
    int maxSize = 8;
    std::vector<PartyMember> members;   // in join order
    bool Valid() const { return !id.empty(); }
    const PartyMember* Find(const std::string& uid) const {
        for (const auto& m : members) if (m.uid == uid) return &m;
        return nullptr;
    }
};

class Parties {
public:
    explicit Parties(okay::account::AccountService& acct) : acct_(acct) {}

    /// My current party (out.id empty when I'm not in one). False on a failed request.
    bool Mine(Party& out) {
        out = Party{};
        if (!acct_.IsLoggedIn()) return false;
        const std::string me = MyId();
        if (me.empty()) return false;
        auto r = acct_.Api("/rest/v1/party_members?select=party_id&member=eq." + me, "GET");
        if (!r.ok) return false;
        auto rows = SplitObjects(r.body);
        if (rows.empty()) return true;   // not in a party
        const std::string pid = Field(rows[0], "party_id");
        auto pr = acct_.Api("/rest/v1/parties?select=id,leader,game,join_link,max_size&id=eq." + pid, "GET");
        if (!pr.ok) return false;
        auto ps = SplitObjects(pr.body);
        if (ps.empty()) return true;
        out.id = Field(ps[0], "id"); out.leader = Field(ps[0], "leader");
        out.game = Field(ps[0], "game"); out.joinLink = Field(ps[0], "join_link");
        out.maxSize = std::max(1, std::atoi(Field(ps[0], "max_size").c_str()));
        auto mr = acct_.Api("/rest/v1/party_members?select=member,ready,joined_at&party_id=eq." + pid + "&order=joined_at.asc", "GET");
        if (!mr.ok) return false;
        std::string inList;
        for (const auto& o : SplitObjects(mr.body)) {
            PartyMember m; m.uid = Field(o, "member"); m.ready = Field(o, "ready") == "true";
            m.joinedAt = ParseIso(Field(o, "joined_at"));
            if (m.uid.empty()) continue;
            if (!inList.empty()) inList += ",";
            inList += m.uid;
            out.members.push_back(m);
        }
        if (!inList.empty()) {
            auto p = acct_.Api("/rest/v1/profiles?select=id,username,display_name,avatar_url,last_seen,playing&id=in.(" + inList + ")", "GET");
            if (p.ok)
                for (const auto& o : SplitObjects(p.body)) {
                    const std::string id = Field(o, "id");
                    for (auto& m : out.members) if (m.uid == id) {
                        m.username = Field(o, "username"); m.display = Field(o, "display_name");
                        m.avatar = Field(o, "avatar_url"); m.playing = Field(o, "playing");
                        const long seen = ParseIso(Field(o, "last_seen"));
                        m.online = seen > 0 && std::difftime(std::time(nullptr), (std::time_t)seen) < 180.0;
                    }
                }
        }
        return true;
    }

    /// Start a party with me as the leader (leaves any party I'm in).
    bool Create(std::string* err = nullptr) { return Rpc("create_party", "{}", err); }
    /// Join a party by id (from an invite).
    bool Join(const std::string& partyId, std::string* err = nullptr) {
        if (partyId.empty()) { if (err) *err = "That invite isn't valid."; return false; }
        return Rpc("join_party", "{\"p_party\":" + JStr(partyId) + "}", err);
    }
    bool Leave(std::string* err = nullptr) { return Rpc("leave_party", "{}", err); }
    /// Leader only.
    bool Kick(const std::string& uid, std::string* err = nullptr) { return Rpc("kick_from_party", "{\"p_member\":" + JStr(uid) + "}", err); }
    bool Promote(const std::string& uid, std::string* err = nullptr) { return Rpc("promote_party_leader", "{\"p_member\":" + JStr(uid) + "}", err); }
    /// My ready flag.
    bool SetReady(const std::string& partyId, bool ready) {
        const std::string me = MyId();
        if (me.empty() || partyId.empty()) return false;
        return acct_.Api("/rest/v1/party_members?party_id=eq." + partyId + "&member=eq." + me, "PATCH",
                         std::string("{\"ready\":") + (ready ? "true" : "false") + "}").ok;
    }
    /// Leader only: the session members should follow ("" = not in a match).
    bool SetSession(const std::string& partyId, const std::string& game, const std::string& joinLink) {
        if (partyId.empty()) return false;
        return acct_.Api("/rest/v1/parties?id=eq." + partyId, "PATCH",
                         "{\"game\":" + JStr(game) + ",\"join_link\":" + JStr(joinLink) + "}").ok;
    }

    std::string MyId() {
        if (myId_.empty() && acct_.IsLoggedIn()) {
            std::string enc;
            for (char c : acct_.CurrentSession().username) {
                if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') enc += c;
                else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); enc += b; }
            }
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + enc, "GET");
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myId_ = Field(o[0], "id"); }
        }
        return myId_;
    }

    /// The chat message that invites someone: "[Party] <id>" + the inviter's name.
    static std::string InviteBody(const std::string& partyId, const std::string& from) {
        return "[Party] " + partyId + "\n" + from;
    }
    static bool ParseInvite(const std::string& body, std::string& partyId, std::string& from) {
        if (body.rfind("[Party] ", 0) != 0) return false;
        const std::size_t nl = body.find('\n');
        partyId = body.substr(8, nl == std::string::npos ? std::string::npos : nl - 8);
        from = nl == std::string::npos ? std::string() : body.substr(nl + 1);
        while (!partyId.empty() && (partyId.back() == ' ' || partyId.back() == '\r')) partyId.pop_back();
        return partyId.size() >= 32;
    }

private:
    okay::account::AccountService& acct_;
    std::string myId_;

    bool Rpc(const char* fn, const std::string& body, std::string* err) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return false; }
        auto r = acct_.Api(std::string("/rest/v1/rpc/") + fn, "POST", body);
        if (r.ok) return true;
        if (err) {
            std::string m = Field(r.body, "message");
            if (r.reached && r.status == 404) m = "Parties aren't set up on the server yet.";
            *err = m.empty() ? (r.reached ? "Something went wrong." : "Couldn't reach the server.") : m;
        }
        return false;
    }
    static long ParseIso(const std::string& ls) {
        if (ls.size() < 19) return 0;
        std::tm tm{};
        if (std::sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return 0;
        tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
        return (long)_mkgmtime(&tm);
#else
        return (long)timegm(&tm);
#endif
    }
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; std::size_t start = 0; bool inStr = false;
        for (std::size_t i = 0; i < json.size(); ++i) {
            const char c = json[i];
            if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true;
            else if (c == '{') { if (depth++ == 0) start = i; }
            else if (c == '}' && depth > 0 && --depth == 0) out.push_back(json.substr(start, i - start + 1));
        }
        return out;
    }
    static std::string Field(const std::string& obj, const std::string& key) {
        const std::string k = "\"" + key + "\"";
        std::size_t p = obj.find(k);
        if (p == std::string::npos) return {};
        p = obj.find(':', p + k.size());
        if (p == std::string::npos) return {};
        ++p; while (p < obj.size() && obj[p] == ' ') ++p;
        std::string v;
        if (p < obj.size() && obj[p] == '"') {
            for (++p; p < obj.size() && obj[p] != '"'; ++p) {
                if (obj[p] == '\\' && p + 1 < obj.size()) { ++p; v += obj[p] == 'n' ? '\n' : obj[p]; continue; }
                v += obj[p];
            }
            return v;
        }
        while (p < obj.size() && obj[p] != ',' && obj[p] != '}') v += obj[p++];
        while (!v.empty() && v.back() == ' ') v.pop_back();
        return v == "null" ? std::string() : v;
    }
    static std::string JStr(const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n"; else if (c != '\r') o += c;
        }
        return o + "\"";
    }
};

} // namespace okay
