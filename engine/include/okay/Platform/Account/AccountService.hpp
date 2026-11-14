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
//               stored in the clear: each one gets a random salt from the OS
//               and is stretched with PBKDF2-HMAC-SHA256 (600,000 rounds, the
//               hash text says which, so the cost can go up later). Accounts
//               made by older versions (a plain SHA-256 chain) still sign in
//               and are re-hashed the same way on that sign-in. This lets the
//               feature work out of the box (and offline) until a server is
//               wired up. It is a development fallback, not a real server.
//
// The header is dependency-free (only the C++17 standard library + a bundled
// public-domain SHA-256) so the launcher can use it without extra build glue.
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#endif
#include "okay/Platform/NativeHttp.hpp"

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

    /// PBKDF2 helper: `*this` has absorbed exactly one 64-byte block (an HMAC key pad). The digest of that block followed by the 32 bytes
    /// at `m`, in one compression, without touching `*this`.
    std::array<std::uint8_t, 32> FinishWith32(const std::uint8_t* m) const {
        std::uint8_t blk[64] = {0};
        for (int i = 0; i < 32; ++i) blk[i] = m[i];
        blk[32] = 0x80;
        blk[62] = 0x03; blk[63] = 0x00;                  // (64 + 32) bytes = 768 bits
        Sha256 t = *this;
        t.Block(blk);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = std::uint8_t((t.h_[i] >> 24) & 0xff);
            out[i * 4 + 1] = std::uint8_t((t.h_[i] >> 16) & 0xff);
            out[i * 4 + 2] = std::uint8_t((t.h_[i] >> 8) & 0xff);
            out[i * 4 + 3] = std::uint8_t(t.h_[i] & 0xff);
        }
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

// Random bytes from the operating system's CSPRNG (never a seeded Mersenne Twister): getrandom-style /dev/urandom on POSIX,
// RtlGenRandom (SystemFunction036) on Windows, found at run time so nothing extra is linked. False when the system has none.
inline bool OsRandom(std::uint8_t* out, std::size_t n) {
#if defined(_WIN32)
    typedef unsigned char (__stdcall *RtlGenRandomFn)(void*, unsigned long);
    static RtlGenRandomFn fn = [] {
        HMODULE m = LoadLibraryA("advapi32.dll");
        return m ? reinterpret_cast<RtlGenRandomFn>(reinterpret_cast<void*>(GetProcAddress(m, "SystemFunction036"))) : nullptr;
    }();
    return fn && fn(out, (unsigned long)n) != 0;
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(out), (std::streamsize)n);
    return (std::size_t)f.gcount() == n;
#endif
}

inline std::string RandomHex(std::size_t bytes) {
    std::vector<std::uint8_t> buf(bytes);
    if (!OsRandom(buf.data(), buf.size())) {      // no OS source: std::random_device per byte (still not a seeded generator)
        std::random_device rd;
        for (auto& b : buf) b = std::uint8_t(rd() & 0xff);
    }
    return ToHex(buf.data(), buf.size());
}

// ---- PBKDF2-HMAC-SHA256 (RFC 8018 / RFC 2898), one 32-byte block ----------------------------------------------------------------
inline std::vector<std::uint8_t> HexBytes(const std::string& hex) {
    std::vector<std::uint8_t> out;
    auto nib = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    if (hex.size() % 2) return {};
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int a = nib(hex[i]), b = nib(hex[i + 1]);
        if (a < 0 || b < 0) return {};
        out.push_back(std::uint8_t(a * 16 + b));
    }
    return out;
}
inline std::array<std::uint8_t, 32> Pbkdf2Sha256(const std::string& password, const std::vector<std::uint8_t>& salt, std::uint32_t iterations) {
    std::array<std::uint8_t, 64> key{};
    if (password.size() > 64) {
        Sha256 h; h.Update(password);
        const auto d = h.Digest();
        for (std::size_t i = 0; i < 32; ++i) key[i] = d[i];
    } else {
        for (std::size_t i = 0; i < password.size(); ++i) key[i] = std::uint8_t(password[i]);
    }
    std::array<std::uint8_t, 64> ipad{}, opad{};
    for (std::size_t i = 0; i < 64; ++i) { ipad[i] = key[i] ^ 0x36; opad[i] = key[i] ^ 0x5c; }
    Sha256 inner, outer;
    inner.Update(ipad.data(), 64);
    outer.Update(opad.data(), 64);
    // U1 = HMAC(P, salt || INT_32_BE(1))
    Sha256 first = inner;
    first.Update(salt.data(), salt.size());
    const std::uint8_t one[4] = {0, 0, 0, 1};
    first.Update(one, 4);
    const auto in1 = first.Digest();
    std::array<std::uint8_t, 32> u = outer.FinishWith32(in1.data());
    std::array<std::uint8_t, 32> t = u;
    for (std::uint32_t i = 1; i < iterations; ++i) {
        const auto inD = inner.FinishWith32(u.data());
        u = outer.FinishWith32(inD.data());
        for (std::size_t k = 0; k < 32; ++k) t[k] ^= u[k];
    }
    return t;
}

// The stored form: "$pbkdf2-sha256$<iterations>$<hash hex>" (the salt sits in its own column). A bare 64-hex string is the old
// SHA-256 chain (HashPassword). Raising kPasswordRounds later makes every sign-in with a weaker record re-hash it.
inline constexpr std::uint32_t kPasswordRounds = 600000;
inline constexpr std::uint32_t kMinRounds = 1000, kMaxRounds = 20000000;   // what a stored record may claim (a damaged file can't stall a login)
inline std::string HashPasswordV2(const std::string& password, const std::string& saltHex, std::uint32_t rounds = kPasswordRounds) {
    const std::vector<std::uint8_t> salt = HexBytes(saltHex);
    const auto d = Pbkdf2Sha256(password, salt, rounds);
    return "$pbkdf2-sha256$" + std::to_string(rounds) + "$" + ToHex(d.data(), d.size());
}
inline bool IsHashV2(const std::string& stored) { return stored.rfind("$pbkdf2-sha256$", 0) == 0; }
inline bool StoredRounds(const std::string& stored, std::uint32_t& rounds) {
    if (!IsHashV2(stored)) return false;
    const std::size_t a = 15, b = stored.find('$', a);
    if (b == std::string::npos || b == a || b - a > 9) return false;
    std::uint64_t n = 0;
    for (std::size_t i = a; i < b; ++i) { if (stored[i] < '0' || stored[i] > '9') return false; n = n * 10 + (std::uint64_t)(stored[i] - '0'); }
    if (n < kMinRounds || n > kMaxRounds) return false;
    rounds = (std::uint32_t)n;
    return true;
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

// The JSON string literal whose opening quote is at json[q], with its escapes
// decoded (\n, \t, ... and \uXXXX as UTF-8: Supabase's auth server writes "&",
// "<" and ">" as &, <, >). Returns the index just past the
// closing quote (json.size() if there is none).
inline std::size_t JsonReadString(const std::string& json, std::size_t q, std::string& out) {
    auto hex4 = [&](std::size_t at, unsigned& v) {
        if (at + 4 > json.size()) return false;
        v = 0;
        for (std::size_t k = at; k < at + 4; ++k) {
            const char h = json[k];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
            else return false;
        }
        return true;
    };
    std::size_t i = q + 1;
    for (; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') return i + 1;
        if (c != '\\' || i + 1 >= json.size()) { out += c; continue; }
        c = json[++i];
        switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = 0, lo = 0;
                if (!hex4(i + 1, cp)) { out += 'u'; break; }
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 2 < json.size() && json[i + 1] == '\\' && json[i + 2] == 'u' &&
                    hex4(i + 3, lo) && lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
                       out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                break;
            }
            default: out += c; break;   // \" \\ \/
        }
    }
    return i;
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
    JsonReadString(json, q1, out);
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
        i = JsonReadString(json, q1, s);
        out.push_back(s);
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
        pos = JsonReadString(json, q1, s);
        out.push_back(s);
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

/// RFC 3986 percent-encoding of everything but unreserved characters (for a query value).
inline std::string PercentEncode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
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
    std::string token;         // access token (short-lived for Supabase)
    std::string refreshToken;  // used to renew the access token (Supabase)
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

    /// Where the emailed reset link should land (the hosted page from
    /// supabase/auth-pages/reset-password, see docs/password-reset.md). Must be an
    /// http(s) URL that is also on the Supabase Auth redirect allow-list. Empty =
    /// not configured: Supabase then uses the project's Site URL instead.
    void SetResetRedirect(std::string url) { resetRedirect_ = std::move(url); }
    const std::string& ResetRedirect() const { return resetRedirect_; }
    bool HasResetRedirect() const { return resetRedirect_.rfind("http", 0) == 0; }

    /// The recover endpoint for `base` (+ optional redirect). Pure, so it is unit tested.
    static std::string RecoverUrl(std::string base, const std::string& redirect) {
        if (!base.empty() && base.back() == '/') base.pop_back();
        std::string u = base + "/auth/v1/recover";
        if (redirect.rfind("http", 0) == 0) u += "?redirect_to=" + detail::PercentEncode(redirect);
        return u;
    }

    /// Send a password-reset email (Supabase only). The user follows the emailed
    /// link to set a new password. Returns ok when the request was accepted.
    Result RequestPasswordReset(const std::string& email) {
        if (provider_ != Provider::Supabase)
            return Fail("Password reset needs the online (Supabase) account server.");
        if (!LooksLikeEmail(email)) return Fail("Enter a valid email address.");
        ApiResponse r = HttpRequest("POST", RecoverUrl(serverUrl_, resetRedirect_),
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
                rec.hash = detail::HashPasswordV2(newPassword, rec.salt);
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
        return ApiTimed(path, method, jsonBody, extraHeader, 0);
    }
    /// Api with a time limit in seconds (0 = none, as Api). A request that is not answered in time comes back not-reached, so a caller
    /// that must not hang (the join-time session ticket) can retry or give up. The limit is enforced by the curl fallback (Linux, macOS,
    /// and Windows when the native client is off); the native Windows client keeps WinHTTP's own timeouts.
    ApiResponse ApiTimed(const std::string& path, const std::string& method, const std::string& jsonBody,
                         const std::string& extraHeader, int timeoutSec) const {
        if (!IsOnline() || !session_.loggedIn) return {};
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += path.empty() || path.front() == '/' ? path : ("/" + path);
        ApiResponse r = HttpRequest(method, url, jsonBody, /*authToken=*/session_.token, extraHeader, {}, {}, timeoutSec);
        // Expired access token: the launcher refreshes it (RefreshIfExpiring) and
        // saves it; workers built from the saved session pick that up and retry
        // once. (Workers never refresh themselves: Supabase rotates refresh
        // tokens, and racing refreshes can revoke the whole session.)
        if (r.status == 401 && provider_ == Provider::Supabase && ReloadSessionToken())
            r = HttpRequest(method, url, jsonBody, /*authToken=*/session_.token, extraHeader, {}, {}, timeoutSec);
        return r;
    }

    /// Re-read the access token saved by another instance (same account).
    /// True when it changed.
    bool ReloadSessionToken() const {
        std::ifstream f(SessionPath());
        if (!f) return false;
        std::string user, token, refresh;
        std::getline(f, user); std::getline(f, token); std::getline(f, refresh);
        while (!token.empty() && (token.back() == '\r' || token.back() == '\n')) token.pop_back();
        while (!refresh.empty() && (refresh.back() == '\r' || refresh.back() == '\n')) refresh.pop_back();
        if (token.empty() || token == session_.token) return false;
        session_.token = token;
        if (!refresh.empty()) session_.refreshToken = refresh;
        return true;
    }

    /// The signed-in account's id (the JWT "sub"), "" when unknown. Profiles
    /// use the same id, so this finds MY profile even if the shown name differs.
    std::string UserId() const { return JwtClaim(session_.token, "sub"); }
    /// When the access token expires (unix seconds), 0 when unknown.
    long TokenExpiresAt() const { return std::atol(JwtClaim(session_.token, "exp").c_str()); }
    /// Renew the access token when it expires within `marginSec` (Supabase).
    /// Call from ONE place (the launcher's main session). True if refreshed.
    bool RefreshIfExpiring(long marginSec = 600) {
        if (provider_ != Provider::Supabase || !session_.loggedIn) return false;
        const long exp = TokenExpiresAt();
        if (exp > 0 && exp - (long)std::time(nullptr) > marginSec) return false;
        ReloadSessionToken();   // someone else may already have renewed it
        if (TokenExpiresAt() - (long)std::time(nullptr) > marginSec) return true;
        return RefreshSupabaseToken();
    }

    /// Anonymous request (apikey only, no bearer) — for public reads that don't
    /// need a session, e.g. checking username availability before sign-up.
    ApiResponse ApiAnon(const std::string& path, const std::string& method = "GET",
                        const std::string& jsonBody = {}, const std::string& extraHeader = {}) const {
        if (!IsOnline()) return {};
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += path.empty() || path.front() == '/' ? path : ("/" + path);
        return HttpRequest(method, url, jsonBody, /*authToken=*/{}, extraHeader);
    }

    /// Upload a local file as the raw request body with the session token — e.g.
    /// Supabase Storage `POST /storage/v1/object/<bucket>/<path>` for chat
    /// attachments. `contentType` is the file's MIME type.
    ApiResponse ApiUpload(const std::string& path, const std::string& localFile,
                          const std::string& contentType, const std::string& extraHeader = {}) const {
        if (!IsOnline() || !session_.loggedIn) return {};
        std::string url = serverUrl_;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += path.empty() || path.front() == '/' ? path : ("/" + path);
        return HttpRequest("POST", url, {}, /*authToken=*/session_.token, extraHeader,
                           localFile, contentType.empty() ? "application/octet-stream" : contentType);
    }

    /// Whether a username (profiles.username handle) is free. Online only; on a
    /// local backend or when offline it optimistically returns true.
    bool UsernameAvailable(const std::string& username) const {
        if (provider_ != Provider::Supabase || username.empty()) return true;
        std::string enc; char b[4];
        for (unsigned char c : username) {
            if (std::isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') enc += (char)c;
            else { std::snprintf(b, sizeof(b), "%%%02X", c); enc += b; }
        }
        ApiResponse r = ApiAnon("/rest/v1/profiles?select=id&username=eq." + enc);
        if (!r.ok) return true;                       // couldn't check -> don't block
        return r.body.find('{') == std::string::npos; // no rows -> available
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
        if (r.status == 401 || r.status == 403) {    // token expired/revoked
            // Try to renew from the refresh token before giving up — a 1-hour
            // Supabase access token expiring shouldn't sign the player out.
            if (RefreshSupabaseToken()) return true;
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
    std::string resetRedirect_;   // see SetResetRedirect
    std::string apiKey_;
    Provider    provider_;
    mutable Session session_;   // mutable: Api() picks up a token refreshed by another instance

    fs::path SessionPath() const { return configDir_ / "session.txt"; }

    // A claim from a JWT's payload (base64url JSON), without verifying it — we
    // only read our own token's id / expiry.
    static std::string JwtClaim(const std::string& jwt, const std::string& key) {
        const std::size_t a = jwt.find('.'), b = a == std::string::npos ? a : jwt.find('.', a + 1);
        if (b == std::string::npos) return "";
        const std::string part = jwt.substr(a + 1, b - a - 1);
        std::string json; unsigned acc = 0; int bits = 0;
        for (char c : part) {
            int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
                  : (c == '-' || c == '+') ? 62 : (c == '_' || c == '/') ? 63 : -1;
            if (v < 0) continue;
            acc = (acc << 6) | (unsigned)v; bits += 6;
            if (bits >= 8) { bits -= 8; json += (char)((acc >> bits) & 0xFF); }
        }
        const std::string k = "\"" + key + "\"";
        std::size_t p = json.find(k); if (p == std::string::npos) return "";
        p = json.find(':', p + k.size()); if (p == std::string::npos) return "";
        ++p; while (p < json.size() && json[p] == ' ') ++p;
        if (p < json.size() && json[p] == '"') { const std::size_t e = json.find('"', p + 1); return e == std::string::npos ? "" : json.substr(p + 1, e - p - 1); }
        std::size_t e = p; while (e < json.size() && (std::isdigit((unsigned char)json[e]) || json[e] == '-')) ++e;
        return json.substr(p, e - p);
    }
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
        std::string user, token, refresh;
        std::getline(f, user);
        std::getline(f, token);
        std::getline(f, refresh);   // optional 3rd line (older files won't have it)
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
        };
        trim(user); trim(token); trim(refresh);
        if (!user.empty() && !token.empty()) {
            // Older sessions may have been saved with the email as the display
            // name; show only the local-part so the UI never shows a raw email.
            if (provider_ == Provider::Supabase) {
                std::size_t at = user.find('@');
                if (at != std::string::npos) user = user.substr(0, at);
            }
            session_.loggedIn = true;
            session_.username = user;
            session_.token    = token;
            session_.refreshToken = refresh;
        }
    }

    void SaveSession() {
        std::ofstream f(SessionPath(), std::ios::trunc);
        f << session_.username << "\n" << session_.token << "\n" << session_.refreshToken << "\n";
    }

    Result Succeed(const std::string& username, const std::string& token,
                   const std::string& refresh = {}) {
        session_.loggedIn = true;
        session_.username = username;
        session_.token    = token;
        session_.refreshToken = refresh;
        SaveSession();
        Result r; r.ok = true; r.session = session_; return r;
    }

    // Renew the Supabase access token from the stored refresh token. Returns
    // true and updates the session on success. Used by VerifySession so a
    // signed-in player isn't kicked out just because the 1-hour token expired.
    bool RefreshSupabaseToken() {
        if (provider_ != Provider::Supabase || session_.refreshToken.empty() || !IsOnline())
            return false;
        std::string base = serverUrl_;
        if (!base.empty() && base.back() == '/') base.pop_back();
        ApiResponse r = HttpRequest("POST", base + "/auth/v1/token?grant_type=refresh_token",
                                    "{\"refresh_token\":\"" + detail::JsonEscape(session_.refreshToken) + "\"}",
                                    /*authToken=*/{});
        if (!r.ok) return false;
        std::string tok = detail::JsonField(r.body, "access_token");
        if (tok.empty()) return false;
        session_.token = tok;
        std::string nr = detail::JsonField(r.body, "refresh_token");
        if (!nr.empty()) session_.refreshToken = nr;   // Supabase rotates refresh tokens
        SaveSession();
        return true;
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
        f.close();
        std::error_code ec;   // the hashes are for this user's eyes only
        fs::permissions(DbPath(), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    }

    Result LocalRegister(const std::string& username, const std::string& password) {
        auto records = ReadDb();
        std::string key = Lower(username);
        for (const auto& r : records)
            if (Lower(r.user) == key) return Fail("That username is already taken.");
        Record r;
        r.user = username;
        r.salt = detail::RandomHex(16);
        r.hash = detail::HashPasswordV2(password, r.salt);
        records.push_back(r);
        WriteDb(records);
        return Succeed(username, detail::RandomHex(24));
    }

    Result LocalLogin(const std::string& username, const std::string& password) {
        auto records = ReadDb();
        std::string key = Lower(username);
        for (auto& r : records) {
            if (Lower(r.user) != key) continue;
            bool good = false, upgrade = false;
            std::uint32_t rounds = 0;
            if (detail::StoredRounds(r.hash, rounds)) {                   // the current format
                good = detail::SecureEquals(detail::HashPasswordV2(password, r.salt, rounds), r.hash);
                upgrade = good && rounds < detail::kPasswordRounds;      // made when the cost was lower
            } else if (!detail::IsHashV2(r.hash)) {                      // an older version's SHA-256 chain
                good = detail::SecureEquals(detail::HashPassword(password, r.salt), r.hash);
                upgrade = good;
            }                                                            // (an unreadable v2 record never matches)
            if (!good) return Fail("Incorrect password.");
            if (upgrade) {                                               // transparent: the password is in hand, store it the new way
                r.salt = detail::RandomHex(16);
                r.hash = detail::HashPasswordV2(password, r.salt);
                WriteDb(records);
            }
            return Succeed(r.user, detail::RandomHex(24));
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
        // Sign in with a username OR an email: if the identifier isn't an email,
        // resolve it to the account's email via a SECURITY DEFINER RPC that only
        // returns the email when the password is correct (no email enumeration).
        std::string loginId = email;
        if (!signUp && loginId.find('@') == std::string::npos) {
            ApiResponse rr = HttpRequest("POST", base + "/rest/v1/rpc/email_for_login",
                "{\"p_username\":\"" + detail::JsonEscape(loginId) +
                "\",\"p_password\":\"" + detail::JsonEscape(password) + "\"}", /*authToken=*/{});
            std::string resolved;   // the RPC returns a JSON string (the email) or null
            if (rr.ok) { std::string b = rr.body;
                std::size_t q = b.find('"');
                if (q != std::string::npos) { std::size_t e2 = b.find('"', q + 1);
                    if (e2 != std::string::npos) resolved = b.substr(q + 1, e2 - q - 1); } }
            if (resolved.empty())
                return Fail("Invalid username/email or password.");
            loginId = resolved;
        }
        std::string url = base + (signUp ? "/auth/v1/signup"
                                         : "/auth/v1/token?grant_type=password");
        std::string body = "{\"email\":\"" + detail::JsonEscape(loginId) +
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
        // Prefer the username from user metadata; never surface the raw email as
        // the display name. When no handle was stored (e.g. an account made
        // before usernames, or a bare email sign-up), derive a friendly handle
        // from the email's local-part ("alex.doe@x.com" -> "alex.doe").
        std::string display = detail::JsonField(r.body, "username");
        if (display.empty()) {
            std::size_t at = loginId.find('@');
            display = at == std::string::npos ? loginId : loginId.substr(0, at);
        }
        std::string refresh = detail::JsonField(r.body, "refresh_token");
        return Succeed(display, token, refresh);
    }

    // `extraHeader` may hold several headers, one per line ("x-upsert: true\n
    // cache-control: max-age=30"). Blank lines and stray quotes are dropped.
    static std::vector<std::string> SplitHeaders(const std::string& extra) {
        std::vector<std::string> out;
        std::string cur;
        for (std::size_t i = 0; i <= extra.size(); ++i) {
            const char c = i < extra.size() ? extra[i] : '\n';
            if (c == '\n' || c == '\r') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
            else if (c != '"') cur += c;
        }
        return out;
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
                            const std::string& extraHeader = {},
                            const std::string& bodyFile = {},     // raw file body (uploads)
                            const std::string& bodyType = {},
                            int timeoutSec = 0) const {
        // Native client first (Windows: WinHTTP, kept-alive connections, no
        // process spawn); fall back to curl if it can't reach the server.
        if (okay::http::NativeAvailable()) {
            std::vector<std::string> hdrs;
            if (!apiKey_.empty())      hdrs.push_back("apikey: " + apiKey_);
            if (!authToken.empty())    hdrs.push_back("Authorization: Bearer " + authToken);
            for (const std::string& h : SplitHeaders(extraHeader)) hdrs.push_back(h);
            if (!bodyFile.empty())     hdrs.push_back("Content-Type: " + bodyType);
            else if (!jsonBody.empty()) hdrs.push_back("Content-Type: application/json");
            okay::http::Result nr = okay::http::Request(method, url, hdrs, bodyFile.empty() ? jsonBody : std::string(), bodyFile, {}, timeoutSec > 0 ? timeoutSec * 1000 : 0);
            if (nr.reached) {
                ApiResponse out;
                out.status = nr.status; out.body = std::move(nr.body);
                out.reached = true;
                out.ok = out.status >= 200 && out.status < 300;
                return out;
            }
        }
        std::error_code ec;
        fs::path dir   = fs::temp_directory_path(ec);
        std::string id = detail::RandomHex(8);
        fs::path resp  = dir / ("okay_acct_" + id + ".out");
        fs::path code  = dir / ("okay_acct_" + id + ".code");
        fs::path bodyF = dir / ("okay_acct_" + id + ".json");
        fs::path cfgF  = dir / ("okay_acct_" + id + ".cfg");

        std::string cmd = "curl -s -X " + method;
        if (timeoutSec > 0)   // a stalled server must not hang the caller (the join-time ticket request)
            cmd += " --connect-timeout " + std::to_string(timeoutSec < 5 ? timeoutSec : 5) + " --max-time " + std::to_string(timeoutSec);
        cmd +=
            " -o \"" + resp.string() + "\" -w \"%{http_code}\"";

        // Headers/secrets go in a curl config file (-K), keeping them out of argv.
        std::string cfg;
        if (!apiKey_.empty())                          // managed backends (Supabase)
            cfg += "header = \"apikey: " + apiKey_ + "\"\n";
        if (!authToken.empty())
            cfg += "header = \"Authorization: Bearer " + authToken + "\"\n";
        for (const std::string& h : SplitHeaders(extraHeader))   // e.g. PostgREST "Prefer: ..."
            cfg += "header = \"" + h + "\"\n";
        if (!bodyFile.empty()) {                       // upload a file as-is
            cfg += "header = \"Content-Type: " + bodyType + "\"\n";
            cmd += " --data-binary @\"" + bodyFile + "\"";
        } else if (!jsonBody.empty()) {
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
