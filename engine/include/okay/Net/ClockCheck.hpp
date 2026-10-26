#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace okay::net {

/// The server's side of the anti-cheat clock heartbeat, over the network: the
/// player's okayac::ClockHeartbeat (#269, shared/AntiCheatChecks.hpp) with the
/// server as the verifier. The server issues a challenge about once a second;
/// the client answers with its game clock and an HMAC over the challenge; each
/// 2-second window gives one rate, and three off windows in a row, all the same
/// way, are drift. One NTP step or a hitch never is.
///
/// Its own file so the shared header's platform includes stay out of the
/// network code.
class ClockCheck {
public:
    enum class Verdict { Pending, Ok, Drift, BadResponse };
    struct Challenge { std::uint64_t nonce = 0; std::uint32_t seq = 0; };

    explicit ClockCheck(double tolerance);
    ~ClockCheck();
    ClockCheck(const ClockCheck&) = delete;
    ClockCheck& operator=(const ClockCheck&) = delete;

    /// A fresh challenge at the server's `nowMs`.
    Challenge Issue(std::uint64_t nowMs);
    /// A client's answer, at the server's `nowMs`.
    Verdict Accept(std::uint64_t nonce, std::uint32_t seq, std::uint64_t clientMs,
                   const std::string& tag, std::uint64_t nowMs);
    /// The HMAC key the client answers with (random per client). It goes to the
    /// client with each challenge, so it only means something over an
    /// encrypted session.
    const std::string& Key() const { return m_key; }
    /// The last window's rate: client / server - 1 (0.10 = 10% fast).
    double LastDrift() const;

    /// The client's answer (its tag) to a challenge.
    static std::string Answer(const std::string& key, std::uint64_t nonce, std::uint32_t seq,
                              std::uint64_t clientMs);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::string m_key;
};

} // namespace okay::net
