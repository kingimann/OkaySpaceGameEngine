#pragma once
// Workshop / Marketplace over Supabase (workshop_items table; see
// docs/sql/social.sql). Players browse and publish shared games, scenes and
// templates. Each item carries metadata (title, author, description, kind,
// download count) plus a public download URL: a file the author uploaded to the
// public 'workshop' Storage bucket (the server refuses outside links, and the
// client keeps only those links too: IsOwnStorageUrl), which players "Get" from
// the launcher. Replies are read with okay::json (Core/Json.hpp).
//
// Like Friends, this drives an okay::account::AccountService instance (reusing
// the signed-in session + anon key + bearer token) and gracefully no-ops when
// signed out, offline, or when the table is missing, so it never breaks a build
// that hasn't run social.sql.
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Backend/SupabaseRoutes.hpp"
#include "okay/Core/Json.hpp"
#include <string>
#include <filesystem>
#include <vector>
#include <map>
#include <utility>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <ctime>

namespace okay {

struct WorkshopItem {
    long        id = 0;
    std::string title;
    std::string author;       // author_name as published
    std::string description;
    std::string kind;         // "game" | "scene" | "template"
    std::string url;          // public download URL (stored in storage_path)
    long        downloads = 0;
    // Marketplace review (docs/sql/admin.sql); "approved" when the server has no review.
    std::string status = "approved";   // pending | approved | rejected
    std::string reviewNote;            // why it was rejected
    std::string authorId;
    bool        reviewsLocked = false;
    bool        featured = false;
    long        created = 0;
};

struct WorkshopReview {
    std::string reviewer;     // reviewer_name as written
    int         rating = 0;   // 1-5 stars
    std::string body;
    long        when = 0;
    bool        mine = false; // true when it's my own review
};

class Workshop {
public:
    explicit Workshop(okay::account::AccountService& acct) : acct_(acct) {}

    bool Available() const { return acct_.IsLoggedIn(); }

    // Everything published, newest first. Readable by anyone (RLS select=true). `reached` (optional) is set to false
    // when the server could not be asked or refused (offline, not set up), so a caller can tell that from "nothing listed".
    std::vector<WorkshopItem> List(bool* reached = nullptr) {
        return Query("&order=featured.desc,created_at.desc", "&order=created_at.desc", reached);
    }
    /// Listings with a review status (mods see every status; RLS shows others
    /// only approved ones + their own). Empty status = all.
    std::vector<WorkshopItem> ByStatus(const std::string& status) {
        return Query(status.empty() ? "&order=created_at.desc" : "&status=eq." + status + "&order=created_at.asc", "");
    }
    /// Approve / reject / re-queue a listing (moderators; RLS + trigger enforce it).
    bool SetStatus(long id, const std::string& status, const std::string& note) {
        return acct_.Api("/rest/v1/workshop_items?id=eq." + std::to_string(id), "PATCH",
                         "{\"status\":" + JStr(status) + ",\"review_note\":" + JStr(note) + "}").ok;
    }
    bool SetFeatured(long id, bool on) {
        return acct_.Api("/rest/v1/workshop_items?id=eq." + std::to_string(id), "PATCH",
                         std::string("{\"featured\":") + (on ? "true" : "false") + "}").ok;
    }
    /// Turn reviews off / on for an item (its author or a moderator).
    bool SetReviewsLocked(long id, bool on) {
        return acct_.Api("/rest/v1/workshop_items?id=eq." + std::to_string(id), "PATCH",
                         std::string("{\"reviews_locked\":") + (on ? "true" : "false") + "}").ok;
    }
    /// Remove one review (moderators).
    bool RemoveReviewBy(long itemId, const std::string& reviewerName) {
        return acct_.Api("/rest/v1/workshop_reviews?item_id=eq." + std::to_string(itemId) +
                         "&reviewer_name=eq." + Url(reviewerName), "DELETE").ok;
    }

private:
    // `orderA` is tried with the review columns; `orderB` (if set) without
    // them, for servers that haven't run admin.sql.
    std::vector<WorkshopItem> Query(const std::string& orderA, const std::string& orderB, bool* reached = nullptr) {
        std::vector<WorkshopItem> out;
        if (reached) *reached = true;
        static const char* kBase = "/rest/v1/workshop_items?select=id,author,author_name,title,description,kind,storage_path,downloads,created_at";
        bool review = true;
        auto r = acct_.Api(std::string(kBase) + ",status,review_note,reviews_locked,featured" + orderA, "GET");
        if (!r.ok && !orderB.empty()) { review = false; r = acct_.Api(std::string(kBase) + orderB, "GET"); }
        if (!r.ok) { if (reached) *reached = false; return out; }
        return ParseItems(r.body, acct_.ServerUrl(), review);
    }
public:
    /// The listings in a PostgREST reply (a JSON array of rows), read with okay::json: every field by its exact key, so a
    /// title or description cannot stand in for another column (#888 F9). `url` is kept only when it is a file in our own
    /// 'workshop' bucket on `serverUrl` (IsOwnStorageUrl); anything else is left empty ("no file to download").
    static std::vector<WorkshopItem> ParseItems(const std::string& body, const std::string& serverUrl, bool review) {
        std::vector<WorkshopItem> out;
        for (const auto& obj : SplitObjects(body)) {
            WorkshopItem w;
            w.authorId    = Field(obj, "author");
            w.created     = ParseIso(Field(obj, "created_at"));
            if (review) {
                w.status        = Field(obj, "status");
                if (w.status.empty()) w.status = "approved";
                w.reviewNote    = Field(obj, "review_note");
                w.reviewsLocked = Field(obj, "reviews_locked") == "true";
                w.featured      = Field(obj, "featured") == "true";
            }
            w.id          = std::atol(Field(obj, "id").c_str());
            w.author      = okay::account::PublicName(Field(obj, "author_name"));
            w.title       = Field(obj, "title");
            w.description = Field(obj, "description");
            w.kind        = Field(obj, "kind");
            w.url         = Field(obj, "storage_path");
            if (!IsOwnStorageUrl(serverUrl, w.url)) w.url.clear();
            w.downloads   = std::atol(Field(obj, "downloads").c_str());
            if (!w.title.empty()) out.push_back(w);
        }
        return out;
    }
    /// A public file in the 'workshop' bucket of our own server: "<serverUrl>/storage/v1/object/public/workshop/<path>",
    /// the only links the server's workshop_items_guard lets an author save (docs/sql/admin.sql). No "..", no
    /// backslash, no spaces, quotes or control characters, and nothing when the server is unknown.
    static bool IsOwnStorageUrl(std::string serverUrl, const std::string& url) {
        while (!serverUrl.empty() && serverUrl.back() == '/') serverUrl.pop_back();
        if (serverUrl.empty() || (serverUrl.rfind("https://", 0) != 0 && serverUrl.rfind("http://", 0) != 0)) return false;
        const std::string prefix = serverUrl + okay::backend::supabase::routes::StoragePublic("workshop", "");
        if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) return false;
        if (url.find("..") != std::string::npos || url.find('\\') != std::string::npos) return false;
        for (unsigned char c : url) if (c <= ' ' || c == '"' || c == '\'' || c == '<' || c == '>' || c == 0x7F) return false;
        return true;
    }

    // Publish a new item. The author uuid defaults to auth.uid() server-side,
    // so we only send the display fields. Returns true on a 2xx.
    bool Publish(const std::string& title, const std::string& description,
                 const std::string& kind, const std::string& url,
                 std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in to publish."; return false; }
        if (title.empty())       { if (err) *err = "A title is required.";  return false; }
        std::string body = "{\"title\":" + JStr(title) +
                           ",\"description\":" + JStr(description) +
                           ",\"kind\":" + JStr(kind.empty() ? "game" : kind) +
                           ",\"storage_path\":" + JStr(url) +
                           ",\"author_name\":" + JStr(acct_.CurrentSession().username) + "}";
        auto r = acct_.Api("/rest/v1/workshop_items", "POST", body);
        if (!r.ok && err) *err = r.body.empty() ? "Publish failed." : r.body;
        return r.ok;
    }

    // Edit one of my own items (RLS "edit own item" blocks editing others').
    bool Update(long id, const std::string& title, const std::string& description,
                const std::string& kind, const std::string& url, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in to edit."; return false; }
        if (id <= 0 || title.empty()) { if (err) *err = "A title is required."; return false; }
        std::string body = "{\"title\":" + JStr(title) +
                           ",\"description\":" + JStr(description) +
                           ",\"kind\":" + JStr(kind.empty() ? "game" : kind) +
                           ",\"storage_path\":" + JStr(url) + "}";
        auto r = acct_.Api("/rest/v1/workshop_items?id=eq." + std::to_string(id), "PATCH", body);
        if (!r.ok && err) *err = r.body.empty() ? "Edit failed." : r.body;
        return r.ok;
    }

    // Best-effort download-counter bump (fire and forget).
    void NoteDownload(long id, long current) {
        if (!acct_.IsLoggedIn() || id <= 0) return;
        acct_.Api("/rest/v1/workshop_items?id=eq." + std::to_string(id),
                  "PATCH", "{\"downloads\":" + std::to_string(current + 1) + "}");
    }

    // Remove one of my own items (RLS blocks removing others').
    bool Remove(long id) {
        if (id <= 0) return false;
        return acct_.Api("/rest/v1/workshop_items?id=eq." + std::to_string(id),
                         "DELETE").ok;
    }

    // --- Reviews & ratings -------------------------------------------------

    // Aggregate rating per item: itemId -> (sumOfStars, count). One request;
    // the launcher turns this into an average + star display in the list. If
    // the reviews table is missing/empty this is just an empty map (no-op).
    std::map<long, std::pair<long, long>> Ratings() {
        std::map<long, std::pair<long, long>> out;
        auto r = acct_.Api("/rest/v1/workshop_reviews?select=item_id,rating", "GET");
        if (!r.ok) return out;
        for (const auto& obj : SplitObjects(r.body)) {
            long id = std::atol(Field(obj, "item_id").c_str());
            long rt = std::atol(Field(obj, "rating").c_str());
            if (id <= 0 || rt < 1) continue;
            out[id].first += rt; out[id].second += 1;
        }
        return out;
    }

    // All reviews for one item, newest first.
    std::vector<WorkshopReview> Reviews(long itemId) {
        std::vector<WorkshopReview> out;
        if (itemId <= 0) return out;
        auto r = acct_.Api("/rest/v1/workshop_reviews"
                           "?select=reviewer_name,rating,body,created_at"
                           "&item_id=eq." + std::to_string(itemId) +
                           "&order=created_at.desc", "GET");
        if (!r.ok) return out;
        std::string me = acct_.CurrentSession().username;
        for (const auto& obj : SplitObjects(r.body)) {
            WorkshopReview w;
            w.reviewer = Field(obj, "reviewer_name");
            w.rating   = (int)std::atol(Field(obj, "rating").c_str());
            w.body     = Field(obj, "body");
            w.when     = ParseIso(Field(obj, "created_at"));
            w.mine     = (!me.empty() && me == w.reviewer);
            out.push_back(w);
        }
        return out;
    }

    // Post (or replace) my review for an item. Deletes any prior review of mine
    // first (RLS scopes the delete to my own row), then inserts the new one, so
    // re-rating just overwrites. reviewer defaults to auth.uid() server-side.
    bool SubmitReview(long itemId, int rating, const std::string& body,
                      std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in to review."; return false; }
        if (itemId <= 0 || rating < 1 || rating > 5) { if (err) *err = "Pick 1-5 stars."; return false; }
        acct_.Api("/rest/v1/workshop_reviews?item_id=eq." + std::to_string(itemId) +
                  "&reviewer=eq." + Url(MyUid()), "DELETE");   // only mine (mods may delete any review)
        std::string payload = "{\"item_id\":" + std::to_string(itemId) +
                              ",\"rating\":" + std::to_string(rating) +
                              ",\"body\":" + JStr(body) +
                              ",\"reviewer_name\":" + JStr(acct_.CurrentSession().username) + "}";
        auto r = acct_.Api("/rest/v1/workshop_reviews", "POST", payload);
        if (!r.ok && err) *err = r.body.empty() ? "Review failed." : r.body;
        return r.ok;
    }

    // Delete my review for an item (RLS scopes to my own row).
    bool RemoveMyReview(long itemId) {
        if (!acct_.IsLoggedIn() || itemId <= 0) return false;
        return acct_.Api("/rest/v1/workshop_reviews?item_id=eq." + std::to_string(itemId) +
                         "&reviewer=eq." + Url(MyUid()), "DELETE").ok;
    }

    // Upload a file for a listing into the public "workshop" bucket (OkayBackend Storage; under my
    // own folder) and return its public URL ("" on failure, with *err set).
    std::string UploadFile(const std::string& localPath, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in to publish."; return ""; }
        std::string enc; for (char c : acct_.CurrentSession().username) {
            if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') enc += c;
            else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); enc += b; } }
        auto me = acct_.Api("/rest/v1/profiles?select=id&username=eq." + enc, "GET");
        auto mo = me.ok ? SplitObjects(me.body) : std::vector<std::string>();
        const std::string uid = mo.empty() ? std::string() : Field(mo[0], "id");
        if (uid.empty()) { if (err) *err = "Couldn't resolve your account."; return ""; }
        std::string name = std::filesystem::path(localPath).filename().string(), safe;
        for (char c : name) safe += (std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? c : '_';
        if (safe.empty()) safe = "file";
        char rnd[24]; std::snprintf(rnd, sizeof(rnd), "%08x%08x", (unsigned)std::rand(), (unsigned)std::time(nullptr));
        const std::string obj = uid + "/" + rnd + "-" + safe;
        okay::backend::UploadOptions replace;
        replace.upsert = true;
        auto r = acct_.Storage().Upload("workshop", obj, localPath, "application/octet-stream", replace);
        if (!r.ok) {
            if (err) *err = r.status == 404 || r.body.find("Bucket not found") != std::string::npos
                ? "File uploads aren't set up on the server (create the 'workshop' bucket from docs/sql/social.sql)."
                : r.body.find("too large") != std::string::npos || r.status == 413 ? "That file is too big for the server's upload limit."
                : (r.body.empty() ? std::string("Upload failed.") : r.body);
            return "";
        }
        return acct_.Storage().PublicUrl("workshop", obj);
    }

private:
    okay::account::AccountService& acct_;
    std::string myUid_;

    static std::string Url(const std::string& v) {
        std::string o;
        for (unsigned char c : v) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
            else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
        }
        return o;
    }
    // My user id (looked up by username once). "-" when unknown, so a scoped
    // delete matches nothing rather than everything.
    std::string MyUid() {
        if (myUid_.empty() && acct_.IsLoggedIn()) {
            auto r = acct_.Api("/rest/v1/profiles?select=id&username=eq." + Url(acct_.CurrentSession().username), "GET");
            if (r.ok) { auto o = SplitObjects(r.body); if (!o.empty()) myUid_ = Field(o[0], "id"); }
        }
        return myUid_.empty() ? std::string("-") : myUid_;
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
public:
    // Tiny JSON helpers (public so the tests can reach them).
    // Top-level JSON objects. Braces inside strings ("a { in a description") don't count.
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
    /// One top-level field of a JSON object, read with okay::json by its exact key (#888 F9: the hand-written reader took the
    /// first "key" text anywhere, so a title "storage_path" made the description the download link). Strings come back
    /// decoded, numbers as their whole-number text when they have no fraction, true/false as words, null and anything
    /// missing or not JSON as "".
    static std::string Field(const std::string& obj, const std::string& key) {
        okay::json::Value v;
        if (!okay::json::Parse(obj, v) || !v.IsObject()) return "";
        const okay::json::Value* f = v.Get(key);
        if (!f) return "";
        switch (f->type) {
            case okay::json::Type::String: return f->string;
            case okay::json::Type::Bool:   return f->boolean ? "true" : "false";
            case okay::json::Type::Number: {
                if (f->number > -9.2e18 && f->number < 9.2e18 && f->number == (double)(long long)f->number)
                    return std::to_string((long long)f->number);
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.17g", f->number);
                return buf;
            }
            default: return "";
        }
    }
    static std::string JStr(const std::string& s) {
        std::string o = "\""; for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else if (c == '\t') o += "\\t";
            else if (c == '\r') {}
            else o += c;
        } o += "\""; return o;
    }
};

} // namespace okay
