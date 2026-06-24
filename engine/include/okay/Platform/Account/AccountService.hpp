// OkaySpace account service — a small client-side account system for signing
// players in and out. It is used both by the launcher (its Account tab) and by
// the engine, which exposes a process-wide instance (okay::Account) and the
// account_* OkayScript builtins so games can sign players in from script.
//
// It is built around a single AccountService that supports two backends,
// chosen at runtime by whether a server URL is configured:
//
//   * Remote  — when a server URL is set, Register/Login POST JSON over HTTPS
//               (via the system `curl`, the same approach the launcher already
//               uses for the updater) and read back a session token. This is
//               the real "online account" path; point it at your auth server.
//
//   * Local   — when no server URL is set, accounts live in a small on-disk
//               database next to the launcher's config. Passwords are never
//               stored in the clear: each one is salted and stretched with
//               SHA-256 before being written. This lets the feature work out
//               of the box (and offline) until a server is wired up.
//
// The header is dependency-free (only the C++17 standard library + a bundled
// public-domain SHA-256) so the launcher can use it without extra build glue.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace okay::account {

namespace fs = std::filesystem;

// ===========================================================================
// SHA-256 (public domain, compact). Used to hash passwords for the local
// backend so credentials are never persisted in plaintext.
// ===========================================================================
namespace detail {

class Sha256 {
public:
    Sha256() { Reset(); }

    void Update(const std::uint8_t* data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            buffer_[bufLen_++] = data[i];
            if (bufLen_ == 64) { Block(buffer_.data()); bitLen_ += 512; bufLen_ = 0; }
        }
    }
    void Update(const std::string& s) {
        Update(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    std::array<std::uint8_t, 32> Digest() {
        std::uint64_t totalBits = bitLen_ + std::uint64_t(bufLen_) * 8;
        std::uint8_t pad = 0x80;
        Update(&pad, 1);
        std::uint8_t zero = 0x00;
        while (bufLen_ != 56) Update(&zero, 1);
        for (int i = 7; i >= 0; --i) {
            std::uint8_t b = std::uint8_t((totalBits >> (i * 8)) & 0xff);
            Update(&b, 1);
        }
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = std::uint8_t((h_[i] >> 24) & 0xff);
            out[i * 4 + 1] = std::uint8_t((h_[i] >> 16) & 0xff);
            out[i * 4 + 2] = std::uint8_t((h_[i] >> 8) & 0xff);
            out[i * 4 + 3] = std::uint8_t(h_[i] & 0xff);
        }
        Reset();
        return out;
    }

private:
    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t   bufLen_ = 0;
    std::uint64_t bitLen_ = 0;

    void Reset() {
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        bufLen_ = 0;
        bitLen_ = 0;
    }

    static std::uint32_t Ror(std::uint32_t x, std::uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    void Block(const std::uint8_t* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
                   (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        std::uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            std::uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }
};

inline std::string ToHex(const std::uint8_t* data, std::size_t len) {
    static const char* hx = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(hx[data[i] >> 4]);
        out.push_back(hx[data[i] & 0xf]);
    }
    return out;
}

inline std::string Sha256Hex(const std::string& s) {
    Sha256 h;
    h.Update(s);
    auto d = h.Digest();
    return ToHex(d.data(), d.size());
}

// Salt + iterate so a leaked database can't be reversed with a quick lookup.
inline std::string HashPassword(const std::string& password, const std::string& saltHex) {
    std::string acc = saltHex + ":" + password;
    for (int i = 0; i < 50000; ++i) acc = Sha256Hex(acc);
    return acc;
}

inline std::string RandomHex(std::size_t bytes) {
    std::random_device rd;
    std::mt19937_64 gen(((std::uint64_t)rd() << 32) ^ rd());
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<std::uint8_t> buf(bytes);
    for (auto& b : buf) b = std::uint8_t(dist(gen));
    return ToHex(buf.data(), buf.size());
}

// Constant-time string compare so a local login can't be timed.
inline bool SecureEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= (unsigned)(a[i] ^ b[i]);
    return diff == 0;
}

// Run a shell command and wait. On Windows this avoids std::system so no
// console window flashes for each curl request (CREATE_NO_WINDOW); elsewhere it
// is std::system. Returns the process exit code (-1 on spawn failure).
inline int RunCommand(const std::string& cmd) {
#if defined(_WIN32)
    std::string full = "cmd /c " + cmd;
    std::vector<char> buf(full.begin(), full.end()); buf.push_back('\0');
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
#else
    return std::system(cmd.c_str());
#endif
}

// Minimal JSON helpers — just enough to send credentials and read a token back.
inline std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;       break;
        }
    }
    return out;
}

// Pull the string value of a top-level "key" out of a flat JSON object.
inline std::string JsonField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    std::size_t k = json.find(needle);
    if (k == std::string::npos) return {};
    std::size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return {};
    std::size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    std::string out;
    for (std::size_t i = q1 + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) { out += json[++i]; continue; }
        if (c == '"') break;
        out += c;
    }
    return out;
}

// Pull an array of strings for a top-level "key" (e.g. {"keys":["a","b"]}).
inline std::vector<std::string> JsonStringArray(const std::string& json,
                                                const std::string& key) {
    std::vector<std::string> out;
    std::string needle = "\"" + key + "\"";
    std::size_t k = json.find(needle);
    if (k == std::string::npos) return out;
    std::size_t lb = json.find('[', k + needle.size());
    if (lb == std::string::npos) return out;
    std::size_t rb = json.find(']', lb);
    if (rb == std::string::npos) rb = json.size();
    std::size_t i = lb + 1;
    while (i < rb) {
        std::size_t q1 = json.find('"', i);
        if (q1 == std::string::npos || q1 >= rb) break;
        std::string s;
        std::size_t j = q1 + 1;
        for (; j < json.size(); ++j) {
            char c = json[j];
            if (c == '\\' && j + 1 < json.size()) { s += json[++j]; continue; }
            if (c == '"') break;
            s += c;
        }
        out.push_back(s);
        i = j + 1;
    }
    return out;
}

// Collect the string value of EVERY occurrence of "key":"value" — handy for a
// PostgREST array of objects like [{"key":"a"},{"key":"b"}] -> ["a","b"].
inline std::vector<std::string> JsonFieldValues(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    std::string needle = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        std::size_t colon = json.find(':', pos + needle.size());
        pos += needle.size();
        if (colon == std::string::npos) break;
        std::size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) break;
        std::string s;
        std::size_t j = q1 + 1;
        for (; j < json.size(); ++j) {
            char c = json[j];
            if (c == '\\' && j + 1 < json.size()) { s += json[++j]; continue; }
            if (c == '"') break;
            s += c;
        }
        out.push_back(s);
        pos = j + 1;
    }
    return out;
}

// Collect the numeric value of every "key":<number> occurrence (unquoted), for a
// PostgREST array like [{"score":500},{"score":300}] -> [500, 300].
inline std::vector<long> JsonNumberValues(const std::string& json, const std::string& key) {
    std::vector<long> out;
    std::string needle = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        std::size_t colon = json.find(':', pos + needle.size());
        pos += needle.size();
        if (colon == std::string::npos) break;
        std::size_t i = colon + 1;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
        std::size_t start = i;
        if (i < json.size() && (json[i] == '-' || json[i] == '+')) ++i;
        while (i < json.size() && std::isdigit((unsigned char)json[i])) ++i;
        if (i > start) { try { out.push_back(std::stol(json.substr(start, i - start))); } catch (...) {} }
        pos = i;
    }
    return out;
}

} // namespace detail

// ===========================================================================
// Public types
// ===========================================================================

/// The currently signed-in player. `loggedIn` is false when nobody is signed
/// in; `token` is an opaque session token (issued by the server, or generated
/// locally for the offline backend).
struct Session {
    bool        loggedIn = false;
    std::string username;
    std::string token;
};

/// Result of a register/login attempt. On failure `error` carries a short,
/// human-readable reason suitable for showing in the UI.
struct Result {
    bool        ok = false;
    std::string error;
    Session     session;
};

/// Result of an authenticated request to the account server (see Api). `ok` is
/// true for a 2xx response; `reached` is false when the server couldn't be
/// contacted at all (offline / DNS / TLS). `status` is the HTTP status code
/// (0 when not reached) and `body` is the raw response body.
struct ApiResponse {
    bool        ok      = false;
    bool        reached = false;
    long        status  = 0;
    std::string body;
};

/// One row of a server leaderboard: the player's name, their score, and their
/// rank (1 = top).
struct ScoreEntry {
    std::string name;
    long        score = 0;
    int         rank  = 0;
};

/// Which backend signs players in.
///   Auto     — pick from the configuration: Local when no server URL is set,
///              Supabase when an API key is also set, otherwise Custom.
///   Local    — on-device accounts (a dev fallback; not a real server).
///   Custom   — our own auth server (the reference server's /register, /login).
///   Supabase — a hosted Supabase project (auth/v1 REST + anon apikey).
enum class Provider { Auto, Local, Custom, Supabase };

/// A client-side account service. Construct it with a config directory (where a
/// saved session and any local accounts live) and, for online use, a server URL
/// (+ an API key for managed providers like Supabase). With nothing configured
/// it falls back to local, on-device accounts for development.
class AccountService {
public:
    explicit AccountService(fs::path configDir, std::string serverUrl = {},
                            std::string apiKey = {}, Provider provider = Provider::Auto)
        : configDir_(std::move(configDir)), serverUrl_(std::move(serverUrl)),
          apiKey_(std::move(apiKey)), provider_(Resolve(provider, serverUrl_, apiKey_)) {
        std::error_code ec;
        fs::create_directories(configDir_, ec);
        LoadSession();
    }

    /// True when configured to talk to a real (non-local) backend.
    bool IsOnline() const { return provider_ != Provider::Local; }
    Provider GetProvider() const { return provider_; }
    /// Identifier of the active backend ("local", "custom", or "supabase").
    const char* ProviderName() const {
        switch (provider_) {
            case Provider::Supabase: return "supabase";
            case Provider::Custom:   return "custom";
            default:                 return "local";
        }
    }
    /// Whether the backend identifies players by email (Supabase) rather than a
    /// freeform username (local / custom). Lets the UI label the field.
    bool UsesEmail() const { return provider_ == Provider::Supabase; }
    const std::string& ServerUrl() const { return serverUrl_; }

    const Session& CurrentSession() const { return session_; }
    bool IsLoggedIn() const { return session_.loggedIn; }

    /// Create a new account (2-arg form: no separate display name).
    Result Register(const std::string& id, const std::string& password) {
        return Register(id, password, std::string{});
    }
    /// Create a new account. For Supabase `id` is the email and `username` is an
    /// optional display name (stored in user metadata); for local/custom backends
    /// `id` is the username and `username` is ignored.
    Result Register(const std::string& id, const std::string& password,
                    const std::string& username) {
        std::string err = Validate(id, password);
        if (err.empty() && !username.empty() && (username.size() < 2 || username.size() > 32))
            err = "Username must be 2–32 characters.";
        if (!err.empty()) return Fail(err);
        switch (provider_) {
            case Provider::Supabase: return SupabaseAuth(true, id, password, username);
            case Provider::Custom:   return RemoteAuth("register", id, password);
            default:                 return LocalRegister(id, password);
        }
    }

    /// Sign in to an existing account.
    Result Login(const std::string& username, const std::string& password) {
        std::string err = Validate(username, password);
        if (!err.empty()) return Fail(err);
        switch (provider_) {
            case Provider::Supabase: return SupabaseAuth(false, username, password);
            case Provider::Custom:   return RemoteAuth("login", username, password);
            default:                 return LocalLogin(username, password);
        }
    }

    /// Sign out and forget the saved session.
    void Logout() {
        session_ = {};
        std::error_code ec;
        fs::remove(SessionPath(), ec);
    }

    /// Send a password-reset email (Supabase only). The user follows the emailed
    /// link to set a new password. Returns ok when the request was accepted.
    Result RequestPasswordReset(const std::string& email) {
        if (provider_ != Provider::Supabase)
            return Fail("Password reset needs the online (Supabase) account server.");
        if (!LooksLikeEmail(email)) return Fail("Enter a valid email address.");
        std::string base = serverUrl_;
        if (!base.empty() && base.back() == '/') base.pop_back();
        ApiResponse r = HttpRequest("POST", base + "/auth/v1/recover",
                                    "{\"email\":\"" + detail::JsonEscape(email) + "\"}", {});
        if (!r.reached) return Fail("Couldn't reach the account server.");
        if (!r.ok) return Fail("Couldn't start password reset (HTTP " + std::to_string(r.status) + ").");
        Result ok; ok.ok = true; return ok;
    }

    /// Change the signed-in user's password. Supabase: PUT auth/v1/user with the
    /// bearer token. Local: re-hash. Requires being signed in.
    Result ChangePassword(const std::string& newPassword) {
        if (!session_.loggedIn) return Fail("Sign in first.");
        if (newPassword.size() < 6) return Fail("Password must be at least 6 characters.");
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Api("/auth/v1/user", "PUT",
                "{\"password\":\"" + detail::JsonEscape(newPassword) + "\"}");
            if (!r.reached) return Fail("Couldn't reach the account server.");
            if (!r.ok) {
                std::string e = detail::JsonField(r.body, "msg");
                if (e.empty()) e = detail::JsonField(r.body, "message");
                return Fail(e.empty() ? "Password change failed." : e);
            }
            Result ok; ok.ok = true; ok.session = session_; return ok;
        }
        if (provider_ == Provider::Local) {
            auto records = ReadDb();
            std::string key = Lower(session_.username);
            for (auto& rec : records) {
                if (Lower(rec.user) != key) continue;
                rec.salt = detail::RandomHex(16);
                rec.hash = detail::HashPassword(newPassword, rec.salt);
                WriteDb(records);
                Result ok; ok.ok = true; ok.session = session_; return ok;
            }
            return Fail("Account not found.");
        }
        return Fail("Changing the password isn't supported for this server.");
    }

    /// Change the signed-in user's email (Supabase only). Supabase may send a
    /// confirmation link to the new address before it takes effect.
    Result ChangeEmail(const std::string& newEmail) {
        if (!session_.loggedIn) return Fail("Sign in first.");
        if (!LooksLikeEmail(newEmail)) return Fail("Enter a valid email address.");
        if (provider_ != Provider::Supabase)
            return Fail("Email change needs the online (Supabase) account server.");
        ApiResponse r = Api("/auth/v1/user", "PUT",
            "{\"email\":\"" + detail::JsonEscape(newEmail) + "\"}");
        if (!r.reached) return Fail("Couldn't reach the account server.");
        if (!r.ok) {
            std::string e = detail::JsonField(r.body, "msg");
            if (e.empty()) e = detail::JsonField(r.body, "message");
            return Fail(e.empty() ? "Email change failed." : e);
        }
        Result ok; ok.ok = true; ok.session = session_; return ok;
    }

    /// Change the signed-in user's display username. Supabase stores it in user
    /// metadata; the local backend renames the account. Updates the session.
    Result ChangeUsername(const std::string& newUsername) {
        if (!session_.loggedIn) return Fail("Sign in first.");
        if (newUsername.size() < 2 || newUsername.size() > 32)
            return Fail("Username must be 2–32 characters.");
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Api("/auth/v1/user", "PUT",
                "{\"data\":{\"username\":\"" + detail::JsonEscape(newUsername) + "\"}}");
            if (!r.reached) return Fail("Couldn't reach the account server.");
            if (!r.ok) {
                std::string e = detail::JsonField(r.body, "msg");
                if (e.empty()) e = detail::JsonField(r.body, "message");
                return Fail(e.empty() ? "Username change failed." : e);
            }
            session_.username = newUsername;   // display name
            SaveSession();
            Result ok; ok.ok = true; ok.session = session_; return ok;
        }
        if (provider_ == Provider::Local) {
            auto records = ReadDb();
            std::string newKey = Lower(newUsername);
            for (const auto& rec : records)            // must stay unique
                if (Lower(rec.user) == newKey && Lower(rec.user) != Lower(session_.username))
                    return Fail("That username is already taken.");
            std::string key = Lower(session_.username);
            for (auto& rec : records) {
                if (Lower(rec.user) != key) continue;
                rec.user = newUsername;
                WriteDb(records);
                session_.username = newUsername;
                SaveSession();
                Result ok; ok.ok = true; ok.session = session_; return ok;
            }
            return Fail("Account not found.");
        }
        return Fail("Changing the username isn't supported for this server.");
    }

    /// Make an authenticated request to the account server, attaching the
    /// current session token as `Authorization: Bearer <token>`. `path` is the
    /// part after the server URL (e.g. "/profile"); `method` defaults to GET.
    /// `jsonBody`, when non-empty, is sent as an application/json body.
    ///
    /// Use this to build server features on top of accounts (cloud saves,
    /// profiles, ...). Returns a not-reached response for the local backend or
    /// when no one is signed in.
    ApiResponse Api(const std::string& path, const std::string& method = "GET",
                    const std::string& jsonBody = {}, const std::string& extraHeader = {}) const {
        if (!IsOnline() || !session_.loggedIn) return {};
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += path.empty() || path.front() == '/' ? path : ("/" + path);
        return HttpRequest(method, url, jsonBody, /*authToken=*/session_.token, extraHeader);
    }

    /// Confirm the saved session is still valid with the server (GET /verify
    /// with the bearer token). On a definitive rejection (401/403) the local
    /// session is cleared so the player is signed out. Network errors leave the
    /// session intact (so being offline doesn't sign players out). The local
    /// backend has nothing to check, so it just reports whether someone is in.
    bool VerifySession() {
        if (!session_.loggedIn) return false;
        if (!IsOnline()) return true;
        // Supabase exposes the signed-in user at auth/v1/user; our custom server
        // uses /verify.
        ApiResponse r = Api(provider_ == Provider::Supabase ? "/auth/v1/user" : "/verify");
        if (!r.reached) return true;                 // offline: keep the session
        if (r.status == 401 || r.status == 403) {    // token revoked/expired
            Logout();
            return false;
        }
        return r.ok;
    }

    /// Verify an ARBITRARY access token (not our own session) and, on success, hand
    /// back the account id it belongs to. This is what a multiplayer host uses to
    /// authenticate a joining client's Supabase token — wire it into
    /// NetworkManager::SetTokenVerifier. Supabase: GET auth/v1/user with the token as
    /// Bearer ({"id": "<uuid>", ...}); custom server: GET /verify with the token.
    /// Returns false for the local backend, offline, or an empty/invalid token.
    bool VerifyToken(const std::string& token, std::string& outUserId) const {
        outUserId.clear();
        if (token.empty() || !IsOnline()) return false;
        std::string base = serverUrl_;
        if (!base.empty() && base.back() == '/') base.pop_back();
        if (provider_ == Provider::Supabase) {
            ApiResponse r = HttpRequest("GET", base + "/auth/v1/user", {}, token);
            if (!r.ok) return false;
            outUserId = detail::JsonField(r.body, "id");
            return !outUserId.empty();
        }
        return HttpRequest("GET", base + "/verify", {}, token).ok;   // custom server
    }

    // ---- Cloud saves ---------------------------------------------------
    // Per-account key/value storage on the server, so progress follows the
    // player across devices. `key` names a save slot ("save1", "settings");
    // `data` is arbitrary text (your serialized save). These require the online
    // backend and a signed-in player; on the local backend they no-op (Save/
    // Delete return false, Load returns "", List returns empty).
    //
    // Wire protocol (see the reference server):
    //   POST   /cloud/<key>   {"data": "..."}   -> 200
    //   GET    /cloud/<key>                       -> 200 {"data": "..."} | 404
    //   DELETE /cloud/<key>                       -> 200
    //   GET    /cloud                             -> 200 {"keys": [...]}
    bool CloudSave(const std::string& key, const std::string& data) {
        if (!CloudKeyOk(key)) return false;
        if (provider_ == Provider::Supabase)   // upsert a row in the cloud_saves table
            return Api("/rest/v1/cloud_saves", "POST",
                       "[{\"key\":\"" + detail::JsonEscape(key) + "\",\"data\":\"" +
                       detail::JsonEscape(data) + "\"}]",
                       "Prefer: resolution=merge-duplicates,return=minimal").ok;
        return Api("/cloud/" + key, "POST",
                   "{\"data\":\"" + detail::JsonEscape(data) + "\"}").ok;
    }
    std::string CloudLoad(const std::string& key) {
        if (!CloudKeyOk(key)) return {};
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Api("/rest/v1/cloud_saves?select=data&key=eq." + key, "GET");
            return r.ok ? detail::JsonField(r.body, "data") : std::string{};  // [{"data":...}]
        }
        ApiResponse r = Api("/cloud/" + key, "GET");
        return r.ok ? detail::JsonField(r.body, "data") : std::string{};
    }
    bool CloudHas(const std::string& key) {
        if (!CloudKeyOk(key)) return false;
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Api("/rest/v1/cloud_saves?select=key&key=eq." + key, "GET");
            return r.ok && r.body.find('{') != std::string::npos;  // non-empty result array
        }
        return Api("/cloud/" + key, "GET").ok;
    }
    bool CloudDelete(const std::string& key) {
        if (!CloudKeyOk(key)) return false;
        if (provider_ == Provider::Supabase)
            return Api("/rest/v1/cloud_saves?key=eq." + key, "DELETE").ok;
        return Api("/cloud/" + key, "DELETE").ok;
    }
    std::vector<std::string> CloudList() {
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Api("/rest/v1/cloud_saves?select=key", "GET");
            return r.ok ? detail::JsonFieldValues(r.body, "key") : std::vector<std::string>{};
        }
        ApiResponse r = Api("/cloud", "GET");
        return r.ok ? detail::JsonStringArray(r.body, "keys") : std::vector<std::string>{};
    }

    // ---- Leaderboards --------------------------------------------------
    // Global high-score tables keyed by a board name. Submitting keeps the
    // player's best. Requires the online backend + a signed-in player; no-ops
    // on the local backend (Submit returns false, Top returns empty).
    //
    // Wire protocol (see the reference server):
    //   POST /leaderboard/<board>          {"score": N}   -> 200
    //   GET  /leaderboard/<board>?count=N                 -> 200
    //        {"entries": ["1,alice,500", "2,bob,300", ...]}  (rank,name,score)
    bool LeaderboardSubmit(const std::string& board, long score) {
        if (!CloudKeyOk(board)) return false;
        if (provider_ == Provider::Supabase)   // upsert (board,user); a DB trigger keeps the best
            return Api("/rest/v1/leaderboards", "POST",
                       "[{\"board\":\"" + detail::JsonEscape(board) + "\",\"name\":\"" +
                       detail::JsonEscape(session_.username) + "\",\"score\":" +
                       std::to_string(score) + "}]",
                       "Prefer: resolution=merge-duplicates,return=minimal").ok;
        return Api("/leaderboard/" + board, "POST",
                   "{\"score\":" + std::to_string(score) + "}").ok;
    }
    std::vector<ScoreEntry> LeaderboardTop(const std::string& board, int count = 10) {
        std::vector<ScoreEntry> out;
        if (!CloudKeyOk(board)) return out;
        if (provider_ == Provider::Supabase) {
            ApiResponse r = Api("/rest/v1/leaderboards?select=name,score&board=eq." + board +
                                "&order=score.desc&limit=" + std::to_string(count), "GET");
            if (!r.ok) return out;
            auto names  = detail::JsonFieldValues(r.body, "name");
            auto scores = detail::JsonNumberValues(r.body, "score");
            for (std::size_t i = 0; i < names.size() && i < scores.size(); ++i) {
                ScoreEntry e; e.name = names[i]; e.score = scores[i]; e.rank = (int)i + 1;
                out.push_back(std::move(e));
            }
            return out;
        }
        ApiResponse r = Api("/leaderboard/" + board + "?count=" + std::to_string(count), "GET");
        if (!r.ok) return out;
        for (const std::string& s : detail::JsonStringArray(r.body, "entries")) {
            // "rank,name,score" — names are usernames, so they carry no commas.
            std::size_t c1 = s.find(',');
            std::size_t c2 = s.rfind(',');
            if (c1 == std::string::npos || c2 == c1) continue;
            ScoreEntry e;
            try { e.rank = std::stoi(s.substr(0, c1)); } catch (...) {}
            e.name = s.substr(c1 + 1, c2 - c1 - 1);
            try { e.score = std::stol(s.substr(c2 + 1)); } catch (...) {}
            out.push_back(std::move(e));
        }
        return out;
    }

private:
    // Save keys live in a URL path, so keep them to a safe, simple alphabet.
    static bool CloudKeyOk(const std::string& key) {
        if (key.empty() || key.size() > 64) return false;
        for (char c : key)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                return false;
        return true;
    }

    fs::path    configDir_;
    std::string serverUrl_;
    std::string apiKey_;
    Provider    provider_;
    Session     session_;

    fs::path SessionPath() const { return configDir_ / "session.txt"; }
    fs::path DbPath() const { return configDir_ / "accounts.db"; }

    // Turn Provider::Auto into a concrete backend from what's configured.
    static Provider Resolve(Provider p, const std::string& url, const std::string& key) {
        if (p != Provider::Auto) return p;
        if (url.empty()) return Provider::Local;
        return key.empty() ? Provider::Custom : Provider::Supabase;
    }

    static std::string Lower(std::string s) {
        for (char& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    }

    static Result Fail(const std::string& msg) {
        Result r; r.ok = false; r.error = msg; return r;
    }

    static bool LooksLikeEmail(const std::string& s) {
        std::size_t at = s.find('@');
        if (at == std::string::npos || at == 0) return false;
        std::size_t dot = s.find('.', at);
        return dot != std::string::npos && dot + 1 < s.size();
    }

    std::string Validate(const std::string& username, const std::string& password) const {
        if (provider_ == Provider::Supabase) {
            if (!LooksLikeEmail(username)) return "Enter a valid email address.";
        } else {
            if (username.size() < 3) return "Username must be at least 3 characters.";
            if (username.size() > 32) return "Username is too long (max 32).";
            for (char c : username)
                if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                    return "Username may use letters, digits, '_', '-', '.' only.";
        }
        if (password.size() < 6) return "Password must be at least 6 characters.";
        return {};
    }

    // ---- session persistence ------------------------------------------
    void LoadSession() {
        std::ifstream f(SessionPath());
        if (!f) return;
        std::string user, token;
        std::getline(f, user);
        std::getline(f, token);
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
        };
        trim(user); trim(token);
        if (!user.empty() && !token.empty()) {
            session_.loggedIn = true;
            session_.username = user;
            session_.token    = token;
        }
    }

    void SaveSession() {
        std::ofstream f(SessionPath(), std::ios::trunc);
        f << session_.username << "\n" << session_.token << "\n";
    }

    Result Succeed(const std::string& username, const std::string& token) {
        session_.loggedIn = true;
        session_.username = username;
        session_.token    = token;
        SaveSession();
        Result r; r.ok = true; r.session = session_; return r;
    }

    // ---- local backend -------------------------------------------------
    // Each record is one line: "username<TAB>saltHex<TAB>hashHex".
    struct Record { std::string user, salt, hash; };

    std::vector<Record> ReadDb() const {
        std::vector<Record> out;
        std::ifstream f(DbPath());
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::size_t t1 = line.find('\t');
            std::size_t t2 = line.find('\t', t1 == std::string::npos ? t1 : t1 + 1);
            if (t1 == std::string::npos || t2 == std::string::npos) continue;
            Record r;
            r.user = line.substr(0, t1);
            r.salt = line.substr(t1 + 1, t2 - t1 - 1);
            r.hash = line.substr(t2 + 1);
            while (!r.hash.empty() && (r.hash.back() == '\r' || r.hash.back() == '\n'))
                r.hash.pop_back();
            out.push_back(std::move(r));
        }
        return out;
    }

    void WriteDb(const std::vector<Record>& records) const {
        std::ofstream f(DbPath(), std::ios::trunc);
        for (const auto& r : records)
            f << r.user << '\t' << r.salt << '\t' << r.hash << '\n';
    }

    Result LocalRegister(const std::string& username, const std::string& password) {
        auto records = ReadDb();
        std::string key = Lower(username);
        for (const auto& r : records)
            if (Lower(r.user) == key) return Fail("That username is already taken.");
        Record r;
        r.user = username;
        r.salt = detail::RandomHex(16);
        r.hash = detail::HashPassword(password, r.salt);
        records.push_back(r);
        WriteDb(records);
        return Succeed(username, detail::RandomHex(24));
    }

    Result LocalLogin(const std::string& username, const std::string& password) {
        auto records = ReadDb();
        std::string key = Lower(username);
        for (const auto& r : records) {
            if (Lower(r.user) != key) continue;
            std::string attempt = detail::HashPassword(password, r.salt);
            if (detail::SecureEquals(attempt, r.hash))
                return Succeed(r.user, detail::RandomHex(24));
            return Fail("Incorrect password.");
        }
        return Fail("No account found with that username.");
    }

    // ---- remote backend ------------------------------------------------
    // POST {"username":..,"password":..} to <serverUrl>/<action>; a 2xx
    // response is expected to contain a "token" field.
    Result RemoteAuth(const std::string& action, const std::string& username,
                      const std::string& password) {
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += "/" + action;

        std::string body = "{\"username\":\"" + detail::JsonEscape(username) +
                           "\",\"password\":\"" + detail::JsonEscape(password) + "\"}";
        // No bearer token yet — this is how the player gets one.
        ApiResponse r = HttpRequest("POST", url, body, /*authToken=*/{});

        if (!r.reached)
            return Fail("Couldn't reach the account server (offline or unreachable).");
        if (!r.ok) {
            std::string serverErr = detail::JsonField(r.body, "error");
            return Fail(serverErr.empty()
                ? ("Account server rejected the request (HTTP " + std::to_string(r.status) + ").")
                : serverErr);
        }
        std::string token = detail::JsonField(r.body, "token");
        if (token.empty()) return Fail("Server response was missing a session token.");
        return Succeed(username, token);
    }

    // ---- managed backend: Supabase (auth/v1 REST) ----------------------
    // Sign up:  POST <url>/auth/v1/signup            {"email","password"}
    // Sign in:  POST <url>/auth/v1/token?grant_type=password {"email","password"}
    // Both take the project's anon key as the `apikey` header (added by
    // HttpRequest) and return {"access_token": "...", "user": {...}} on success,
    // or an error object on failure.
    Result SupabaseAuth(bool signUp, const std::string& email, const std::string& password,
                        const std::string& username = {}) {
        std::string base = serverUrl_;
        if (!base.empty() && base.back() == '/') base.pop_back();
        std::string url = base + (signUp ? "/auth/v1/signup"
                                         : "/auth/v1/token?grant_type=password");
        std::string body = "{\"email\":\"" + detail::JsonEscape(email) +
                           "\",\"password\":\"" + detail::JsonEscape(password) + "\"";
        if (signUp && !username.empty())   // store the display name in user metadata
            body += ",\"data\":{\"username\":\"" + detail::JsonEscape(username) + "\"}";
        body += "}";
        ApiResponse r = HttpRequest("POST", url, body, /*authToken=*/{});

        if (!r.reached)
            return Fail("Couldn't reach the account server (offline or unreachable).");
        if (!r.ok) {
            // Supabase reports errors as error_description / msg / message / error.
            std::string e = detail::JsonField(r.body, "error_description");
            if (e.empty()) e = detail::JsonField(r.body, "msg");
            if (e.empty()) e = detail::JsonField(r.body, "message");
            if (e.empty()) e = detail::JsonField(r.body, "error");
            return Fail(e.empty()
                ? ("Account server rejected the request (HTTP " + std::to_string(r.status) + ").")
                : e);
        }
        std::string token = detail::JsonField(r.body, "access_token");
        if (token.empty()) {
            // Sign-up with email confirmation enabled returns no session yet.
            if (signUp)
                return Fail("Account created — check your email to confirm, then sign in. "
                            "(Turn off email confirmation in Supabase for instant sign-in.)");
            return Fail("Server response was missing an access token.");
        }
        // Display the username from user metadata when present; else the email.
        std::string display = detail::JsonField(r.body, "username");
        return Succeed(display.empty() ? email : display, token);
    }

    // Perform one HTTP request via the system `curl` so the launcher/engine need
    // no HTTP library and reuse the platform's TLS stack. Secrets (the request
    // body and the auth header) are passed through files, never argv, so they
    // don't show up in the process list. We avoid curl's -f/--fail because it
    // discards the response body on HTTP errors and we want the server's
    // {"error": ...} message; instead we capture the status code via -w.
    ApiResponse HttpRequest(const std::string& method, const std::string& url,
                            const std::string& jsonBody,
                            const std::string& authToken,
                            const std::string& extraHeader = {}) const {
        std::error_code ec;
        fs::path dir   = fs::temp_directory_path(ec);
        std::string id = detail::RandomHex(8);
        fs::path resp  = dir / ("okay_acct_" + id + ".out");
        fs::path code  = dir / ("okay_acct_" + id + ".code");
        fs::path bodyF = dir / ("okay_acct_" + id + ".json");
        fs::path cfgF  = dir / ("okay_acct_" + id + ".cfg");

        std::string cmd = "curl -s -X " + method +
            " -o \"" + resp.string() + "\" -w \"%{http_code}\"";

        // Headers/secrets go in a curl config file (-K), keeping them out of argv.
        std::string cfg;
        if (!apiKey_.empty())                          // managed backends (Supabase)
            cfg += "header = \"apikey: " + apiKey_ + "\"\n";
        if (!authToken.empty())
            cfg += "header = \"Authorization: Bearer " + authToken + "\"\n";
        if (!extraHeader.empty())                      // e.g. PostgREST "Prefer: ..."
            cfg += "header = \"" + extraHeader + "\"\n";
        if (!jsonBody.empty()) {
            std::ofstream(bodyF, std::ios::trunc) << jsonBody;
            cfg += "header = \"Content-Type: application/json\"\n";
            cmd += " --data-binary @\"" + bodyF.string() + "\"";
        }
        if (!cfg.empty()) {
            std::ofstream(cfgF, std::ios::trunc) << cfg;
            cmd += " -K \"" + cfgF.string() + "\"";
        }
        cmd += " \"" + url + "\" > \"" + code.string() + "\"";
#if !defined(_WIN32)
        cmd += " 2>/dev/null";
#endif
        int rc = detail::RunCommand(cmd);

        auto slurp = [](const fs::path& p) {
            std::ifstream r(p, std::ios::binary);
            std::stringstream ss; ss << r.rdbuf(); return ss.str();
        };
        ApiResponse out;
        out.body = slurp(resp);
        std::string codeS = slurp(code);
        fs::remove(resp, ec); fs::remove(code, ec);
        fs::remove(bodyF, ec); fs::remove(cfgF, ec);

        try { out.status = std::stol(codeS); } catch (...) { out.status = 0; }
        out.reached = (rc == 0 && out.status != 0);
        out.ok = out.reached && out.status >= 200 && out.status < 300;
        return out;
    }
};

/// Best-effort per-user config directory for launcher data: %APPDATA%/OkaySpace
/// on Windows, $XDG_CONFIG_HOME or ~/.config/OkaySpace elsewhere, falling back
/// to `fallbackBesideExe` if no home directory is known.
inline fs::path DefaultConfigDir(const fs::path& fallbackBesideExe) {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
        return fs::path(appdata) / "OkaySpace";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
        return fs::path(xdg) / "OkaySpace";
    if (const char* home = std::getenv("HOME"))
        return fs::path(home) / ".config" / "OkaySpace";
#endif
    return fallbackBesideExe;
}

} // namespace okay::account
