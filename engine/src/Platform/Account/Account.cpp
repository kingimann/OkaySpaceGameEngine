#include "okay/Platform/Account/Account.hpp"
#include <cstdlib>
#include <memory>

namespace okay {

namespace {
    // The shared service plus the configuration used to (re)create it. The
    // service is built lazily on first Get() so a game that never touches
    // accounts pays nothing.
    std::unique_ptr<account::AccountService>& Service() {
        static std::unique_ptr<account::AccountService> s;
        return s;
    }
    // Configuration set via Configure(); empty entries fall back to env vars /
    // compile-time defaults in Get().
    bool&        Configured() { static bool b = false; return b; }
    std::string& ConfigDir()  { static std::string d; return d; }
    std::string& ServerUrl()  { static std::string u; return u; }
    std::string& ApiKey()     { static std::string k; return k; }
    std::string& LastErr()    { static std::string e; return e; }

    // Compile-time defaults let a shipped build be online out of the box:
    //   -DOKAY_DEFAULT_ACCOUNT_URL=... -DOKAY_DEFAULT_ACCOUNT_KEY=...
#ifndef OKAY_DEFAULT_ACCOUNT_URL
#  define OKAY_DEFAULT_ACCOUNT_URL ""
#endif
#ifndef OKAY_DEFAULT_ACCOUNT_KEY
#  define OKAY_DEFAULT_ACCOUNT_KEY ""
#endif

    std::string EnvOr(const char* name, const char* fallback) {
        if (const char* v = std::getenv(name)) return v;
        return fallback;
    }

    // Where account data lives when Configure() wasn't called: the per-user
    // config directory, falling back to the current directory.
    std::string DefaultDir() {
        return account::DefaultConfigDir(std::filesystem::path(".")).string();
    }
}

void Account::Configure(const std::string& configDir, const std::string& serverUrl,
                        const std::string& apiKey) {
    Configured() = true;
    ConfigDir() = configDir;
    ServerUrl() = serverUrl;
    ApiKey()    = apiKey;
    Service().reset();              // rebuilt on next Get()
}

bool Account::Exists() { return Service() != nullptr; }

account::AccountService& Account::Get() {
    auto& s = Service();
    if (!s) {
        // Configure() wins; otherwise read env vars, then compile-time defaults.
        std::string dir = !ConfigDir().empty() ? ConfigDir() : DefaultDir();
        std::string url = ServerUrl(), key = ApiKey();
        if (!Configured()) {
            url = EnvOr("OKAY_ACCOUNT_SERVER", OKAY_DEFAULT_ACCOUNT_URL);
            key = EnvOr("OKAY_ACCOUNT_API_KEY", OKAY_DEFAULT_ACCOUNT_KEY);
        }
        s = std::make_unique<account::AccountService>(std::filesystem::path(dir), url, key);
    }
    return *s;
}

void Account::Shutdown() { Service().reset(); }

bool Account::IsOnline()   { return Get().IsOnline(); }
bool Account::IsLoggedIn() { return Get().IsLoggedIn(); }
std::string Account::Username() { return Get().CurrentSession().username; }
std::string Account::Token()    { return Get().CurrentSession().token; }

account::Result Account::Register(const std::string& username, const std::string& password) {
    account::Result r = Get().Register(username, password);
    LastErr() = r.ok ? std::string{} : r.error;
    return r;
}

account::Result Account::Login(const std::string& username, const std::string& password) {
    account::Result r = Get().Login(username, password);
    LastErr() = r.ok ? std::string{} : r.error;
    return r;
}

void Account::Logout() { Get().Logout(); }

bool Account::VerifySession() { return Get().VerifySession(); }

bool Account::VerifyToken(const std::string& token, std::string& outUserId) {
    return Get().VerifyToken(token, outUserId);
}

account::ApiResponse Account::Api(const std::string& path, const std::string& method,
                                 const std::string& jsonBody, const std::string& extraHeader) {
    return Get().Api(path, method, jsonBody, extraHeader);
}

bool Account::CloudSave(const std::string& key, const std::string& data) {
    return Get().CloudSave(key, data);
}
std::string Account::CloudLoad(const std::string& key) { return Get().CloudLoad(key); }
bool Account::CloudHas(const std::string& key)        { return Get().CloudHas(key); }
bool Account::CloudDelete(const std::string& key)     { return Get().CloudDelete(key); }
std::vector<std::string> Account::CloudList()         { return Get().CloudList(); }

bool Account::LeaderboardSubmit(const std::string& board, long score) {
    return Get().LeaderboardSubmit(board, score);
}
std::vector<account::ScoreEntry> Account::LeaderboardTop(const std::string& board, int count) {
    return Get().LeaderboardTop(board, count);
}

std::string Account::LastError() { return LastErr(); }

} // namespace okay
