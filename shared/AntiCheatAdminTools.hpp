#pragma once
// Admin > Anti-cheat: the two device-based helpers of docs/sql/anticheat.sql.
//   ac_linked_accounts(p_user)             other accounts that used a machine this account used
//   ac_ban_with_devices(p_user, ...)       ban an account and every machine it used, in one step
// Requests are built and answers parsed here, as plain functions, so they are unit
// tested (tests/test_anticheat_admin_tools.cpp) without a server. The launcher only
// sends them (acb::Send). Who may call them is decided by the server (admins only).
#include "AntiCheatBackend.hpp"

namespace okayac {
namespace backend {

// Another account that shared a machine with the one looked up. A lead, not proof:
// machines can be shared (family PCs, internet cafes).
struct LinkedAccount {
    std::string userId;
    int sharedMachines = 0;
    long lastSeen = 0;
    bool banned = false;   // has an active ban of its own
};

inline bool LinkedAccounts(const std::string& uid, Request& out, std::string* err = nullptr) {
    if (!IsUuid(uid)) return detail::Fail(err, "Pick an account to look up.");
    out = Request{};
    out.method = "POST";
    out.path = "/rest/v1/rpc/ac_linked_accounts";
    out.body = "{\"p_user\":" + JStr(uid) + "}";
    return true;
}

// The rows of ac_linked_accounts. False for anything that isn't a row list (an error object).
inline bool ParseLinkedAccounts(const std::string& body, std::vector<LinkedAccount>& out) {
    detail::Value root;
    if (!detail::Rows(body, root)) return false;
    std::vector<LinkedAccount> v;
    for (const auto& o : root.arr) {
        if (o.kind != detail::Value::Object) continue;
        LinkedAccount a;
        a.userId = detail::S(o, "other_user");
        if (!IsUuid(a.userId)) continue;
        a.sharedMachines = (int)detail::N(o, "shared_machines");
        a.lastSeen = ParseTime(detail::S(o, "last_seen"));
        a.banned = detail::B(o, "other_banned");
        v.push_back(std::move(a));
    }
    out = std::move(v);
    return true;
}

// "2 shared machines, last seen ..., banned" for the list line (the launcher adds the time).
inline std::string LinkedSummary(const LinkedAccount& a) {
    return std::to_string(a.sharedMachines) + (a.sharedMachines == 1 ? " shared machine" : " shared machines") +
           (a.banned ? ", already banned" : "");
}

// Ban the account and every machine it used. `until` 0 = permanent; `reportId` 0 = none.
inline bool BanWithDevices(const std::string& uid, const std::string& reason, long until, long reportId, long now,
                           Request& out, std::string* err = nullptr) {
    const std::string why = Text(reason, 300);
    if (!IsUuid(uid)) return detail::Fail(err, "That isn't a player id.");
    if (why.empty()) return detail::Fail(err, "Give a reason: the player sees it.");
    if (until != 0 && until <= now) return detail::Fail(err, "The ban would already be over.");
    out = Request{};
    out.method = "POST";
    out.path = "/rest/v1/rpc/ac_ban_with_devices";
    out.body = "{\"p_user\":" + JStr(uid) + ",\"p_reason\":" + JStr(why) +
               ",\"p_until\":" + (until ? JStr(IsoTime(until)) : std::string("null")) +
               ",\"p_report\":" + (reportId > 0 ? std::to_string(reportId) : std::string("null")) + "}";
    return true;
}

// ac_ban_with_devices answers with a bare number: how many ban rows it made (the account's, plus one per machine).
inline bool ParseBanCount(const std::string& body, int& out) {
    detail::Value root;
    if (body.size() > 64 || !og::json::Reader(body).Parse(root) || root.kind != detail::Value::Number) return false;
    const long n = std::atol(root.str.c_str());
    if (n < 0 || n > 1000) return false;
    out = (int)n;
    return true;
}

// Evidence bundle of one report (admins only; the list request leaves it out to stay small).
inline bool ReportEvidence(long reportId, Request& out, std::string* err = nullptr) {
    if (reportId <= 0) return detail::Fail(err, "No report picked.");
    out = Request{};
    out.path = "/rest/v1/ac_reports?select=evidence&id=eq." + std::to_string(reportId) + "&limit=1";
    return true;
}
// The bundle's JSON text out of that answer ("" = none attached, or not an array / not an object).
inline std::string ParseReportEvidence(const std::string& body) {
    detail::Value root;
    if (!detail::Rows(body, root) || root.arr.empty() || root.arr[0].kind != detail::Value::Object) return {};
    const detail::Value* ev = root.arr[0].get("evidence");
    if (!ev || ev->kind != detail::Value::Object) return {};
    // The row came back through PostgREST; the bundle is re-read from the raw text so it is shown as the server stored it.
    const std::size_t at = body.find("\"evidence\"");
    if (at == std::string::npos) return {};
    const std::size_t brace = body.find('{', at);
    if (brace == std::string::npos) return {};
    int depth = 0; bool inStr = false;
    for (std::size_t i = brace; i < body.size(); ++i) {
        const char c = body[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return body.substr(brace, i - brace + 1);
    }
    return {};
}

} // namespace backend
} // namespace okayac
