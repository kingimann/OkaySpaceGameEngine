// Real PlayFab backend over the Client REST API. Compiled only with
// -DOKAY_WITH_PLAYFAB=ON (requires libcurl). It performs live HTTPS calls to
// https://<TitleId>.playfabapi.com and needs a valid PlayFab Title ID at run
// time. A tiny JSON reader extracts just the fields the API surface needs.
#include "okay/Platform/PlayFab/PlayFabService.hpp"
#include "okay/Core/Log.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <string>

namespace okay {

namespace {
size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

// Minimal JSON field readers (sufficient for PlayFab's flat responses).
std::string JsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto p = json.find(needle);
    if (p == std::string::npos) return {};
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return {};
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size() || json[p] != '"') return {};
    ++p;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) ++p;
        out += json[p++];
    }
    return out;
}

bool JsonInt(const std::string& json, const std::string& key, int& out) {
    std::string needle = "\"" + key + "\"";
    auto p = json.find(needle);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    out = std::atoi(json.c_str() + p);
    return true;
}
} // namespace

std::unique_ptr<IPlayFabService> CreateNullPlayFabService();

class RestPlayFabService : public IPlayFabService {
public:
    bool Initialize(const PlayFabConfig& config) override {
        if (config.titleId.empty()) {
            OKAY_ERROR("PlayFab: titleId is required");
            return false;
        }
        m_titleId = config.titleId;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        m_ready = true;
        return true;
    }
    void Shutdown() override {
        if (m_ready) { curl_global_cleanup(); m_ready = false; }
    }

    const char* BackendName() const override { return "playfab-rest"; }
    bool IsRealBackend() const override { return true; }

    bool LoginWithCustomId(const std::string& customId, bool createAccount) override {
        std::string body = "{\"TitleId\":\"" + m_titleId + "\",\"CustomId\":\"" +
                           customId + "\",\"CreateAccount\":" +
                           (createAccount ? "true" : "false") + "}";
        std::string resp;
        if (!Post("/Client/LoginWithCustomID", body, resp, false)) return false;
        m_session   = JsonString(resp, "SessionTicket");
        m_playFabId = JsonString(resp, "PlayFabId");
        m_loggedIn  = !m_session.empty();
        if (m_loggedIn) OKAY_INFO("PlayFab: logged in as ", m_playFabId);
        else OKAY_ERROR("PlayFab login failed: ", resp);
        return m_loggedIn;
    }
    bool IsLoggedIn() const override { return m_loggedIn; }
    std::string PlayFabId() const override { return m_playFabId; }
    std::string SessionTicket() const override { return m_session; }

    bool SetUserData(const std::string& key, const std::string& value) override {
        std::string body = "{\"Data\":{\"" + key + "\":\"" + value + "\"}}";
        std::string resp;
        return Post("/Client/UpdateUserData", body, resp, true);
    }
    std::string GetUserData(const std::string& key) const override {
        std::string body = "{\"Keys\":[\"" + key + "\"]}";
        std::string resp;
        if (!const_cast<RestPlayFabService*>(this)->Post("/Client/GetUserData", body, resp, true))
            return {};
        return JsonString(resp, "Value");
    }

    bool UpdateStatistic(const std::string& name, int value) override {
        std::string body = "{\"Statistics\":[{\"StatisticName\":\"" + name +
                           "\",\"Value\":" + std::to_string(value) + "}]}";
        std::string resp;
        return Post("/Client/UpdatePlayerStatistics", body, resp, true);
    }
    int GetStatistic(const std::string& name) const override {
        std::string body = "{\"StatisticNames\":[\"" + name + "\"]}";
        std::string resp;
        if (!const_cast<RestPlayFabService*>(this)->Post("/Client/GetPlayerStatistics", body, resp, true))
            return 0;
        int v = 0;
        JsonInt(resp, "Value", v);
        return v;
    }
    std::vector<LeaderboardEntry> GetLeaderboard(const std::string& name, int maxCount) override {
        std::string body = "{\"StatisticName\":\"" + name +
                           "\",\"StartPosition\":0,\"MaxResultsCount\":" +
                           std::to_string(maxCount) + "}";
        std::string resp;
        std::vector<LeaderboardEntry> out;
        if (!Post("/Client/GetLeaderboard", body, resp, true)) return out;
        // Walk each {"PlayFabId":..,"StatValue":..,"Position":..,"DisplayName":..}
        size_t pos = 0;
        while ((pos = resp.find("\"PlayFabId\"", pos)) != std::string::npos) {
            std::string slice = resp.substr(pos, 256);
            LeaderboardEntry e;
            e.playFabId   = JsonString(slice, "PlayFabId");
            e.displayName = JsonString(slice, "DisplayName");
            JsonInt(slice, "StatValue", e.value);
            JsonInt(slice, "Position", e.rank);
            out.push_back(e);
            pos += 11;
            if ((int)out.size() >= maxCount) break;
        }
        return out;
    }

private:
    bool Post(const std::string& endpoint, const std::string& body,
              std::string& response, bool authenticated) {
        if (!m_ready) return false;
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string url = "https://" + m_titleId + ".playfabapi.com" + endpoint;
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string authHeader;
        if (authenticated && !m_session.empty()) {
            authHeader = "X-Authorization: " + m_session;
            headers = curl_slist_append(headers, authHeader.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) { OKAY_ERROR("PlayFab HTTP error: ", curl_easy_strerror(rc)); return false; }
        if (status < 200 || status >= 300) { OKAY_ERROR("PlayFab API ", status, ": ", response); return false; }
        return true;
    }

    std::string m_titleId, m_playFabId, m_session;
    bool m_ready = false, m_loggedIn = false;
};

std::unique_ptr<IPlayFabService> CreatePlayFabService() {
    return std::make_unique<RestPlayFabService>();
}

} // namespace okay
