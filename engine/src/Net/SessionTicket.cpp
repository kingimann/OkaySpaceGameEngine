#include "okay/Net/SessionTicket.hpp"
#include "okay/Core/Base64.hpp"
#include "okay/Net/SecureChannel.hpp"
#include <charconv>

namespace okay::net {

namespace {
constexpr const char* kPrefix = "okt1.";

#ifdef OKAY_NET_CRYPTO
bool ParseInt64(const std::string& v, std::int64_t& out) {
    if (v.empty()) return false;
    auto r = std::from_chars(v.data(), v.data() + v.size(), out);
    return r.ec == std::errc() && r.ptr == v.data() + v.size();
}
#endif
bool Fail(std::string* error, const char* why) { if (error) *error = why; return false; }
} // namespace

// RFC 4648 section 5 (URL-safe alphabet), no padding, through okay/Core/Base64.hpp. Decoding is strict: no stray
// character, and the unused low bits of the last character must be zero (one canonical spelling per ticket).
std::string SessionTicket::Base64UrlEncode(const std::vector<std::uint8_t>& b) {
    return base64::Encode(b, base64::Alphabet::Url, false);
}

bool SessionTicket::Base64UrlDecode(const std::string& t, std::vector<std::uint8_t>& out) {
    return base64::DecodeUnpadded(t, out, base64::Alphabet::Url);
}

bool SessionTicket::Verify(const std::string& ticket, const std::vector<std::uint8_t>& publicKey,
                           const std::string& audience, std::int64_t now,
                           SessionTicketClaims& out, std::string* error) {
#ifdef OKAY_NET_CRYPTO
    netcrypto::Init();
    if (publicKey.size() != netcrypto::kSignPublicBytes) return Fail(error, "no ticket key configured");
    if (ticket.size() > 2048) return Fail(error, "ticket too long");
    if (ticket.compare(0, 5, kPrefix) != 0) return Fail(error, "not a session ticket");
    const std::size_t dot = ticket.find('.', 5);
    if (dot == std::string::npos || ticket.find('.', dot + 1) != std::string::npos) return Fail(error, "malformed ticket");
    const std::string signedPart = ticket.substr(0, dot);
    std::vector<std::uint8_t> payload, sig;
    if (!Base64UrlDecode(ticket.substr(5, dot - 5), payload) || !Base64UrlDecode(ticket.substr(dot + 1), sig))
        return Fail(error, "malformed ticket");
    if (sig.size() != netcrypto::kSignBytes) return Fail(error, "malformed ticket");
    if (!netcrypto::Verify(sig.data(), reinterpret_cast<const std::uint8_t*>(signedPart.data()),
                           signedPart.size(), publicKey.data()))
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
#ifdef OKAY_NET_CRYPTO
    netcrypto::Init();
    if (secretKey.size() != netcrypto::kSignSecretBytes) return {};
    const std::string payload = "sub=" + c.userId + "&aud=" + c.audience + "&iat=" + std::to_string(c.issuedAt) +
                                "&exp=" + std::to_string(c.expiresAt) + (c.keyBinding.empty() ? "" : "&cnf=" + c.keyBinding);
    const std::string signedPart = kPrefix + Base64UrlEncode(std::vector<std::uint8_t>(payload.begin(), payload.end()));
    std::vector<std::uint8_t> sig(netcrypto::kSignBytes);
    netcrypto::Sign(sig.data(), reinterpret_cast<const std::uint8_t*>(signedPart.data()),
                    signedPart.size(), secretKey.data());
    return signedPart + "." + Base64UrlEncode(sig);
#else
    (void)c; (void)secretKey; return {};
#endif
}

std::string SessionTicket::KeyBinding(const std::vector<std::uint8_t>& publicKey) {
#ifdef OKAY_NET_CRYPTO
    if (publicKey.empty()) return {};
    netcrypto::Init();
    std::vector<std::uint8_t> h(netcrypto::kSha256Bytes);
    netcrypto::Sha256(h.data(), publicKey.data(), publicKey.size());
    return Base64UrlEncode(h);
#else
    (void)publicKey; return {};
#endif
}

bool SessionTicket::GenerateKeyPair(std::vector<std::uint8_t>& pk, std::vector<std::uint8_t>& sk) {
#ifdef OKAY_NET_CRYPTO
    netcrypto::Init();
    pk.resize(netcrypto::kSignPublicBytes); sk.resize(netcrypto::kSignSecretBytes);
    return netcrypto::SignKeypair(pk.data(), sk.data());
#else
    (void)pk; (void)sk; return false;
#endif
}

} // namespace okay::net
