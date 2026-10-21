#pragma once
// Workshop / Marketplace over Supabase (workshop_items table; see
// docs/sql/social.sql). Players browse and publish shared games, scenes and
// templates. Each item carries metadata (title, author, description, kind,
// download count) plus a public download URL — an author points it at a hosted
// build (a GitHub release, itch.io page, or a file in the public 'workshop'
// Storage bucket), and players "Get" it from the launcher.
//
// Like Friends, this drives an okay::account::AccountService instance (reusing
// the signed-in session + anon key + bearer token) and gracefully no-ops when
// signed out, offline, or when the table is missing, so it never breaks a build
// that hasn't run social.sql.
#include "okay/Platform/Account/AccountService.hpp"
#include <string>
#include <filesystem>
#include <vector>
#include <map>
#include <utility>
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

    // Everything published, newest first. Readable by anyone (RLS select=true).
    std::vector<WorkshopItem> List() {
        std::vector<WorkshopItem> out;
        auto r = acct_.Api(
            "/rest/v1/workshop_items"
            "?select=id,author_name,title,description,kind,storage_path,downloads"
            "&order=created_at.desc", "GET");
        if (!r.ok) return out;
        for (const auto& obj : SplitObjects(r.body)) {
            WorkshopItem w;
            w.id          = std::atol(Field(obj, "id").c_str());
            w.author      = Field(obj, "author_name");
            w.title       = Field(obj, "title");
            w.description = Field(obj, "description");
            w.kind        = Field(obj, "kind");
            w.url         = Field(obj, "storage_path");
            w.downloads   = std::atol(Field(obj, "downloads").c_str());
            if (!w.title.empty()) out.push_back(w);
        }
        return out;
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
        acct_.Api("/rest/v1/workshop_reviews?item_id=eq." + std::to_string(itemId), "DELETE");
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
        return acct_.Api("/rest/v1/workshop_reviews?item_id=eq." + std::to_string(itemId),
                         "DELETE").ok;
    }

    // Upload a file for a listing into the public "workshop" bucket (under my
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
        auto r = acct_.ApiUpload("/storage/v1/object/workshop/" + obj, localPath, "application/octet-stream", "x-upsert: true");
        if (!r.ok) {
            if (err) *err = r.status == 404 || r.body.find("Bucket not found") != std::string::npos
                ? "File uploads aren't set up on the server (create the 'workshop' bucket from docs/sql/social.sql)."
                : r.body.find("too large") != std::string::npos || r.status == 413 ? "That file is too big for the server's upload limit."
                : (r.body.empty() ? std::string("Upload failed.") : r.body);
            return "";
        }
        std::string base = acct_.ServerUrl();
        if (!base.empty() && base.back() == '/') base.pop_back();
        return base + "/storage/v1/object/public/workshop/" + obj;
    }

private:
    okay::account::AccountService& acct_;

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
        ++p; while (p < obj.size() && obj[p] == ' ') ++p;
        if (p >= obj.size()) return "";
        if (obj[p] == '"') {
            std::string v; ++p;
            while (p < obj.size() && obj[p] != '"') {
                if (obj[p] == '\\' && p + 1 < obj.size()) { ++p; char e = obj[p];
                    v += (e=='n'?'\n':e=='t'?'\t':e); }
                else v += obj[p];
                ++p;
            }
            return v;
        }
        std::size_t e = p; while (e < obj.size() && obj[e] != ',' && obj[e] != '}') ++e;
        std::string v = obj.substr(p, e - p);
        while (!v.empty() && v.back() == ' ') v.pop_back();
        return v == "null" ? "" : v;
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
