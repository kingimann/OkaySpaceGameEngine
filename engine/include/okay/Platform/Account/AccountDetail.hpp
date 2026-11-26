// Helpers of the account service, used by AccountService (AccountService.hpp), the community headers and the OkayBackend
// Supabase provider (okay/Backend/Supabase.hpp): the public name rule, SHA-256 / PBKDF2 wrappers, OS random bytes, the small
// JSON readers and writers, percent-encoding, plain-words network reasons and the atomic file write. Moved here unchanged
// from AccountService.hpp (OkayBackend round 2) so the backend layer can use them without including AccountService.
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#if !defined(_WIN32)
#include <time.h>   // nanosleep (detail::NapMs)
#endif
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  if defined(OKAY_XBOX_UWP)
#    include <bcrypt.h>
#  endif
#endif
#include "okay/Core/Json.hpp"       // okay::json::Escape (detail::JsonEscape)
#include "okay/Crypto/Sha256.hpp"   // the engine's one SHA-256 / HMAC / PBKDF2

namespace okay::account {

namespace fs = std::filesystem;

/// The name shown to OTHER people for an account: never an email address. Names come from the server as free text
/// (author_name, sender_name, display_name, username...) and old clients stored the email there; an email is cut to the
/// part before the "@" ("alex.doe@x.com" -> "alex.doe"), and when nothing is left the name is "Player" plus the first
/// characters of the account id (or just "Player"). Empty stays empty (callers have their own "unknown"). Every place the
/// launcher and the editor read a person's name from the server goes through this (tests/test_public_name.cpp).
inline std::string PublicName(const std::string& name, const std::string& userId = {}) {
    if (name.find('@') == std::string::npos) return name;
    std::string local = name.substr(0, name.find('@'));
    while (!local.empty() && std::isspace((unsigned char)local.back())) local.pop_back();
    while (!local.empty() && std::isspace((unsigned char)local.front())) local.erase(local.begin());
    if (!local.empty()) return local;
    std::string tag;
    for (char c : userId) { if (std::isalnum((unsigned char)c)) tag += c; if (tag.size() == 4) break; }
    return tag.empty() ? std::string("Player") : "Player-" + tag;
}

// ===========================================================================
// SHA-256 / PBKDF2. These are thin wrappers over the engine's one implementation, okay/Crypto/Sha256.hpp (docs/platform/sha256_comparison.md).
// The wrapper keeps the interface the account service, the updater and the anti-cheat code already use.
// ===========================================================================
namespace detail {

class Sha256 {
public:
    void Update(const std::uint8_t* data, std::size_t len) { impl_.Update(data, len); }
    void Update(const std::string& s) { impl_.Update(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }

    /// The digest; the object starts a fresh message afterwards (as it always did).
    std::array<std::uint8_t, 32> Digest() {
        std::array<std::uint8_t, 32> out{};
        impl_.Final(out.data());
        impl_ = okay::crypto::Sha256();
        return out;
    }

private:
    okay::crypto::Sha256 impl_;
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
#if defined(OKAY_XBOX_UWP)
    // UWP: no LoadLibrary. BCryptGenRandom with the system-preferred RNG needs no algorithm handle (Microsoft's BCryptGenRandom page).
    return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out), (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#elif defined(_WIN32)
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
    std::array<std::uint8_t, 32> out{};
    okay::crypto::Pbkdf2HmacSha256(out.data(), out.size(), reinterpret_cast<const std::uint8_t*>(password.data()), password.size(),
                                   salt.data(), salt.size(), iterations);
    return out;
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

// Minimal JSON helpers — just enough to send credentials and read a token back.
// The text of `s` as it goes between the quotes of a JSON string: the engine's one JSON escaper (okay::json::Escape, RFC 8259
// section 7) without its quotes. '"', '\\', line feed, carriage return and tab as before; every other control character
// (U+0000 to U+001F) as \u00xx, so a cloud save or a name holding one is still valid JSON (before OkayBackend round 4 they went
// out raw and the server refused the request). UTF-8 and every other byte go as they are.
inline std::string JsonEscape(const std::string& s) {
    std::string quoted;
    quoted.reserve(s.size() + 2);
    okay::json::Escape(s, quoted);
    return quoted.substr(1, quoted.size() - 2);
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

/// Why a request got no HTTP answer, in plain words, from the HTTP seam's low-level reason (okay::http::Result::error:
/// "connect: winhttp error 12029", "curl exit code 6", the own client's "ErrName: message"). The code stays at the end in
/// brackets so a report can be looked up. Never holds a secret: the seam's reasons carry no header, body or query string,
/// and any "?..." part is cut anyway. Empty in, empty out. Written from the WinHTTP error-code list (winhttp.h) and
/// curl's exit-code list.
inline std::string PlainNetReason(std::string raw) {
    if (raw.empty()) return {};
    for (std::size_t q = raw.find('?'); q != std::string::npos; q = raw.find('?')) {   // no query strings in what we show
        std::size_t e = raw.find_first_of(" \t\r\n", q);
        raw.erase(q, e == std::string::npos ? std::string::npos : e - q);
    }
    for (char& c : raw) if (c == '\r' || c == '\n') c = ' ';
    if (raw.size() > 160) raw.resize(160);
    auto code = [&](const char* tag) -> long {   // the number after `tag`, -1 when absent
        const std::size_t p = raw.find(tag);
        if (p == std::string::npos) return -1;
        long v = 0; bool any = false;
        for (std::size_t i = p + std::strlen(tag); i < raw.size() && raw[i] >= '0' && raw[i] <= '9'; ++i) { v = v * 10 + (raw[i] - '0'); any = true; if (v > 99999) break; }
        return any ? v : -1;
    };
    const char* words = nullptr;
    const long w = code("winhttp error "), c = code("curl exit code ");
    switch (w) {
        case 12002: words = "The server took too long to answer."; break;
        case 12007: words = "The server's name could not be looked up (no internet, or DNS is blocked)."; break;
        case 12029: words = "Could not connect to the server (no internet, or a firewall blocks it)."; break;
        case 12030: case 12031: words = "The connection was cut off before the server answered."; break;
        case 12152: words = "The server sent an answer that could not be read."; break;
        case 12037: case 12038: case 12044: case 12045: case 12057: case 12157: case 12169: case 12170: case 12175: case 12179:
            words = "The secure connection failed (a certificate or security setting on this PC, or a program that inspects HTTPS)."; break;
        case 12166: case 12167: case 12180: words = "Windows' proxy settings could not be used."; break;
        default: break;
    }
    if (!words) switch (c) {
        case 6: words = "The server's name could not be looked up (no internet, or DNS is blocked)."; break;
        case 7: words = "Could not connect to the server (no internet, or a firewall blocks it)."; break;
        case 28: words = "The server took too long to answer."; break;
        case 35: case 51: case 58: case 60: words = "The secure connection failed (a certificate or security setting on this PC, or a program that inspects HTTPS)."; break;
        case 52: case 56: words = "The connection was cut off before the server answered."; break;
        default: break;
    }
    if (!words) {
        if (raw.find("timeout") != std::string::npos) words = "The server took too long to answer.";
        else if (raw.find("proxy-auth") != std::string::npos) words = "A proxy on this network wants a password.";
        else if (raw.find("curl could not be started") != std::string::npos) words = "The backup connection (curl) could not be started.";
        else if (raw.find("bad-url") != std::string::npos) words = "The account server address is not a valid web address.";
    }
    return words ? std::string(words) + " [" + raw + "]" : "[" + raw + "]";
}

/// The sign-in's "not reached" message: the fixed first line (the launcher's FriendlyError keys on it) plus the reason
/// in plain words on a second line when the seam gave one.
inline std::string NotReachedMessage(const std::string& base, const std::string& rawReason) {
    const std::string why = PlainNetReason(rawReason);
    return why.empty() ? base : base + "\nDetails: " + why;
}

/// Write `body` to `path` whole: a temp file beside it (a random name, so two programs writing the same file at once never
/// share one), then a rename over `path`, so a reader (the launcher and the editor share session.txt) sees the old file
/// or the new one, never half of one. Windows refuses the rename while another program has the file open, so it is tried
/// again for about a quarter of a second before the old file is removed to make room. False = not written (the temp
/// file is removed). Used by AccountService::SaveSession and GameIpc::WriteAtomic.
/// A short pause without <thread> (CLAUDE.md rule 21a: this header reaches the Windows programs).
inline void NapMs(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    timespec ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0) {}
#endif
}

inline bool WriteFileAtomic(const fs::path& path, const std::string& body) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + "." + RandomHex(4) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc | std::ios::binary);
        if (!f) return false;
        f << body;
        f.flush();
        if (!f) { f.close(); fs::remove(tmp, ec); return false; }
    }
    for (int i = 0; i < 50; ++i) {
        ec.clear();
        fs::rename(tmp, path, ec);
        if (!ec) return true;
#if defined(_WIN32)
        Sleep(5);
#else
        break;   // POSIX rename replaces an open file: a failure will not go away
#endif
    }
    fs::remove(path, ec); ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) { std::error_code e2; fs::remove(tmp, e2); }
    return !ec;
}

/// Pick the account server address the way the launcher always has: a configured value (env var or file) counts only
/// when it is an http(s) address, so an empty or junk value (a stale "Use local (clear)" leftover) falls back to the one
/// built in instead of knocking the program offline. Trailing spaces and line ends are dropped. Used by the launcher and by
/// okay::Account::Get (the editor, the player), so both programs talk to the same server.
inline std::string ChooseServerUrl(std::string configured, const std::string& builtIn) {
    while (!configured.empty() && (configured.back() == '\r' || configured.back() == '\n' || configured.back() == ' ')) configured.pop_back();
    while (!configured.empty() && configured.front() == ' ') configured.erase(configured.begin());
    return configured.rfind("http", 0) == 0 ? configured : builtIn;
}

} // namespace detail

} // namespace okay::account
