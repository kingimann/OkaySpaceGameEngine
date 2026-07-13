#pragma once
// PlayFab (Azure PlayFab) integration via its Client REST API — no SDK needed.
// Reuses the account layer's curl transport pattern: the system `curl` does the
// HTTPS, secrets travel through temp files (never argv), and responses are
// parsed with the same tiny JSON helpers. Blocking calls — do the login once
// on game start (an On Start Actions list), not every frame.
//
//   PlayFab::Get().Configure("AB12CD");                       // your Title ID
//   PlayFab::Get().LoginWithCustomID("player-device-1234");   // creates the account
//   PlayFab::Get().SetStat("highscore", 4200);                // leaderboard stat
//   std::vector<PlayFab::Entry> top;
//   PlayFab::Get().GetLeaderboard("highscore", 10, top);
//
// Visual scripting mirrors this with the playfab_* instructions.
#include "okay/Platform/Account/AccountService.hpp"   // detail::JsonField / RunCommand / RandomHex

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace okay {

class PlayFab {
public:
    static PlayFab& Get() { static PlayFab s; return s; }

    /// Set the PlayFab Title ID (Game Manager ▸ your title, e.g. "AB12CD").
    void Configure(const std::string& titleId) { titleId_ = titleId; }
    bool IsConfigured() const { return !titleId_.empty(); }
    bool IsLoggedIn()  const { return !ticket_.empty(); }
    const std::string& TitleId()   const { return titleId_; }
    const std::string& PlayFabId() const { return playFabId_; }
    const std::string& LastError() const { return lastError_; }

    /// Sign in (creating the account on first use) with any stable id you
    /// choose — a device id, a saved random id, or your own account name.
    bool LoginWithCustomID(const std::string& customId) {
        if (titleId_.empty()) return Fail("PlayFab: Configure(titleId) first");
        std::string body = "{\"TitleId\":\"" + Esc(titleId_) + "\",\"CustomId\":\"" +
                           Esc(customId) + "\",\"CreateAccount\":true}";
        return FinishLogin(Post("/Client/LoginWithCustomID", body, /*authed=*/false), customId);
    }

    /// Username + password account (PlayFab's classic auth) — pairs with
    /// RegisterWithPassword below and the engine's NetBackend Register/Login.
    bool LoginWithPassword(const std::string& username, const std::string& password) {
        if (titleId_.empty()) return Fail("PlayFab: Configure(titleId) first");
        std::string body = "{\"TitleId\":\"" + Esc(titleId_) + "\",\"Username\":\"" +
                           Esc(username) + "\",\"Password\":\"" + Esc(password) + "\"}";
        return FinishLogin(Post("/Client/LoginWithPlayFab", body, /*authed=*/false), username);
    }

    bool RegisterWithPassword(const std::string& username, const std::string& password) {
        if (titleId_.empty()) return Fail("PlayFab: Configure(titleId) first");
        std::string body = "{\"TitleId\":\"" + Esc(titleId_) + "\",\"Username\":\"" +
                           Esc(username) + "\",\"Password\":\"" + Esc(password) +
                           "\",\"RequireBothUsernameAndEmail\":false}";
        return FinishLogin(Post("/Client/RegisterPlayFabUser", body, /*authed=*/false), username);
    }

    void Logout() { ticket_.clear(); playFabId_.clear(); username_.clear(); }
    const std::string& Username()      const { return username_; }
    const std::string& SessionTicket() const { return ticket_; }

    /// The name other players see (shown on leaderboards).
    bool SetDisplayName(const std::string& name) {
        Resp r = Post("/Client/UpdateUserTitleDisplayName",
                      "{\"DisplayName\":\"" + Esc(name) + "\"}");
        return r.ok ? true : Fail(ErrOf(r, "set display name failed"));
    }

    /// Publish a statistic value (drives PlayFab leaderboards).
    bool SetStat(const std::string& stat, int value) {
        Resp r = Post("/Client/UpdatePlayerStatistics",
                      "{\"Statistics\":[{\"StatisticName\":\"" + Esc(stat) +
                      "\",\"Value\":" + std::to_string(value) + "}]}");
        return r.ok ? true : Fail(ErrOf(r, "set stat failed"));
    }

    struct Entry {
        std::string name;      // display name (or PlayFabId when unset)
        int value    = 0;      // the stat value
        int position = 0;      // 0-based rank
    };

    /// Top `maxCount` entries of a statistic's leaderboard.
    bool GetLeaderboard(const std::string& stat, int maxCount, std::vector<Entry>& out) {
        out.clear();
        if (maxCount < 1) maxCount = 10;
        Resp r = Post("/Client/GetLeaderboard",
                      "{\"StatisticName\":\"" + Esc(stat) +
                      "\",\"StartPosition\":0,\"MaxResultsCount\":" + std::to_string(maxCount) + "}");
        if (!r.ok) return Fail(ErrOf(r, "get leaderboard failed"));
        // Rows look like {"PlayFabId":"..","DisplayName":"..","StatValue":n,"Position":n}.
        // Walk each object so name/value/rank stay paired even when a row has
        // no display name.
        std::size_t pos = 0;
        while ((pos = r.body.find("\"StatValue\"", pos)) != std::string::npos) {
            std::size_t objStart = r.body.rfind('{', pos);
            std::size_t objEnd   = r.body.find('}', pos);
            if (objStart == std::string::npos || objEnd == std::string::npos) break;
            std::string obj = r.body.substr(objStart, objEnd - objStart + 1);
            Entry e;
            e.name = account::detail::JsonField(obj, "DisplayName");
            if (e.name.empty()) e.name = account::detail::JsonField(obj, "PlayFabId");
            e.value    = (int)NumField(obj, "StatValue");
            e.position = (int)NumField(obj, "Position");
            out.push_back(e);
            pos = objEnd;
        }
        return true;
    }

    /// Per-player key/value save data in the PlayFab backend.
    bool SetUserData(const std::string& key, const std::string& value) {
        Resp r = Post("/Client/UpdateUserData",
                      "{\"Data\":{\"" + Esc(key) + "\":\"" + Esc(value) + "\"}}");
        return r.ok ? true : Fail(ErrOf(r, "set user data failed"));
    }

    bool GetUserData(const std::string& key, std::string& outValue) {
        outValue.clear();
        Resp r = Post("/Client/GetUserData", "{\"Keys\":[\"" + Esc(key) + "\"]}");
        if (!r.ok) return Fail(ErrOf(r, "get user data failed"));
        // Response: {"data":{"Data":{"<key>":{"Value":"...","..."},...}}}
        std::size_t kp = r.body.find("\"" + key + "\"");
        if (kp == std::string::npos) return true;   // key simply not set yet
        outValue = account::detail::JsonField(r.body.substr(kp), "Value");
        return true;
    }

    bool DeleteUserData(const std::string& key) {
        Resp r = Post("/Client/UpdateUserData",
                      "{\"KeysToRemove\":[\"" + Esc(key) + "\"]}");
        return r.ok ? true : Fail(ErrOf(r, "delete user data failed"));
    }

    /// Every key stored for this player (the names, not the values).
    bool ListUserData(std::vector<std::string>& outKeys) {
        outKeys.clear();
        Resp r = Post("/Client/GetUserData", "{}");
        if (!r.ok) return Fail(ErrOf(r, "list user data failed"));
        std::size_t dp = r.body.find("\"Data\"");
        if (dp == std::string::npos) return true;
        std::size_t open = r.body.find('{', dp + 6);
        if (open == std::string::npos) return true;
        // Walk the Data object: at depth 1 each string is a key; skip its value
        // (a nested object) by brace counting.
        int depth = 0;
        for (std::size_t i = open; i < r.body.size(); ++i) {
            char c = r.body[i];
            if (c == '{') { ++depth; continue; }
            if (c == '}') { if (--depth == 0) break; continue; }
            if (c == '"' && depth == 1) {
                std::size_t end = i + 1;
                while (end < r.body.size() && r.body[end] != '"') {
                    if (r.body[end] == '\\') ++end;
                    ++end;
                }
                if (end >= r.body.size()) break;
                outKeys.push_back(r.body.substr(i + 1, end - i - 1));
                // Jump past this key's value object so its inner strings
                // aren't mistaken for keys.
                std::size_t vo = r.body.find('{', end);
                if (vo == std::string::npos) break;
                int vd = 0; std::size_t j = vo;
                for (; j < r.body.size(); ++j) {
                    if (r.body[j] == '{') ++vd;
                    else if (r.body[j] == '}') { if (--vd == 0) break; }
                }
                i = j;
            }
        }
        return true;
    }

private:
    PlayFab() = default;

    struct Resp { bool ok = false; long status = 0; std::string body; };

    bool Fail(const std::string& why) { lastError_ = why; return false; }

    bool FinishLogin(const Resp& r, const std::string& shownName) {
        if (!r.ok) return Fail(ErrOf(r, "login failed"));
        ticket_    = account::detail::JsonField(r.body, "SessionTicket");
        playFabId_ = account::detail::JsonField(r.body, "PlayFabId");
        if (ticket_.empty()) return Fail("PlayFab: no session ticket in response");
        username_ = shownName;
        lastError_.clear();
        return true;
    }

    static std::string Esc(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            if ((unsigned char)c >= 0x20) o += c;
        }
        return o;
    }

    static double NumField(const std::string& json, const std::string& key) {
        std::size_t p = json.find("\"" + key + "\"");
        if (p == std::string::npos) return 0;
        p = json.find(':', p);
        if (p == std::string::npos) return 0;
        return std::atof(json.c_str() + p + 1);
    }

    static std::string ErrOf(const Resp& r, const char* fallback) {
        std::string e = account::detail::JsonField(r.body, "errorMessage");
        if (e.empty()) e = account::detail::JsonField(r.body, "error");
        return "PlayFab: " + (e.empty() ? std::string(fallback) : e) +
               " (HTTP " + std::to_string(r.status) + ")";
    }

    // One POST to https://<title>.playfabapi.com<path> via the system curl.
    // Mirrors account::AccountService::HttpRequest (files for secrets, -w for
    // the status code, no -f so error bodies survive).
    Resp Post(const std::string& path, const std::string& jsonBody, bool authed = true) const {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path dir   = fs::temp_directory_path(ec);
        std::string id = account::detail::RandomHex(8);
        fs::path resp  = dir / ("okay_pf_" + id + ".out");
        fs::path code  = dir / ("okay_pf_" + id + ".code");
        fs::path bodyF = dir / ("okay_pf_" + id + ".json");
        fs::path cfgF  = dir / ("okay_pf_" + id + ".cfg");

        std::string url = "https://" + titleId_ + ".playfabapi.com" + path;
        std::string cmd = "curl -s -X POST -o \"" + resp.string() + "\" -w \"%{http_code}\"";
        std::string cfg = "header = \"Content-Type: application/json\"\n";
        if (authed && !ticket_.empty())
            cfg += "header = \"X-Authorization: " + ticket_ + "\"\n";
        std::ofstream(bodyF, std::ios::trunc) << jsonBody;
        std::ofstream(cfgF, std::ios::trunc) << cfg;
        cmd += " --data-binary @\"" + bodyF.string() + "\" -K \"" + cfgF.string() + "\"";
        cmd += " \"" + url + "\" > \"" + code.string() + "\"";
#if !defined(_WIN32)
        cmd += " 2>/dev/null";
#endif
        int rc = account::detail::RunCommand(cmd);

        auto slurp = [](const fs::path& p) {
            std::ifstream r(p, std::ios::binary);
            std::stringstream ss; ss << r.rdbuf(); return ss.str();
        };
        Resp out;
        out.body = slurp(resp);
        std::string codeS = slurp(code);
        fs::remove(resp, ec); fs::remove(code, ec);
        fs::remove(bodyF, ec); fs::remove(cfgF, ec);
        try { out.status = std::stol(codeS); } catch (...) { out.status = 0; }
        out.ok = (rc == 0 && out.status >= 200 && out.status < 300);
        return out;
    }

    std::string titleId_;
    std::string ticket_;      // session ticket (X-Authorization)
    std::string playFabId_;
    std::string username_;    // the id/name used at login (shown in UIs)
    std::string lastError_;
};

} // namespace okay
