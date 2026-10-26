#pragma once
// Anti-cheat backend calls (docs/sql/anticheat.sql, docs/anticheat.md): players
// file reports, see their own anti-cheat bans and appeal them; admins review
// reports, ban / unban and decide appeals (launcher: Admin > Anti-cheat).
//
// Each call is built as a Request (method, PostgREST path, JSON body, extra
// header) and each answer parsed by a plain function, so both are unit tested
// (tests/test_anticheat_backend.cpp) without a server. Send() hands a Request to
// AccountService::Api, which signs it with the player's session. Every rule
// (who may read or change what) is enforced by RLS on the server; the checks
// here only stop malformed input before it's sent.
#include <map>

#include "AntiCheatIntegrity.hpp"   // og::json / CleanText / Trim, okay::account

namespace okayac {
namespace backend {

struct Request {
    std::string method = "GET";
    std::string path;     // after the server URL: "/rest/v1/..."
    std::string body;     // JSON ("" = none)
    std::string header;   // one extra header line ("" = none)
};

inline okay::account::ApiResponse Send(okay::account::AccountService& acct, const Request& r) {
    return acct.Api(r.path, r.method, r.body, r.header);
}

// ---- Values ------------------------------------------------------------------
inline bool IsUuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!std::isxdigit((unsigned char)c)) return false;
    }
    return true;
}
inline bool IsHwHash(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

// Free text for the server: trimmed, control characters dropped, capped.
inline std::string Text(const std::string& s, std::size_t cap, bool keepNewlines = false) {
    return og::Trim(og::CleanText(s, cap, keepNewlines));
}
inline std::string JStr(const std::string& s) { return "\"" + og::json::Escape(s) + "\""; }
inline std::string JStrOrNull(const std::string& s) { return s.empty() ? std::string("null") : JStr(s); }

// Days since 1970-01-01 for a civil date, and back (Howard Hinnant's algorithms):
// UTC without timegm / _mkgmtime, the same on every platform.
inline long DaysFromCivil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
inline std::string IsoTime(long t) {
    long days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    long secs = t - days * 86400;
    days += 719468;
    const long era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    char b[64];
    std::snprintf(b, sizeof(b), "%04ld-%02u-%02uT%02ld:%02ld:%02ldZ", y, m, d, secs / 3600, (secs / 60) % 60, secs % 60);
    return b;
}
// "2026-10-25T12:00:00.123456+00:00" (PostgREST), "...Z", "... 12:00:00": seconds
// since the epoch, 0 when it isn't a time.
inline long ParseTime(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    char sep = 0;
    if (s.size() < 19 || std::sscanf(s.c_str(), "%4d-%2d-%2d%c%2d:%2d:%2d", &y, &mo, &d, &sep, &h, &mi, &se) != 7) return 0;
    if ((sep != 'T' && sep != ' ') || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return 0;
    long t = DaysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
    std::size_t i = 19;
    if (i < s.size() && s[i] == '.') while (++i < s.size() && std::isdigit((unsigned char)s[i])) {}
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        int oh = 0, om = 0;
        if (std::sscanf(s.c_str() + i + 1, "%2d:%2d", &oh, &om) >= 1) t -= (s[i] == '+' ? 1 : -1) * (oh * 3600L + om * 60L);
    }
    return t;
}

// The ban lengths the Admin page offers: 0 permanent, 1 a day, 2 a week, 3 30 days, 4 a year.
inline long ExpiryFor(int choice, long now) {
    static const long kDays[] = {0, 1, 7, 30, 365};
    if (choice <= 0 || choice > 4) return 0;
    return now + kDays[choice] * 86400L;
}

inline bool ValidStatus(const std::string& s) {
    return s == "open" || s == "reviewing" || s == "actioned" || s == "dismissed";
}

namespace detail {
inline bool Fail(std::string* err, const char* why) { if (err) *err = why; return false; }
inline std::string InList(const std::vector<std::string>& ids) {
    std::string s;
    for (const std::string& id : ids) if (IsUuid(id)) s += (s.empty() ? "" : ",") + id;
    return s;
}
} // namespace detail

// ---- Player calls --------------------------------------------------------------
// Report another player. The server fills in the reporter (auth.uid()) and the status.
inline bool FileReport(const std::string& reportedUid, const std::string& game, const std::string& reason,
                       const std::string& details, const std::string& hwHash, Request& out, std::string* err = nullptr) {
    const std::string why = Text(reason, 500);
    if (!IsUuid(reportedUid)) return detail::Fail(err, "Pick the player to report.");
    if (why.empty()) return detail::Fail(err, "Say what happened.");
    if (!hwHash.empty() && !IsHwHash(hwHash)) return detail::Fail(err, "Bad hardware hash.");
    out = Request{};
    out.method = "POST";
    out.path = "/rest/v1/ac_reports";
    out.header = "Prefer: return=minimal";
    out.body = "{\"reported\":" + JStr(reportedUid) + ",\"kind\":\"player\",\"game\":" + JStrOrNull(Text(game, 120)) +
               ",\"reason\":" + JStr(why) + ",\"details\":" + JStrOrNull(Text(details, 4000, true)) +
               ",\"hw_hash\":" + JStrOrNull(hwHash) + "}";
    return true;
}

// What leaves the machine in a client_check report: every absolute path cut
// down to its file name. A module path ("C:\Users\<name>\AppData\...\x.dll",
// "/home/<name>/...") would otherwise send the player's OS user name. Paths
// relative to the game folder ("Data/x.dll") say nothing about the player and
// stay. The local anticheat.log keeps the full paths.
inline std::string RedactPaths(const std::string& text) {
    std::string out, line;
    auto absStart = [](const std::string& l) -> std::size_t {
        for (std::size_t i = 0; i < l.size(); ++i) {
            const bool edge = i == 0 || l[i - 1] == ' ' || l[i - 1] == '\t' || l[i - 1] == '"' || l[i - 1] == '\'' || l[i - 1] == '(';
            if (!edge) continue;
            if (i + 2 < l.size() && std::isalpha((unsigned char)l[i]) && l[i + 1] == ':' && (l[i + 2] == '\\' || l[i + 2] == '/')) return i;
            if (l[i] == '\\' && i + 1 < l.size() && l[i + 1] == '\\') return i;   // \\server\share, \\?\C:\...
            const char next = i + 1 < l.size() ? l[i + 1] : ' ';
            if ((l[i] == '/' && next != ' ') || (l[i] == '~' && next == '/')) return i;   // /home/..., ~/...
        }
        return std::string::npos;
    };
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        // A path runs to the end of its line (module and file names can hold spaces).
        if (const std::size_t a = absStart(line); a != std::string::npos) {
            const std::size_t slash = line.find_last_of("/\\");
            line = line.substr(0, a) + (slash != std::string::npos && slash >= a ? line.substr(slash + 1) : std::string());
        }
        out += line;
        if (nl == std::string::npos) break;
        out += '\n';
        pos = nl + 1;
    }
    return out;
}

// What the player's own anti-cheat found (P3's runtime checks / integrity), sent
// with this machine's hardware hash ("" when it has none). Paths are cut down
// to file names first (RedactPaths).
inline bool FileClientCheck(const std::string& game, const std::string& check, const std::string& detailInput,
                            const std::string& hwHash, Request& out) {
    const std::string why = Text(check, 500);
    if (why.empty() || (!hwHash.empty() && !IsHwHash(hwHash))) return false;
    const std::string detailText = RedactPaths(detailInput);
    out = Request{};
    out.method = "POST";
    out.path = "/rest/v1/ac_reports";
    out.header = "Prefer: return=minimal";
    out.body = "{\"kind\":\"client_check\",\"game\":" + JStrOrNull(Text(game, 120)) + ",\"reason\":" + JStr(why) +
               ",\"details\":" + JStrOrNull(Text(detailText, 4000, true)) + ",\"hw_hash\":" + JStrOrNull(hwHash) + "}";
    return true;
}

// My anti-cheat bans (by account, or by this machine's hash), with each one's latest appeal.
inline Request MyBans(const std::string& hwHash) {
    Request r;
    r.method = "POST";
    r.path = "/rest/v1/rpc/ac_my_bans";
    r.body = "{\"p_hw_hash\":" + (IsHwHash(hwHash) ? JStr(hwHash) : std::string("null")) + "}";
    return r;
}

inline bool FileAppeal(long banId, const std::string& message, const std::string& hwHash, Request& out, std::string* err = nullptr) {
    const std::string msg = Text(message, 2000, true);
    if (banId <= 0) return detail::Fail(err, "Pick the ban to appeal.");
    if (msg.empty()) return detail::Fail(err, "Write why the ban should be lifted.");
    out = Request{};
    out.method = "POST";
    out.path = "/rest/v1/rpc/ac_file_appeal";
    out.body = "{\"p_ban\":" + std::to_string(banId) + ",\"p_message\":" + JStr(msg) +
               ",\"p_hw_hash\":" + (IsHwHash(hwHash) ? JStr(hwHash) : std::string("null")) + "}";
    return true;
}

// ---- Admin calls ---------------------------------------------------------------
inline Request ListReports(const std::string& status, int limit = 100) {
    limit = std::max(1, std::min(200, limit));
    Request r;
    r.path = "/rest/v1/ac_reports?select=id,reporter,reported,kind,game,reason,details,hw_hash,status,review_note,created_at"
             "&order=created_at.desc&limit=" + std::to_string(limit);
    if (ValidStatus(status)) r.path += "&status=eq." + status;
    return r;
}

inline bool SetReportStatus(long id, const std::string& status, const std::string& note, Request& out, std::string* err = nullptr) {
    if (id <= 0) return detail::Fail(err, "No report picked.");
    if (!ValidStatus(status)) return detail::Fail(err, "Unknown report status.");
    out = Request{};
    out.method = "PATCH";
    out.path = "/rest/v1/ac_reports?id=eq." + std::to_string(id);
    out.header = "Prefer: return=minimal";
    out.body = "{\"status\":" + JStr(status) + ",\"review_note\":" + JStrOrNull(Text(note, 500)) + "}";
    return true;
}

// Ban an account and / or a machine. `until` 0 = permanent; `reportId` 0 = none.
inline bool BanPlayer(const std::string& uid, const std::string& hwHash, const std::string& reason, long until, long reportId,
                      long now, Request& out, std::string* err = nullptr) {
    const std::string why = Text(reason, 300);
    if (!uid.empty() && !IsUuid(uid)) return detail::Fail(err, "That isn't a player id.");
    if (!hwHash.empty() && !IsHwHash(hwHash)) return detail::Fail(err, "That isn't a hardware hash.");
    if (uid.empty() && hwHash.empty()) return detail::Fail(err, "Pick a player (or a machine) to ban.");
    if (why.empty()) return detail::Fail(err, "Give a reason: the player sees it.");
    if (until != 0 && until <= now) return detail::Fail(err, "The ban would already be over.");
    out = Request{};
    out.method = "POST";
    out.path = "/rest/v1/ac_bans";
    out.header = "Prefer: return=minimal";
    out.body = "{\"user_id\":" + JStrOrNull(uid) + ",\"hw_hash\":" + JStrOrNull(hwHash) + ",\"reason\":" + JStr(why) +
               ",\"until\":" + (until ? JStr(IsoTime(until)) : std::string("null")) +
               ",\"report_id\":" + (reportId > 0 ? std::to_string(reportId) : std::string("null")) + "}";
    return true;
}

// Lift a ban (it stays on record; the server stamps who and when).
inline bool Unban(long banId, const std::string& why, Request& out, std::string* err = nullptr) {
    if (banId <= 0) return detail::Fail(err, "No ban picked.");
    out = Request{};
    out.method = "PATCH";
    out.path = "/rest/v1/ac_bans?id=eq." + std::to_string(banId);
    out.header = "Prefer: return=minimal";
    out.body = "{\"lifted\":true,\"lift_reason\":" + JStrOrNull(Text(why, 300)) + "}";
    return true;
}

inline Request ListBans(bool activeOnly, long now) {
    Request r;
    r.path = "/rest/v1/ac_bans?select=id,user_id,hw_hash,reason,until,report_id,created_at,lifted,lifted_at,lift_reason"
             "&order=created_at.desc&limit=200";
    if (activeOnly) r.path += "&lifted=is.false&or=(until.is.null,until.gt." + IsoTime(now) + ")";
    return r;
}

inline Request ListAppeals(const std::string& status) {
    Request r;
    r.path = "/rest/v1/ac_appeals?select=id,ban_id,user_id,message,status,decision_note,created_at,decided_at"
             "&order=created_at.desc&limit=200";
    if (status == "open" || status == "accepted" || status == "rejected") r.path += "&status=eq." + status;
    return r;
}

// Accepting an appeal lifts its ban (a trigger on the server).
inline bool DecideAppeal(long id, bool accept, const std::string& note, Request& out, std::string* err = nullptr) {
    if (id <= 0) return detail::Fail(err, "No appeal picked.");
    out = Request{};
    out.method = "PATCH";
    out.path = "/rest/v1/ac_appeals?id=eq." + std::to_string(id);
    out.header = "Prefer: return=minimal";
    out.body = std::string("{\"status\":\"") + (accept ? "accepted" : "rejected") + "\",\"decision_note\":" + JStrOrNull(Text(note, 500)) + "}";
    return true;
}

// A player by exact username (Admin > Anti-cheat > "Ban a player").
inline Request UserByName(const std::string& username) {
    Request r;
    r.path = "/rest/v1/profiles?select=id,username&limit=1&username=eq." + og::UrlEncodePath(Text(username, 64));
    return r;
}

inline Request Usernames(const std::vector<std::string>& ids) {
    Request r;
    r.path = "/rest/v1/profiles?select=id,username&id=in.(" + detail::InList(ids) + ")";
    return r;
}

// A session ticket for one game server (supabase/functions/session-ticket). The
// machine hash is optional; with it the service also refuses a banned machine.
inline Request SessionTicket(const std::string& serverId, const std::string& hwHash) {
    Request r;
    r.method = "POST";
    r.path = "/functions/v1/session-ticket";
    r.body = "{\"aud\":" + JStr(serverId) + (IsHwHash(hwHash) ? ",\"hw_hash\":" + JStr(hwHash) : std::string()) + "}";
    return r;
}

// ---- Rows ----------------------------------------------------------------------
struct Report {
    long id = 0;
    std::string reporter, reported;   // uuids ("" = none: a client check)
    std::string kind, game, reason, details, hwHash, status, reviewNote;
    long created = 0;
};
struct Ban {
    long id = 0, reportId = 0;
    std::string userId, hwHash, reason, liftReason;
    long until = 0, created = 0, lifted = 0;   // until 0 = permanent; lifted 0 = not lifted
    bool liftedFlag = false;
    bool Active(long now) const { return !liftedFlag && lifted == 0 && (until == 0 || until > now); }
};
struct Appeal {
    long id = 0, banId = 0;
    std::string userId, message, status, decisionNote;
    long created = 0, decided = 0;
};
// One of my bans (rpc ac_my_bans): what I'm told, and where my appeal stands.
struct MyBan {
    long id = 0, until = 0, created = 0;
    std::string reason;
    std::string appealStatus, appealNote;   // "" = never appealed
    int appealsLeft = 0;
    bool CanAppeal() const { return appealsLeft > 0 && appealStatus != "open"; }
};

namespace detail {
using og::json::Value;
inline std::string S(const Value& o, const char* k) {
    const Value* v = o.get(k);
    if (!v) return {};
    if (v->kind == Value::String || v->kind == Value::Number) return v->str;
    return {};
}
inline long N(const Value& o, const char* k) {
    const Value* v = o.get(k);
    if (!v || v->kind != Value::Number || v->str.empty() || v->str.size() > 18) return 0;
    return std::atol(v->str.c_str());
}
inline bool B(const Value& o, const char* k) {
    const Value* v = o.get(k);
    return v && v->kind == Value::Bool && v->b;
}
// The rows of a PostgREST answer; false for anything that isn't an array (an error object).
inline bool Rows(const std::string& body, Value& root) {
    return body.size() <= og::kMaxManifestBytes && og::json::Reader(body).Parse(root) && root.kind == Value::Array;
}
} // namespace detail

inline bool ParseReports(const std::string& body, std::vector<Report>& out) {
    detail::Value root;
    if (!detail::Rows(body, root)) return false;
    std::vector<Report> v;
    for (const auto& o : root.arr) {
        if (o.kind != detail::Value::Object) continue;
        Report r;
        r.id = detail::N(o, "id");
        r.reporter = detail::S(o, "reporter"); r.reported = detail::S(o, "reported");
        r.kind = detail::S(o, "kind"); r.game = detail::S(o, "game"); r.reason = detail::S(o, "reason");
        r.details = detail::S(o, "details"); r.hwHash = detail::S(o, "hw_hash"); r.status = detail::S(o, "status");
        r.reviewNote = detail::S(o, "review_note"); r.created = ParseTime(detail::S(o, "created_at"));
        if (r.id > 0) v.push_back(std::move(r));
    }
    out = std::move(v);
    return true;
}

inline bool ParseBans(const std::string& body, std::vector<Ban>& out) {
    detail::Value root;
    if (!detail::Rows(body, root)) return false;
    std::vector<Ban> v;
    for (const auto& o : root.arr) {
        if (o.kind != detail::Value::Object) continue;
        Ban b;
        b.id = detail::N(o, "id"); b.reportId = detail::N(o, "report_id");
        b.userId = detail::S(o, "user_id"); b.hwHash = detail::S(o, "hw_hash"); b.reason = detail::S(o, "reason");
        b.liftReason = detail::S(o, "lift_reason");
        b.until = ParseTime(detail::S(o, "until")); b.created = ParseTime(detail::S(o, "created_at"));
        b.lifted = ParseTime(detail::S(o, "lifted_at")); b.liftedFlag = detail::B(o, "lifted");
        if (b.id > 0) v.push_back(std::move(b));
    }
    out = std::move(v);
    return true;
}

inline bool ParseAppeals(const std::string& body, std::vector<Appeal>& out) {
    detail::Value root;
    if (!detail::Rows(body, root)) return false;
    std::vector<Appeal> v;
    for (const auto& o : root.arr) {
        if (o.kind != detail::Value::Object) continue;
        Appeal a;
        a.id = detail::N(o, "id"); a.banId = detail::N(o, "ban_id");
        a.userId = detail::S(o, "user_id"); a.message = detail::S(o, "message"); a.status = detail::S(o, "status");
        a.decisionNote = detail::S(o, "decision_note");
        a.created = ParseTime(detail::S(o, "created_at")); a.decided = ParseTime(detail::S(o, "decided_at"));
        if (a.id > 0) v.push_back(std::move(a));
    }
    out = std::move(v);
    return true;
}

inline bool ParseMyBans(const std::string& body, std::vector<MyBan>& out) {
    detail::Value root;
    if (!detail::Rows(body, root)) return false;
    std::vector<MyBan> v;
    for (const auto& o : root.arr) {
        if (o.kind != detail::Value::Object) continue;
        MyBan b;
        b.id = detail::N(o, "id"); b.reason = detail::S(o, "reason");
        b.until = ParseTime(detail::S(o, "until")); b.created = ParseTime(detail::S(o, "created_at"));
        b.appealStatus = detail::S(o, "appeal_status"); b.appealNote = detail::S(o, "appeal_note");
        b.appealsLeft = (int)detail::N(o, "appeals_left");
        if (b.id > 0) v.push_back(std::move(b));
    }
    out = std::move(v);
    return true;
}

inline bool ParseUsernames(const std::string& body, std::map<std::string, std::string>& out) {
    detail::Value root;
    if (!detail::Rows(body, root)) return false;
    for (const auto& o : root.arr)
        if (o.kind == detail::Value::Object) {
            const std::string id = detail::S(o, "id"), name = detail::S(o, "username");
            if (!id.empty()) out[id] = name;
        }
    return true;
}

// The ticket service's answer: a ticket, or banned (403 {"error":"banned"}: the
// caller refreshes ac_my_bans so the ban banner shows), or neither (an error).
struct TicketReply { std::string ticket; long exp = 0; bool banned = false; };
inline bool IsBannedReply(long status, const std::string& body) {
    detail::Value root;
    return status == 403 && body.size() < 65536 && og::json::Reader(body).Parse(root) &&
           root.kind == detail::Value::Object && detail::S(root, "error") == "banned";
}
inline bool ParseTicket(long status, const std::string& body, TicketReply& out) {
    out = TicketReply();
    if (IsBannedReply(status, body)) { out.banned = true; return false; }
    detail::Value root;
    if (status != 200 || body.size() >= 65536 || !og::json::Reader(body).Parse(root) || root.kind != detail::Value::Object) return false;
    out.ticket = detail::S(root, "ticket");
    out.exp = detail::N(root, "exp");
    if (out.ticket.rfind("okt1.", 0) != 0) { out.ticket.clear(); return false; }
    return true;
}

// A failed call, in words for a toast.
inline std::string ErrorText(long status, const std::string& body) {
    if (status == 0) return "Couldn't reach the server.";
    if (IsBannedReply(status, body)) return "You can't join: this account or machine has an anti-cheat ban.";
    std::string msg;
    detail::Value root;
    if (body.size() < 65536 && og::json::Reader(body).Parse(root) && root.kind == detail::Value::Object) msg = detail::S(root, "message");
    const std::string low = og::Lower(msg);
    if (low.find("does not exist") != std::string::npos || low.find("could not find") != std::string::npos)
        return "Anti-cheat isn't set up on this server yet (run docs/sql/anticheat.sql).";
    if (status == 401 || status == 403 || low.find("row-level security") != std::string::npos || low.find("permission denied") != std::string::npos)
        return "You don't have permission to do that.";
    if (!msg.empty()) return og::CleanText(msg, 200, false);
    return "The server refused (HTTP " + std::to_string(status) + ").";
}

} // namespace backend
} // namespace okayac
