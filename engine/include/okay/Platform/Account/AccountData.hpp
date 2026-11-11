#pragma once
// "Download my data" and "Delete my account" for the launcher (Supabase accounts).
// Pure logic, no UI, no threads: the launcher runs these on a worker thread.
// What is exported / deleted / anonymised is documented in docs/account_data.md;
// the server half of deletion is supabase/functions/delete-account.
#include "AccountService.hpp"

#include <cctype>
#include <cstddef>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace okay::account::data {

// ------------------------------------------------------------------ export --

/// One PostgREST read. Every query filters on the signed-in user's id and goes
/// through the user's own token, so row-level security decides what comes back:
/// only rows the user may legitimately read are ever exported.
struct ExportSection { std::string key; std::string path; };

inline std::vector<ExportSection> ExportSections(const std::string& uid) {
    const std::string u = uid;
    auto eq = [&](const char* table, const char* col) { return std::string("/rest/v1/") + table + "?select=*&" + col + "=eq." + u; };
    auto either = [&](const char* table, const char* a, const char* b) {
        return std::string("/rest/v1/") + table + "?select=*&or=(" + a + ".eq." + u + "," + b + ".eq." + u + ")";
    };
    return {
        {"profile", eq("profiles", "id")},
        {"friendships", either("friendships", "requester", "addressee")},
        {"follows", either("follows", "follower", "followee")},
        {"messages_sent", eq("messages", "sender")},
        {"group_messages_sent", eq("group_messages", "sender")},
        {"server_channel_messages", eq("channel_messages", "author")},
        {"message_reactions", eq("message_reactions", "reactor")},
        {"forum_threads", eq("forum_threads", "author")},
        {"forum_posts", eq("forum_posts", "author")},
        {"forum_votes", eq("forum_votes", "voter")},
        {"marketplace_listings", eq("workshop_items", "author")},
        {"marketplace_reviews", eq("workshop_reviews", "reviewer")},
        {"achievements", eq("user_achievements", "owner")},
        {"playtime", eq("user_playtime", "owner")},
        {"badges", eq("profile_badges", "owner")},
        {"activity", eq("activity", "actor")},
        {"servers_owned", eq("servers", "owner")},
        {"server_memberships", eq("server_members", "member")},
        {"group_chats_owned", eq("group_convos", "owner")},
        {"group_chat_memberships", eq("group_members", "member")},
        {"forum_sections_owned", eq("forum_sections", "owner")},
        {"cloud_saves", eq("cloud_saves", "user_id")},
        {"leaderboard_entries", eq("leaderboards", "user_id")},
        {"player_stats", eq("player_stats", "user_id")},
        {"anticheat_appeals", eq("ac_appeals", "user_id")},
    };
}

enum class SectionState { Ok, NotSetUp, Failed };

struct SectionOutcome {
    SectionState state = SectionState::Failed;
    std::string  json;    // the rows as a JSON array (Ok)
    std::string  error;   // Failed
};

/// Classify one response. A table the project never created is NotSetUp (not an
/// error); an expired session or a refused read is a real failure.
inline SectionOutcome ClassifySection(const ApiResponse& r) {
    SectionOutcome o;
    if (!r.reached) { o.error = "could not reach the server"; return o; }
    if (r.ok) {
        std::size_t i = r.body.find_first_not_of(" \t\r\n");
        if (i != std::string::npos && r.body[i] == '[') { o.state = SectionState::Ok; o.json = r.body.substr(i); }
        else o.error = "unexpected response";
        return o;
    }
    const bool missing = r.status == 404 || r.body.find("PGRST205") != std::string::npos ||
                         r.body.find("42P01") != std::string::npos || r.body.find("42703") != std::string::npos;
    if (missing) { o.state = SectionState::NotSetUp; return o; }
    o.error = r.status == 401 ? "your session expired, sign in again" : "HTTP " + std::to_string(r.status);
    return o;
}

struct ExportResult {
    bool        ok = false;        // false: nothing usable (not signed in / server unreachable)
    bool        complete = false;  // ok and no section failed
    std::string error;
    std::string json;              // the file contents
    std::size_t sections = 0, notSetUp = 0, failed = 0, rows = 0;
};

inline std::string IsoUtc(std::time_t t) {
    char b[32]; std::tm tmv{};
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return b;
}

/// Count top-level objects in a JSON array text (rows).
inline std::size_t CountRows(const std::string& arr) {
    std::size_t n = 0; int depth = 0; bool str = false, esc = false;
    for (char c : arr) {
        if (str) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') str = false; continue; }
        if (c == '"') str = true;
        else if (c == '{') { if (depth++ == 0) ++n; }
        else if (c == '}') --depth;
    }
    return n;
}

/// Build the export file. `fetch(path)` performs one authenticated GET (the
/// launcher passes `account.Api(path)`); `progress(done, total, key)` is called
/// before each request so the UI can show real progress.
inline ExportResult BuildExport(const std::string& uid, const std::string& username, std::time_t now,
                                const std::function<ApiResponse(const std::string&)>& fetch,
                                const std::function<void(std::size_t, std::size_t, const std::string&)>& progress = {}) {
    ExportResult out;
    if (uid.empty()) { out.error = "Sign in with an online account first."; return out; }
    const auto secs = ExportSections(uid);
    std::string sections, notSetUp, failed;
    for (std::size_t i = 0; i < secs.size(); ++i) {
        if (progress) progress(i, secs.size(), secs[i].key);
        ApiResponse r = fetch(secs[i].path);
        if (!r.reached && i == 0) { out.error = "Couldn't reach the account server."; return out; }
        SectionOutcome o = ClassifySection(r);
        if (o.state == SectionState::Ok) {
            if (!sections.empty()) sections += ",\n";
            sections += "    \"" + secs[i].key + "\": " + o.json;
            ++out.sections; out.rows += CountRows(o.json);
        } else if (o.state == SectionState::NotSetUp) {
            if (!notSetUp.empty()) notSetUp += ",";
            notSetUp += "\"" + secs[i].key + "\""; ++out.notSetUp;
        } else {
            if (!failed.empty()) failed += ",";
            failed += "{\"section\":\"" + secs[i].key + "\",\"error\":\"" + detail::JsonEscape(o.error) + "\"}"; ++out.failed;
        }
    }
    if (progress) progress(secs.size(), secs.size(), "");
    out.ok = true;
    out.complete = out.failed == 0;
    out.json = "{\n  \"format\": \"okayspace-data-export\",\n  \"version\": 1,\n  \"exported_at\": \"" + IsoUtc(now) +
               "\",\n  \"account\": {\"id\": \"" + detail::JsonEscape(uid) + "\", \"username\": \"" + detail::JsonEscape(username) +
               "\"},\n  \"complete\": " + (out.complete ? "true" : "false") +
               ",\n  \"not_set_up_on_this_server\": [" + notSetUp + "],\n  \"failed_sections\": [" + failed +
               "],\n  \"notes\": [\"Built only from rows your own account may read. Reports you filed with the anti-cheat "
               "system are not readable by their reporter and are not included. Your email is held by the sign-in service "
               "and is shown on the Account page. Launcher settings are stored on this PC, not on the server.\"],\n"
               "  \"sections\": {\n" + sections + "\n  }\n}\n";
    return out;
}

// ------------------------------------------------------------------ delete --

/// The "Delete my account" button is enabled only when all of these hold.
/// `typed` must equal the account's username (case-insensitive, trimmed).
inline bool DeleteConfirmed(const std::string& typed, const std::string& username,
                            const std::string& password, bool understood) {
    auto trim = [](std::string s) {
        std::size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string t = trim(typed), u = trim(username);
    return understood && !u.empty() && lower(t) == lower(u) && !password.empty();
}

struct DeleteRequest { std::string method, path, body; };

/// The request sent to the Edge Function. Identity is the bearer token that
/// AccountService::Api attaches; the body never carries a user id.
inline DeleteRequest BuildDeleteRequest(const std::string& username, const std::string& password) {
    return {"POST", "/functions/v1/delete-account",
            "{\"confirm_username\":\"" + detail::JsonEscape(username) + "\",\"password\":\"" + detail::JsonEscape(password) + "\"}"};
}

struct DeleteOutcome {
    bool        ok = false;
    bool        signOut = false;       // account is gone: clear the local session
    bool        notDeployed = false;
    std::string failedStep;            // server-reported, when a step failed
    std::string message;               // honest text for the user
};

/// Turn the function's response into a user message. Never claims success unless
/// the function said so (HTTP 200 with "ok":true).
inline DeleteOutcome MapDeleteResponse(const ApiResponse& r) {
    DeleteOutcome o;
    if (!r.reached) { o.message = "No reply from the server. If you were offline, nothing was deleted. If the server was busy the deletion may have finished: try again, and if your account is already gone, signing in will fail."; return o; }
    const std::string err = detail::JsonField(r.body, "error");
    if (r.ok && r.body.find("\"ok\":true") != std::string::npos) {
        o.ok = true; o.signOut = true;
        o.message = "Your account and data were deleted. You have been signed out.";
        return o;
    }
    switch (r.status) {
        case 404:
            o.notDeployed = true;
            o.message = "Account deletion is not set up on this server yet: the delete-account function is not deployed. "
                        "Nothing was deleted. Ask the server owner to deploy it (docs/account_data.md).";
            return o;
        case 401: o.message = "Your session expired. Sign in again, then retry. Nothing was deleted."; return o;
        case 403: o.message = err.empty() ? "Not allowed. Nothing was deleted." : (err[0] >= 'a' && err[0] <= 'z' ? std::string(1, (char)(err[0] - 32)) + err.substr(1) : err) + ". Nothing was deleted."; return o;
        case 400: o.message = (err.empty() ? std::string("The request was rejected") : err) + ". Nothing was deleted."; return o;
        case 500: {
            o.failedStep = detail::JsonField(r.body, "failed_step");
            if (!o.failedStep.empty()) {
                o.message = "Deletion stopped at step \"" + o.failedStep + "\". Some of your data may already be gone, but your account still exists. Retry to finish, or contact the server owner.";
                if (o.failedStep == "auth_user")
                    o.message = "Your data was removed but the sign-in record could not be deleted. Retry to finish, or contact the server owner.";
            } else {
                o.message = err.empty() ? "The server hit an error. Your account was not deleted. Try again later." : err;
            }
            return o;
        }
        default: break;
    }
    o.message = "Unexpected reply from the server (HTTP " + std::to_string(r.status) + "). Your account was not deleted.";
    return o;
}

} // namespace okay::account::data
