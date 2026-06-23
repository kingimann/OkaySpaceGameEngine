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
    std::string& ConfigDir() { static std::string d; return d; }
    std::string& ServerUrl() { static std::string u; return u; }
    std::string& LastErr()   { static std::string e; return e; }

    // Where account data lives when Configure() wasn't called: the per-user
    // config directory, falling back to the current directory.
    std::string DefaultDir() {
        return account::DefaultConfigDir(std::filesystem::path(".")).string();
    }
    // The server URL to use when Configure() didn't set one.
    std::string DefaultServer() {
        if (const char* env = std::getenv("OKAY_ACCOUNT_SERVER")) return env;
        return {};
    }
}

void Account::Configure(const std::string& configDir, const std::string& serverUrl) {
    ConfigDir() = configDir;
    ServerUrl() = serverUrl;
    Service().reset();              // rebuilt on next Get()
}

bool Account::Exists() { return Service() != nullptr; }

account::AccountService& Account::Get() {
    auto& s = Service();
    if (!s) {
        std::string dir = ConfigDir().empty() ? DefaultDir() : ConfigDir();
        std::string url = ServerUrl().empty() ? DefaultServer() : ServerUrl();
        s = std::make_unique<account::AccountService>(std::filesystem::path(dir), url);
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

account::ApiResponse Account::Api(const std::string& path, const std::string& method,
                                 const std::string& jsonBody) {
    return Get().Api(path, method, jsonBody);
}

std::string Account::LastError() { return LastErr(); }

} // namespace okay
