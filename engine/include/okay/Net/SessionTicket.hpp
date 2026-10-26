#pragma once
// ---------------------------------------------------------------------------
// SessionTicket — a short-lived, signed "this account may join this server"
// pass (anti-cheat P2). A ticket service (see docs/anticheat.md: a Supabase
// Edge function, deployed by the game's owner) checks the player's login and
// signs one with an ed25519 key only it holds; the game server checks the
// signature with the matching PUBLIC key, so it needs no secret and no network
// call, and a client can't mint or edit a ticket.
//
//   okt1.<payload>.<signature>        (both parts base64url, no padding)
//   payload = "sub=<user id>&aud=<server id or *>&iat=<unix s>&exp=<unix s>"
//   signature = ed25519 over the ASCII text "okt1.<payload part>"
//
// Without libsodium nothing verifies (Verify fails with a reason).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

namespace okay::net {

struct SessionTicketClaims {
    std::string userId;      ///< sub
    std::string audience;    ///< aud: the server id it's for ("*" = any server)
    std::int64_t issuedAt = 0, expiresAt = 0;   ///< unix seconds
};

class SessionTicket {
public:
    static constexpr std::size_t kPublicKeyBytes = 32, kSecretKeyBytes = 64;
    /// Seconds of clock difference tolerated between ticket service and server.
    static constexpr std::int64_t kClockSkew = 60;
    /// Longest validity a ticket may claim (exp - iat), seconds.
    static constexpr std::int64_t kMaxLifetime = 24 * 3600;

    /// Check `ticket` against the service's public key. `audience` is this
    /// server's id ("" = accept any). `now` = unix seconds. On success fills
    /// `out`; on failure `error` (if given) says why.
    static bool Verify(const std::string& ticket, const std::vector<std::uint8_t>& publicKey,
                       const std::string& audience, std::int64_t now,
                       SessionTicketClaims& out, std::string* error = nullptr);

    /// Make a ticket (what the ticket service does). For tests and tools: the
    /// secret key never belongs in a game build.
    static std::string Sign(const SessionTicketClaims& claims, const std::vector<std::uint8_t>& secretKey);

    /// A fresh ed25519 key pair (tests / local tools). False without libsodium.
    static bool GenerateKeyPair(std::vector<std::uint8_t>& publicKey, std::vector<std::uint8_t>& secretKey);

    /// base64url (no padding) <-> bytes; Decode returns false on bad input.
    static std::string Base64UrlEncode(const std::vector<std::uint8_t>& bytes);
    static bool Base64UrlDecode(const std::string& text, std::vector<std::uint8_t>& out);
};

} // namespace okay::net
