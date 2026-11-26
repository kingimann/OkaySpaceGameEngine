// OkayBackend: the one layer the programs use to reach online services (docs/platform/okay_backend.md). Entry header.
//
//   Transport.hpp  Request / Response, and where requests go (the network, a test hook, a recorder)
//   Auth.hpp       the Auth calls every provider answers, Result / Session, reading a token's claims
//   Database.hpp   the Database calls: Query (select, filters, order, limit, insert / upsert / update / delete) and Rpc
//   Supabase.hpp   the Supabase provider: the production backend, sending exactly the requests the programs always sent
//
// This file also picks the provider from configuration, so switching needs no change to the programs:
//   OKAY_BACKEND (environment variable) or "provider" in <config folder>/backend.json: "supabase", "local" or "auto".
//   The environment variable wins over the file. Nothing set (or an unknown word) means "auto", which keeps the rule the
//   programs always used: a configured address and key mean Supabase (the default every release ships with), no address
//   means the on-device accounts. "local" is today's on-device accounts; the Local backend proper comes in round 9.
//
// Header-only like the account service, and free of <mutex> / <thread> (CLAUDE.md rule 21a).
#pragma once

#include "okay/Backend/Transport.hpp"
#include "okay/Backend/Auth.hpp"
#include "okay/Backend/Database.hpp"
#include "okay/Backend/Supabase.hpp"
#include "okay/Platform/Account/AccountDetail.hpp"   // detail::JsonField (reading backend.json)

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace okay::backend {

/// Which provider serves the programs.
enum class ProviderKind { Auto, Supabase, Local };

inline const char* ProviderKindName(ProviderKind k) {
    switch (k) {
        case ProviderKind::Supabase: return "supabase";
        case ProviderKind::Local:    return "local";
        default:                     return "auto";
    }
}

/// "supabase", "local" or "auto" in any letter case, spaces and line ends around it ignored. Anything else is Auto, so a
/// typo never knocks a program off the production backend.
inline ProviderKind ParseProviderKind(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    s.erase(0, i);
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    if (s == "supabase") return ProviderKind::Supabase;
    if (s == "local") return ProviderKind::Local;
    return ProviderKind::Auto;
}

/// The backend configuration in force, and where the choice came from ("OKAY_BACKEND", "backend.json" or "default").
struct Config {
    ProviderKind provider = ProviderKind::Auto;
    std::string  source = "default";
};

/// Read the configuration: `envValue` (the OKAY_BACKEND variable, null when unset) wins; then "provider" in
/// `configDir`/backend.json; then Auto. A file over 64 KiB is not read (it is a few lines). Never throws.
inline Config LoadConfig(const std::filesystem::path& configDir, const char* envValue) {
    Config c;
    if (envValue && *envValue) {
        c.provider = ParseProviderKind(envValue);
        c.source = "OKAY_BACKEND";
        return c;
    }
    if (configDir.empty()) return c;
    std::error_code ec;
    const std::filesystem::path file = configDir / "backend.json";
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size > 64 * 1024) return c;
    std::ifstream f(file, std::ios::binary);
    if (!f) return c;
    std::stringstream ss; ss << f.rdbuf();
    const std::string value = okay::account::detail::JsonField(ss.str(), "provider");
    if (value.empty()) return c;
    c.provider = ParseProviderKind(value);
    c.source = "backend.json";
    return c;
}

/// LoadConfig with the real OKAY_BACKEND variable.
inline Config LoadConfig(const std::filesystem::path& configDir) {
    return LoadConfig(configDir, std::getenv("OKAY_BACKEND"));
}

} // namespace okay::backend
