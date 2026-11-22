// The Epic Games Launcher's command-line arguments (from Epic's documentation of the launcher's exchange-code
// login: -AUTH_LOGIN, -AUTH_PASSWORD, -AUTH_TYPE, -epicapp, -epicenv, -EpicPortal, -epicusername, -epicuserid,
// -epiclocale, -epicsandboxid). Our own parser.
#include "okay/Platform/Epic/EpicService.hpp"

#include <cctype>

namespace okay {

namespace {
std::string Lower(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}
// "-name=value" / "--name=value" -> ("name", "value", true); "-name" -> ("name", "", false).
bool SplitArg(const std::string& a, std::string& name, std::string& value, bool& hasValue) {
    if (a.size() < 2 || a[0] != '-') return false;
    std::size_t start = a[1] == '-' ? 2 : 1;
    const std::size_t eq = a.find('=', start);
    name = Lower(a.substr(start, eq == std::string::npos ? std::string::npos : eq - start));
    hasValue = eq != std::string::npos;
    value = hasValue ? a.substr(eq + 1) : std::string();
    return !name.empty();
}
// The arguments whose value is a login secret.
bool IsSecretName(const std::string& lowerName) { return lowerName == "auth_password"; }
}  // namespace

EpicLaunchArgs ParseEpicLaunchArgs(const std::vector<std::string>& args) {
    EpicLaunchArgs out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string name, value; bool hasValue = false;
        if (!SplitArg(args[i], name, value, hasValue)) continue;
        std::string* dst = nullptr;
        if      (name == "auth_password")  dst = &out.exchangeCode;
        else if (name == "auth_type")      dst = &out.authType;
        else if (name == "epicapp")        dst = &out.appId;
        else if (name == "epicenv")        dst = &out.environment;
        else if (name == "epicuserid")     dst = &out.userId;
        else if (name == "epicusername")   dst = &out.userName;
        else if (name == "epiclocale")     dst = &out.locale;
        else if (name == "epicsandboxid")  dst = &out.sandboxId;
        else if (name == "epicportal")     { out.fromLauncher = true; continue; }
        if (!dst) continue;
        if (!hasValue && i + 1 < args.size() && !(args[i + 1].size() > 1 && args[i + 1][0] == '-')) { value = args[++i]; hasValue = true; }
        if (hasValue) *dst = value;
    }
    if (!out.exchangeCode.empty()) out.fromLauncher = true;
    return out;
}

EpicLaunchArgs ParseEpicLaunchArgs(int argc, char** argv) {
    std::vector<std::string> v;
    for (int i = 0; i < argc; ++i) v.emplace_back(argv[i] ? argv[i] : "");
    return ParseEpicLaunchArgs(v);
}

std::vector<std::string> RedactEpicArgs(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string name, value; bool hasValue = false;
        if (!SplitArg(args[i], name, value, hasValue) || !IsSecretName(name)) { out.push_back(args[i]); continue; }
        if (hasValue) { out.push_back(args[i].substr(0, args[i].find('=') + 1) + "***"); continue; }
        out.push_back(args[i]);
        if (i + 1 < args.size() && !(args[i + 1].size() > 1 && args[i + 1][0] == '-')) { out.push_back("***"); ++i; }
    }
    return out;
}

}  // namespace okay
