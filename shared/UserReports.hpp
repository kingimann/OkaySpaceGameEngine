#pragma once
// User reports (docs/sql/reports.sql): report a player, a chat message, a forum
// post or comment, a marketplace listing or a community server; moderators list
// them and mark them resolved or dismissed (launcher: Admin > Reports).
//
// Like AntiCheatBackend.hpp every call is built as a Request and every answer is
// parsed by a plain function, so it is unit tested (tests/test_user_reports.cpp)
// without a server. The rules (who may file, read or review what, duplicates,
// self-reports, flood limits) live in the SQL; the checks here only stop
// malformed input and turn the server's answer into an honest sentence.
#include "AntiCheatBackend.hpp"

namespace okayrep {

using okayac::backend::Request;

enum class Target { User, Message, GroupMessage, ForumThread, ForumPost, MarketItem, Server };

inline const char* TargetKey(Target t) {
    switch (t) {
        case Target::User: return "user";
        case Target::Message: return "message";
        case Target::GroupMessage: return "group_message";
        case Target::ForumThread: return "forum_thread";
        case Target::ForumPost: return "forum_post";
        case Target::MarketItem: return "market_item";
        case Target::Server: return "server";
    }
    return "";
}
// What the person sees ("Report this ...").
inline const char* TargetLabel(const std::string& key) {
    if (key == "user") return "player";
    if (key == "message") return "message";
    if (key == "group_message") return "group message";
    if (key == "forum_thread") return "forum post";
    if (key == "forum_post") return "forum comment";
    if (key == "market_item") return "marketplace listing";
    if (key == "server") return "server";
    return "item";
}

struct Reason { const char* key; const char* label; };
// The fixed list (the same keys as reports_reason_check in the SQL).
inline const Reason* Reasons(int& count) {
    static const Reason k[] = {
        {"spam", "Spam or advertising"}, {"harassment", "Harassment or bullying"}, {"hate", "Hate speech"},
        {"sexual", "Sexual or adult content"}, {"violence", "Violence or threats"}, {"scam", "Scam or phishing"},
        {"illegal", "Illegal content"}, {"impersonation", "Pretending to be someone else"}, {"other", "Something else"}};
    count = (int)(sizeof(k) / sizeof(k[0]));
    return k;
}
inline bool ValidReason(const std::string& key) {
    int n = 0; const Reason* r = Reasons(n);
    for (int i = 0; i < n; ++i) if (key == r[i].key) return true;
    return false;
}
constexpr std::size_t kNoteMax = 500;

inline bool ValidStatus(const std::string& s) { return s == "open" || s == "resolved" || s == "dismissed"; }

// A target id: a uuid for a player, a positive number for everything else.
inline bool ValidTargetId(Target t, const std::string& id) {
    if (t == Target::User) return okayac::backend::IsUuid(id);
    if (id.empty() || id.size() > 18) return false;
    for (char c : id) if (c < '0' || c > '9') return false;
    return id.find_first_not_of('0') != std::string::npos;
}

// The report the person is sending. `myUid` (may be "") only lets the client say
// "you can't report yourself" before a round trip; the server decides anyway.
inline bool Build(Target t, const std::string& id, const std::string& reason, const std::string& note,
                  const std::string& myUid, Request& out, std::string* err = nullptr) {
    using okayac::backend::detail::Fail;
    if (!ValidTargetId(t, id)) return Fail(err, "Nothing to report here.");
    if (!ValidReason(reason)) return Fail(err, "Pick a reason.");
    if (t == Target::User && !myUid.empty() && okayofficial::Lower(id) == okayofficial::Lower(myUid))
        return Fail(err, "You can't report yourself.");
    const std::string n = okayac::backend::Text(note, kNoteMax, true);
    out = Request{};
    out.method = "POST";
    out.path = "/rest/v1/reports";
    out.header = "Prefer: return=minimal";
    out.body = std::string("{\"target_type\":\"") + TargetKey(t) + "\",\"target_id\":" + okayac::backend::JStr(id) +
               ",\"reason\":" + okayac::backend::JStr(reason) + ",\"note\":" + okayac::backend::JStrOrNull(n) + "}";
    return true;
}

enum class Outcome { Sent, SignedOut, NotSetUp, Duplicate, RateLimited, Self, TargetGone, Denied, NoNetwork, Other };

// Classify the server's answer to a Build()'d insert (201 / 204 = accepted).
inline Outcome Classify(long status, const std::string& body) {
    if (status == 201 || status == 204 || status == 200) return Outcome::Sent;
    if (status == 0) return Outcome::NoNetwork;
    const std::string low = okayofficial::Lower(body.size() < 65536 ? body : body.substr(0, 65536));
    auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
    if (has("report_duplicate")) return Outcome::Duplicate;
    if (has("report_rate_limit")) return Outcome::RateLimited;
    if (has("report_self")) return Outcome::Self;
    if (has("report_target_missing")) return Outcome::TargetGone;
    // PostgREST: 404 + PGRST205 ("Could not find the table 'public.reports' in the schema cache"),
    // or Postgres 42P01 ("relation ... does not exist").
    if (has("pgrst205") || has("42p01") || (status == 404 && (has("could not find the table") || has("does not exist")))) return Outcome::NotSetUp;
    if (status == 401 || has("jwt expired") || has("invalid jwt") || has("pgrst301")) return Outcome::SignedOut;
    if (status == 403 || has("row-level security") || has("permission denied")) return Outcome::Denied;
    return Outcome::Other;
}

inline bool Accepted(Outcome o) { return o == Outcome::Sent; }

// The sentence for a toast / the dialog. "Report sent" is only ever the Sent text.
inline std::string Message(Outcome o) {
    switch (o) {
        case Outcome::Sent: return "Report sent. Thanks, a moderator will look at it.";
        case Outcome::SignedOut: return "Sign in to report. Your report was not sent.";
        case Outcome::NotSetUp: return "Reporting isn't set up on this server yet (the owner needs to run docs/sql/reports.sql). Your report was not sent.";
        case Outcome::Duplicate: return "You already reported this recently. Your report was not sent again.";
        case Outcome::RateLimited: return "You've sent a lot of reports in the last hour. Try again later. Your report was not sent.";
        case Outcome::Self: return "You can't report yourself or your own content. Nothing was sent.";
        case Outcome::TargetGone: return "That no longer exists, or you can't see it. Nothing was sent.";
        case Outcome::Denied: return "The server refused the report (permission). Nothing was sent.";
        case Outcome::NoNetwork: return "Couldn't reach the server. Your report was not sent.";
        case Outcome::Other: return "The server couldn't take the report. Nothing was sent.";
    }
    return "";
}
// A Classify()'d answer in one step.
inline std::string MessageFor(long status, const std::string& body) { return Message(Classify(status, body)); }

// ---- Moderator side ---------------------------------------------------------
// A failed list / review call, in words for the Admin > Reports page.
inline std::string AdminError(long status, const std::string& body) {
    switch (Classify(status, body)) {
        case Outcome::NotSetUp: return "Reports aren't set up on this server yet (run docs/sql/reports.sql in the Supabase SQL editor).";
        case Outcome::SignedOut: return "You're signed out. Sign in again to review reports.";
        case Outcome::Denied: return "You don't have permission to review reports (moderator role needed).";
        case Outcome::NoNetwork: return "Couldn't reach the server.";
        default: break;
    }
    return "The server refused (HTTP " + std::to_string(status) + ").";
}
struct Row {
    long id = 0;
    std::string reporter, targetType, targetId, targetUser, reason, note, snapshot, status, reviewNote;
    long created = 0;
};

// status "" = all. Most recent first.
inline Request List(const std::string& status, int limit = 200) {
    limit = std::max(1, std::min(200, limit));
    Request r;
    r.path = "/rest/v1/reports?select=id,reporter,target_type,target_id,target_user,reason,note,snapshot,status,review_note,created_at"
             "&order=created_at.desc&limit=" + std::to_string(limit);
    if (ValidStatus(status)) r.path += "&status=eq." + status;
    return r;
}

inline bool SetStatus(long id, const std::string& status, const std::string& note, Request& out, std::string* err = nullptr) {
    using okayac::backend::detail::Fail;
    if (id <= 0) return Fail(err, "No report picked.");
    if (!ValidStatus(status)) return Fail(err, "Unknown report status.");
    out = Request{};
    out.method = "PATCH";
    out.path = "/rest/v1/reports?id=eq." + std::to_string(id);
    out.header = "Prefer: return=minimal";
    out.body = "{\"status\":" + okayac::backend::JStr(status) + ",\"review_note\":" +
               okayac::backend::JStrOrNull(okayac::backend::Text(note, 500)) + "}";
    return true;
}

inline bool ParseRows(const std::string& body, std::vector<Row>& out) {
    namespace d = okayac::backend::detail;
    d::Value root;
    if (!d::Rows(body, root)) return false;
    std::vector<Row> v;
    for (const auto& o : root.arr) {
        if (o.kind != d::Value::Object) continue;
        Row r;
        r.id = d::N(o, "id");
        r.reporter = d::S(o, "reporter"); r.targetType = d::S(o, "target_type"); r.targetId = d::S(o, "target_id");
        r.targetUser = d::S(o, "target_user"); r.reason = d::S(o, "reason"); r.note = d::S(o, "note");
        r.snapshot = d::S(o, "snapshot"); r.status = d::S(o, "status"); r.reviewNote = d::S(o, "review_note");
        r.created = okayac::backend::ParseTime(d::S(o, "created_at"));
        if (r.id > 0) v.push_back(std::move(r));
    }
    out = std::move(v);
    return true;
}

// The reason key as words ("harassment" -> "Harassment or bullying").
inline std::string ReasonLabel(const std::string& key) {
    int n = 0; const Reason* r = Reasons(n);
    for (int i = 0; i < n; ++i) if (key == r[i].key) return r[i].label;
    return key;
}

} // namespace okayrep
