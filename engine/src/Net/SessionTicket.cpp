#include "okay/Net/SessionTicket.hpp"
#include "okay/Net/SecureChannel.hpp"
#include <charconv>

namespace okay::net {

namespace {
constexpr const char* kPrefix = "okt1.";
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

#ifdef OKAY_HAVE_SODIUM
bool ParseInt64(const std::string& v, std::int64_t& out) {
    if (v.empty()) return false;
    auto r = std::from_chars(v.data(), v.data() + v.size(), out);
    return r.ec == std::errc() && r.ptr == v.data() + v.size();
}
#endif
bool Fail(std::string* error, const char* why) { if (error) *error = why; return false; }
} // namespace

std::string SessionTicket::Base64UrlEncode(const std::vector<std::uint8_t>& b) {
    std::string o;
    std::size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        const unsigned v = (b[i] << 16) | (b[i + 1] << 8) | b[i + 2];
        o += kB64[(v >> 18) & 63]; o += kB64[(v >> 12) & 63]; o += kB64[(v >> 6) & 63]; o += kB64[v & 63];
    }
    if (b.size() - i == 1) {
        const unsigned v = b[i] << 16;
        o += kB64[(v >> 18) & 63]; o += kB64[(v >> 12) & 63];
    } else if (b.size() - i == 2) {
        const unsigned v = (b[i] << 16) | (b[i + 1] << 8);
        o += kB64[(v >> 18) & 63]; o += kB64[(v >> 12) & 63]; o += kB64[(v >> 6) & 63];
    }
    return o;
}

bool SessionTicket::Base64UrlDecode(const std::string& t, std::vector<std::uint8_t>& out) {
    out.clear();
    if (t.size() % 4 == 1) return false;
    unsigned acc = 0; int bits = 0;
    for (char c : t) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-') v = 62;
        else if (c == '_') v = 63;
        else return false;
        acc = (acc << 6) | (unsigned)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF)); }
    }
    // Leftover bits must be zero (one canonical spelling per ticket).
    return (acc & ((1u << bits) - 1)) == 0;
}

bool SessionTicket::Verify(const std::string& ticket, const std::vector<std::uint8_t>& publicKey,
                           const std::string& audience, std::int64_t now,
                           SessionTicketClaims& out, std::string* error) {
#ifdef OKAY_HAVE_SODIUM
    SecureChannel::Init();
    if (publicKey.size() != crypto_sign_PUBLICKEYBYTES) return Fail(error, "no ticket key configured");
    if (ticket.size() > 2048) return Fail(error, "ticket too long");
    if (ticket.compare(0, 5, kPrefix) != 0) return Fail(error, "not a session ticket");
    const std::size_t dot = ticket.find('.', 5);
    if (dot == std::string::npos || ticket.find('.', dot + 1) != std::string::npos) return Fail(error, "malformed ticket");
    const std::string signedPart = ticket.substr(0, dot);
    std::vector<std::uint8_t> payload, sig;
    if (!Base64UrlDecode(ticket.substr(5, dot - 5), payload) || !Base64UrlDecode(ticket.substr(dot + 1), sig))
        return Fail(error, "malformed ticket");
    if (sig.size() != crypto_sign_BYTES) return Fail(error, "malformed ticket");
    if (crypto_sign_verify_detached(sig.data(), reinterpret_cast<const unsigned char*>(signedPart.data()),
                                    signedPart.size(), publicKey.data()) != 0)
        return Fail(error, "bad ticket signature");
    // Signed, so the payload is the service's: parse it strictly anyway.
    SessionTicketClaims c;
    bool haveSub = false, haveAud = false, haveIat = false, haveExp = false;
    const std::string text(payload.begin(), payload.end());
    std::size_t a = 0;
    while (a <= text.size()) {
        std::size_t b = text.find('&', a);
        if (b == std::string::npos) b = text.size();
        const std::string kv = text.substr(a, b - a);
        const std::size_t eq = kv.find('=');
        if (eq == std::string::npos) return Fail(error, "malformed ticket claims");
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        if (k == "sub") { c.userId = v; haveSub = true; }
        else if (k == "aud") { c.audience = v; haveAud = true; }
        else if (k == "iat") { if (!ParseInt64(v, c.issuedAt)) return Fail(error, "malformed ticket claims"); haveIat = true; }
        else if (k == "exp") { if (!ParseInt64(v, c.expiresAt)) return Fail(error, "malformed ticket claims"); haveExp = true; }
        else if (k == "cnf") { c.keyBinding = v; }
        // other keys: ignored (room for later claims)
        a = b + 1;
    }
    if (!haveSub || !haveAud || !haveIat || !haveExp || c.userId.empty()) return Fail(error, "ticket is missing claims");
    if (c.expiresAt <= c.issuedAt || c.expiresAt - c.issuedAt > kMaxLifetime) return Fail(error, "ticket lifetime is invalid");
    if (now + kClockSkew < c.issuedAt) return Fail(error, "ticket is from the future");
    if (now - kClockSkew >= c.expiresAt) return Fail(error, "ticket has expired");
    if (!audience.empty() && c.audience != "*" && c.audience != audience) return Fail(error, "ticket is for another server");
    out = c;
    return true;
#else
    (void)ticket; (void)publicKey; (void)audience; (void)now; (void)out;
    return Fail(error, "built without libsodium: session tickets can't be checked");
#endif
}

std::string SessionTicket::Sign(const SessionTicketClaims& c, const std::vector<std::uint8_t>& secretKey) {
#ifdef OKAY_HAVE_SODIUM
    SecureChannel::Init();
    if (secretKey.size() != crypto_sign_SECRETKEYBYTES) return {};
    const std::string payload = "sub=" + c.userId + "&aud=" + c.audience + "&iat=" + std::to_string(c.issuedAt) +
                                "&exp=" + std::to_string(c.expiresAt) + (c.keyBinding.empty() ? "" : "&cnf=" + c.keyBinding);
    const std::string signedPart = kPrefix + Base64UrlEncode(std::vector<std::uint8_t>(payload.begin(), payload.end()));
    std::vector<std::uint8_t> sig(crypto_sign_BYTES);
    crypto_sign_detached(sig.data(), nullptr, reinterpret_cast<const unsigned char*>(signedPart.data()),
                         signedPart.size(), secretKey.data());
    return signedPart + "." + Base64UrlEncode(sig);
#else
    (void)c; (void)secretKey; return {};
#endif
}

std::string SessionTicket::KeyBinding(const std::vector<std::uint8_t>& publicKey) {
#ifdef OKAY_HAVE_SODIUM
    if (publicKey.empty()) return {};
    SecureChannel::Init();
    std::vector<std::uint8_t> h(crypto_hash_sha256_BYTES);
    crypto_hash_sha256(h.data(), publicKey.data(), publicKey.size());
    return Base64UrlEncode(h);
#else
    (void)publicKey; return {};
#endif
}

bool SessionTicket::GenerateKeyPair(std::vector<std::uint8_t>& pk, std::vector<std::uint8_t>& sk) {
#ifdef OKAY_HAVE_SODIUM
    SecureChannel::Init();
    pk.resize(crypto_sign_PUBLICKEYBYTES); sk.resize(crypto_sign_SECRETKEYBYTES);
    return crypto_sign_keypair(pk.data(), sk.data()) == 0;
#else
    (void)pk; (void)sk; return false;
#endif
}

} // namespace okay::net
