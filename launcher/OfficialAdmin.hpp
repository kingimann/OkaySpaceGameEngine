#pragma once
// The launcher's Official game admin tab (launcher/main.cpp, OfficialAdminWorker): the requests it sends to the "releases"
// bucket's official/ folder with the admin's own session (docs/sql/official_game_admin.sql), through OkayBackend Storage
// (AccountService::Storage). Moved here from launcher/main.cpp (OkayBackend round 4) so a test can run them against a fake
// server: tests/test_okay_backend_golden_storage.cpp checks they send the same bytes as before the move.
// Header-only; the launcher is its only program.
#include "OfficialGame.hpp"                           // og:: manifest, storage list, encoding, JSON escape
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Platform.hpp"                 // okay::platform::TicksMs32 (the cache-buster)

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace og = okayofficial;

inline constexpr const char* kOaNeedSql = "Run docs/sql/official_game_admin.sql on Supabase to let admins publish.";

// Requests (worker side). Reads go to the public URL with a cache-buster, like
// the players' launchers; writes and listings use the admin's own session.
inline okay::account::ApiResponse OaGet(const okay::account::AccountService& a, const std::string& rel) {
    return a.Storage().ReadPublic("releases", "official/" + og::UrlEncodePath(rel), "cb=" +
                 std::to_string((long long)std::time(nullptr)) + "-" + std::to_string(okay::platform::TicksMs32()));
}
inline okay::account::ApiResponse OaUpload(const okay::account::AccountService& a, const std::string& rel, const std::filesystem::path& file,
                                  const char* type, int cacheSec) {
    okay::backend::UploadOptions o;
    o.upsert = true;
    o.cacheSeconds = cacheSec;
    return a.Storage().Upload("releases", "official/" + og::UrlEncodePath(rel), file.string(), type, o);
}
inline bool OaUploadText(const okay::account::AccountService& a, const std::string& rel, const std::string& text,
                         const char* type, int cacheSec, okay::account::ApiResponse* resp) {
    std::error_code ec;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / ("okayspace_oa_" + std::to_string(okay::platform::TicksMs32()) + "_" +
                                                        std::to_string(std::rand()) + ".txt");
    { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); o << text; if (!o) return false; }
    *resp = OaUpload(a, rel, tmp, type, cacheSec);
    std::filesystem::remove(tmp, ec);
    return resp->ok;
}
// Every object under `prefix` ("official/v1.2.0/"), recursively, as bucket names.
inline bool OaListAll(const okay::account::AccountService& a, const std::string& prefix, std::vector<std::string>& out, int depth = 0) {
    if (depth > 24) return false;
    for (int offset = 0;; offset += 1000) {
        okay::backend::ListOptions page;
        page.prefix = prefix;
        page.limit = 1000;
        page.offset = offset;
        const auto r = a.Storage().List("releases", page);
        std::vector<og::StorageEntry> list;
        if (!r.ok || !og::ParseStorageList(r.body, list)) return false;
        for (const og::StorageEntry& e : list) {
            if (e.folder) { if (!OaListAll(a, prefix + e.name + "/", out, depth + 1)) return false; }
            else out.push_back(prefix + e.name);
        }
        if (list.size() < 1000) return true;
    }
}
// A version's manifest, checked; false if it's missing or doesn't check out.
inline bool OaManifest(const okay::account::AccountService& a, const std::string& ver, og::Manifest& m) {
    const auto r = OaGet(a, "v" + ver + "/" + og::kManifestName);
    return r.ok && og::ParseManifest(r.body, m) && m.version == ver;
}
inline std::string OaLive(const okay::account::AccountService& a, bool* reached = nullptr) {
    const auto r = OaGet(a, "VERSION.txt");
    if (reached) *reached = r.reached;
    const std::string v = og::Trim(r.body.substr(0, r.body.find('\n')));
    return r.ok && og::LooksLikeVersion(v) ? v : std::string();
}
// A failed write in plain words. Never the token; the server's message, capped.
inline std::string OaError(const okay::account::ApiResponse& r, const std::string& what, bool& needSql) {
    if (!r.reached) return "Couldn't reach the server while " + what + ". Check your connection and try again.";
    if (og::StorageDenied(r.status, r.body)) { needSql = true; return kOaNeedSql; }
    if (r.status == 401) return "Your sign-in has expired. Sign out and back in, then try again.";
    if (r.status == 413 || r.body.find("too large") != std::string::npos) return what + ": the file is over the server's upload limit.";
    if (r.body.find("Bucket not found") != std::string::npos)
        return "The releases bucket doesn't exist yet (scripts/publish-supabase.sh creates it).";
    return what + " failed (HTTP " + std::to_string(r.status) + ")" +
           (r.body.empty() ? std::string(".") : ": " + og::CleanText(r.body, 160, false));
}
inline void OaLog(const okay::account::AccountService& a, const std::string& action, const std::string& ver, long long files, long long bytes) {
    std::string body = "{\"p_version\":\"" + og::json::Escape(ver) + "\",\"p_action\":\"" + action + "\"";
    if (files >= 0) body += ",\"p_files\":" + std::to_string(files) + ",\"p_bytes\":" + std::to_string(bytes);
    a.Api("/rest/v1/rpc/log_official_publish", "POST", body + "}");   // best effort: the publish already happened
}
// Delete one version folder (never the live one). False (with a reason) on failure.
inline bool OaDeleteVersion(const okay::account::AccountService& a, const std::string& ver, std::string& why, bool& needSql) {
    bool reached = true;
    const std::string live = OaLive(a, &reached);
    if (!reached) { why = "Couldn't reach the server."; return false; }
    if (live == ver) { why = "v" + ver + " is live. Make another version live first."; return false; }
    std::vector<std::string> names;
    if (!OaListAll(a, "official/v" + ver + "/", names)) { why = "Couldn't list v" + ver + "'s files."; return false; }
    if (names.empty()) { why = "v" + ver + " has no files the server lets you see. " + kOaNeedSql; needSql = true; return false; }
    std::size_t gone = 0;
    for (std::size_t i = 0; i < names.size(); i += 200) {
        const std::vector<std::string> batch(names.begin() + (std::ptrdiff_t)i, names.begin() + (std::ptrdiff_t)std::min(names.size(), i + 200));
        const auto r = a.Storage().Remove("releases", batch);
        if (!r.ok) { why = OaError(r, "Deleting v" + ver, needSql); return false; }
        std::vector<og::StorageEntry> del;
        if (og::ParseStorageList(r.body, del)) gone += del.size();
    }
    if (gone == 0) { why = kOaNeedSql; needSql = true; return false; }
    OaLog(a, "delete", ver, -1, 0);
    return true;
}

// The top of official/ (the version folders), one page, as the tab's refresh lists it.
inline okay::account::ApiResponse OaListTop(const okay::account::AccountService& a) {
    okay::backend::ListOptions page;
    page.prefix = "official/";
    page.limit = 1000;
    return a.Storage().List("releases", page);
}
