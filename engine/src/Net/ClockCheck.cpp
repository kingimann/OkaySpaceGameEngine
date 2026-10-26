#include "okay/Net/ClockCheck.hpp"
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif
#include "AntiCheatChecks.hpp"   // shared/: okayac::ClockHeartbeat (#269)
#include <random>

namespace okay::net {

struct ClockCheck::Impl {
    okayac::ClockHeartbeat hb;
    Impl(const std::string& key, double tol) : hb(key, tol) {}
};

ClockCheck::ClockCheck(double tolerance) {
    std::random_device rd;
    std::uint8_t k[16];
    for (auto& b : k) b = static_cast<std::uint8_t>(rd());
    m_key = okayac::ToHex(k, sizeof k);
    m_impl = std::make_unique<Impl>(m_key, tolerance);
}

ClockCheck::~ClockCheck() = default;

ClockCheck::Challenge ClockCheck::Issue(std::uint64_t nowMs) {
    const okayac::ClockChallenge c = m_impl->hb.Issue(nowMs);
    return {c.nonce, c.seq};
}

ClockCheck::Verdict ClockCheck::Accept(std::uint64_t nonce, std::uint32_t seq, std::uint64_t clientMs,
                                       const std::string& tag, std::uint64_t nowMs) {
    okayac::ClockResponse r;
    r.nonce = nonce; r.seq = seq; r.clientMs = clientMs; r.tag = tag;
    switch (m_impl->hb.Accept(r, nowMs)) {
        case okayac::ClockHeartbeat::Verdict::Ok:          return Verdict::Ok;
        case okayac::ClockHeartbeat::Verdict::Drift:       return Verdict::Drift;
        case okayac::ClockHeartbeat::Verdict::BadResponse: return Verdict::BadResponse;
        default:                                           return Verdict::Pending;
    }
}

double ClockCheck::LastDrift() const { return m_impl->hb.LastDrift(); }

std::string ClockCheck::Answer(const std::string& key, std::uint64_t nonce, std::uint32_t seq,
                               std::uint64_t clientMs) {
    return okayac::AnswerChallenge({nonce, seq}, clientMs, key).tag;
}

} // namespace okay::net
