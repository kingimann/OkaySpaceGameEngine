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
#include <vector>
#include <cstdlib>

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

private:
    okay::account::AccountService& acct_;

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
