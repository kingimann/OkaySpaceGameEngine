#pragma once
// Anti-cheat: heartbeat attestation (opt-in; docs/anticheat.md, "Heartbeat attestation").
//
// A server (the verifier) sends a challenge: a fresh random nonce and a sequence
// number. The client answers with
//     its build id, a hash of its integrity facts, and
//     tag = HMAC-SHA256(attestKey, "okayac-attest/1|nonce|seq|build|factsHash")
// where attestKey is derived from the session key the two already share (the
// encrypted handshake's), and the facts hash covers the nonce, so an answer can't
// be computed ahead of time or reused. The verifier keeps what it issued; an
// answer is accepted once, for a challenge it issued, within a time limit, with
// a good tag.
//
// The facts are the booleans AntiCheatHardening.hpp keeps (code section modified,
// imports hooked, API hooked, blocklisted module, debugger hint, clock suspect,
// protected value tampered, code scan ran). They are few on purpose: the verifier
// can recompute the hash for each of the 2^8 combinations and so learn exactly
// which are set, without the client sending them in the clear.
//
// WHAT IT PROVES: that whatever answered holds this session's key (it is the
// process that did the handshake, not a replayed or relayed recording), is
// answering now, and says it is build X with facts F. A client that has stopped
// answering, answers with a build the server doesn't list, or reports dirty facts
// is a signal, and so is one that never answers at all (an old client, or one
// with the feature cut out).
//
// WHAT IT DOES NOT PROVE: that the facts are TRUE. The client computes them; a
// cheater who controls the client sends "clean" forever, or never runs the checks
// at all, and can't be told apart from an honest client by this message alone.
// It is one more thing a bot has to implement and a patched client has to keep
// up, never a replacement for the server checking what the player actually does.
// Treat any result as a weight, not a verdict.
//
// Pure logic: no I/O, no clock of its own (the caller passes milliseconds). The
// network layer carries the two messages (kMsgChallenge / kMsgResponse below);
// wiring them into it is the network owner's call.
#include <random>

#include "AntiCheatHardening.hpp"

namespace okayac {
namespace attest {

constexpr std::uint8_t kMsgChallenge = 0xA1, kMsgResponse = 0xA2, kVersion = 1;
constexpr std::size_t kMaxBuildId = 64;

struct Challenge {
    std::uint64_t nonce = 0;
    std::uint32_t seq = 0;
};
struct Response {
    std::uint64_t nonce = 0;
    std::uint32_t seq = 0;
    std::string build;
    std::array<std::uint8_t, 32> factsHash{};
    std::array<std::uint8_t, 32> tag{};
};

// The facts as bits (the order is part of the protocol).
enum FactBit : std::uint32_t {
    kCodeScanned = 1u << 0, kCodeModified = 1u << 1, kImportsHooked = 1u << 2, kApiHooked = 1u << 3,
    kModuleBlocked = 1u << 4, kDebuggerHint = 1u << 5, kClockSuspect = 1u << 6, kProtectedTampered = 1u << 7,
};
constexpr std::uint32_t kAllFactBits = 0xFF;
// A healthy client has run the code scan and nothing else is set.
constexpr std::uint32_t kCleanFacts = kCodeScanned;

inline std::uint32_t FactsToBits(const IntegrityFacts& f) {
    return (f.codeScanned ? kCodeScanned : 0u) | (f.codeModified ? kCodeModified : 0u) | (f.importsHooked ? kImportsHooked : 0u) |
           (f.apiHooked ? kApiHooked : 0u) | (f.moduleBlocked ? kModuleBlocked : 0u) | (f.debuggerHint ? kDebuggerHint : 0u) |
           (f.clockSuspect ? kClockSuspect : 0u) | (f.protectedTampered ? kProtectedTampered : 0u);
}

namespace detail {
inline std::array<std::uint8_t, 32> Sha(const std::string& s) {
    okay::account::detail::Sha256 h;
    h.Update(s);
    return h.Digest();
}
// The key the attestation uses, derived (not the raw session key).
inline std::string AttestKey(const std::string& sessionKey) {
    const auto d = okayac::detail::HmacSha256(sessionKey, "okayac-attest-key/1");
    return std::string(reinterpret_cast<const char*>(d.data()), d.size());
}
inline std::array<std::uint8_t, 32> FactsHash(std::uint64_t nonce, std::uint32_t seq, const std::string& build, std::uint32_t bits) {
    return Sha("okayac-facts/1|" + std::to_string(nonce) + "|" + std::to_string(seq) + "|" + build + "|" + std::to_string(bits & kAllFactBits));
}
inline std::array<std::uint8_t, 32> Tag(const std::string& attestKey, std::uint64_t nonce, std::uint32_t seq, const std::string& build,
                                        const std::array<std::uint8_t, 32>& facts) {
    return okayac::detail::HmacSha256(attestKey, "okayac-attest/1|" + std::to_string(nonce) + "|" + std::to_string(seq) + "|" + build + "|" +
                                                     ToHex(facts.data(), facts.size()));
}
inline bool Equal(const std::array<std::uint8_t, 32>& a, const std::array<std::uint8_t, 32>& b) {
    unsigned d = 0;
    for (std::size_t i = 0; i < 32; ++i) d |= (unsigned)(a[i] ^ b[i]);
    return d == 0;
}
} // namespace detail

// ---- The client -----------------------------------------------------------------
class Client {
public:
    Client(const std::string& sessionKey, std::string buildId) : key_(detail::AttestKey(sessionKey)), build_(std::move(buildId)) {
        if (build_.size() > kMaxBuildId) build_.resize(kMaxBuildId);
    }
    Response Answer(const Challenge& c, std::uint32_t factBits) const {
        Response r;
        r.nonce = c.nonce;
        r.seq = c.seq;
        r.build = build_;
        r.factsHash = detail::FactsHash(c.nonce, c.seq, build_, factBits);
        r.tag = detail::Tag(key_, c.nonce, c.seq, build_, r.factsHash);
        return r;
    }
    Response Answer(const Challenge& c, const IntegrityFacts& f) const { return Answer(c, FactsToBits(f)); }

private:
    std::string key_, build_;
};

// ---- The verifier -----------------------------------------------------------------
enum class Verdict {
    Ok,              // fresh, authentic, a listed build, clean facts
    Dirty,           // fresh and authentic, but the facts say something is wrong (see Result::bits)
    Unknown,         // not a challenge this verifier has outstanding
    Replay,          // an answer to a challenge that was already answered
    Expired,         // answered too late
    BadTag,          // wrong key or altered fields
    BadBuild,        // authentic, but a build id the server doesn't list
    BadFacts,        // authentic, but the facts hash matches no combination: a client that isn't computing them honestly
};
inline const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::Ok: return "ok";
        case Verdict::Dirty: return "dirty";
        case Verdict::Unknown: return "unknown";
        case Verdict::Replay: return "replay";
        case Verdict::Expired: return "expired";
        case Verdict::BadTag: return "bad-tag";
        case Verdict::BadBuild: return "bad-build";
        case Verdict::BadFacts: return "bad-facts";
    }
    return "?";
}

struct Result {
    Verdict verdict = Verdict::Unknown;
    std::uint32_t bits = 0;   // the decoded facts, when the answer was authentic
    std::string build;
};

class Verifier {
public:
    // `allowedBuilds` empty = any build is accepted (then BadBuild never happens).
    Verifier(const std::string& sessionKey, std::set<std::string> allowedBuilds = {}, std::uint64_t ttlMs = 15000)
        : key_(detail::AttestKey(sessionKey)), builds_(std::move(allowedBuilds)), ttl_(ttlMs) {
        std::random_device rd;
        rng_.seed(((std::uint64_t)rd() << 32) ^ rd());
    }

    Challenge Issue(std::uint64_t nowMs) {
        Expire(nowMs);
        if (pending_.size() >= kMaxOutstanding) { pending_.erase(pending_.begin()); ++unanswered_; }
        Challenge c;
        c.nonce = rng_();
        c.seq = ++seq_;
        pending_.push_back({c, nowMs});
        return c;
    }

    Result Accept(const Response& r, std::uint64_t nowMs) {
        Result out;
        Expire(nowMs);
        auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Out& o) { return o.c.nonce == r.nonce && o.c.seq == r.seq; });
        if (it == pending_.end()) {
            out.verdict = used_.count(Key(r.nonce, r.seq)) ? Verdict::Replay : (r.seq != 0 && r.seq <= seq_ && expiredSeqs_.count(r.seq) ? Verdict::Expired : Verdict::Unknown);
            Tally(out.verdict);
            return out;
        }
        // Authenticate before anything is spent: a forged answer must not use up the real one.
        if (r.build.size() > kMaxBuildId || !detail::Equal(detail::Tag(key_, r.nonce, r.seq, r.build, r.factsHash), r.tag)) {
            out.verdict = Verdict::BadTag;
            Tally(out.verdict);
            return out;
        }
        pending_.erase(it);
        MarkUsed(r.nonce, r.seq);
        out.build = r.build;
        if (!builds_.empty() && !builds_.count(r.build)) { out.verdict = Verdict::BadBuild; Tally(out.verdict); return out; }
        bool found = false;
        for (std::uint32_t bits = 0; bits <= kAllFactBits && !found; ++bits)
            if (detail::Equal(detail::FactsHash(r.nonce, r.seq, r.build, bits), r.factsHash)) { out.bits = bits; found = true; }
        if (!found) { out.verdict = Verdict::BadFacts; Tally(out.verdict); return out; }
        // A client that hasn't finished its first code scan yet isn't dirty, just young: only the other bits count.
        out.verdict = (out.bits & ~kCodeScanned) == 0 ? Verdict::Ok : Verdict::Dirty;
        Tally(out.verdict);
        return out;
    }

    // Challenges that expired with no answer at all (a client that doesn't take part, or stopped).
    int Unanswered() const { return unanswered_; }
    int Count(Verdict v) const { auto it = counts_.find((int)v); return it == counts_.end() ? 0 : it->second; }
    std::size_t Outstanding() const { return pending_.size(); }

private:
    static constexpr std::size_t kMaxOutstanding = 8;
    static constexpr std::size_t kMaxRemembered = 256;
    struct Out { Challenge c; std::uint64_t issuedAt; };
    static std::string Key(std::uint64_t n, std::uint32_t s) { return std::to_string(n) + ":" + std::to_string(s); }
    void Tally(Verdict v) { ++counts_[(int)v]; }
    void MarkUsed(std::uint64_t n, std::uint32_t s) {
        used_.insert(Key(n, s));
        usedOrder_.push_back(Key(n, s));
        if (usedOrder_.size() > kMaxRemembered) { used_.erase(usedOrder_.front()); usedOrder_.erase(usedOrder_.begin()); }
    }
    void Expire(std::uint64_t now) {
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now > it->issuedAt + ttl_) {
                ++unanswered_;
                expiredSeqs_.insert(it->c.seq);
                if (expiredSeqs_.size() > kMaxRemembered) expiredSeqs_.erase(expiredSeqs_.begin());
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::string key_;
    std::set<std::string> builds_;
    std::uint64_t ttl_;
    std::mt19937_64 rng_;
    std::uint32_t seq_ = 0;
    std::vector<Out> pending_;
    std::set<std::string> used_;
    std::vector<std::string> usedOrder_;
    std::set<std::uint32_t> expiredSeqs_;
    int unanswered_ = 0;
    std::map<int, int> counts_;
};

// ---- Wire format (little endian; sizes are fixed or length-prefixed and bounded) -----------
inline std::vector<std::uint8_t> Serialize(const Challenge& c) {
    std::vector<std::uint8_t> b = {kMsgChallenge, kVersion};
    for (int i = 0; i < 8; ++i) b.push_back((std::uint8_t)(c.nonce >> (8 * i)));
    for (int i = 0; i < 4; ++i) b.push_back((std::uint8_t)(c.seq >> (8 * i)));
    return b;
}
inline bool Parse(const std::uint8_t* d, std::size_t n, Challenge& c) {
    if (n != 14 || d[0] != kMsgChallenge || d[1] != kVersion) return false;
    c.nonce = 0; c.seq = 0;
    for (int i = 0; i < 8; ++i) c.nonce |= (std::uint64_t)d[2 + i] << (8 * i);
    for (int i = 0; i < 4; ++i) c.seq |= (std::uint32_t)d[10 + i] << (8 * i);
    return true;
}
inline std::vector<std::uint8_t> Serialize(const Response& r) {
    std::vector<std::uint8_t> b = {kMsgResponse, kVersion};
    for (int i = 0; i < 8; ++i) b.push_back((std::uint8_t)(r.nonce >> (8 * i)));
    for (int i = 0; i < 4; ++i) b.push_back((std::uint8_t)(r.seq >> (8 * i)));
    b.push_back((std::uint8_t)std::min(r.build.size(), kMaxBuildId));
    b.insert(b.end(), r.build.begin(), r.build.begin() + std::min(r.build.size(), kMaxBuildId));
    b.insert(b.end(), r.factsHash.begin(), r.factsHash.end());
    b.insert(b.end(), r.tag.begin(), r.tag.end());
    return b;
}
inline bool Parse(const std::uint8_t* d, std::size_t n, Response& r) {
    if (n < 14 + 1 + 64 || d[0] != kMsgResponse || d[1] != kVersion) return false;
    const std::size_t bl = d[14];
    if (bl > kMaxBuildId || n != 15 + bl + 64) return false;
    r = Response{};
    for (int i = 0; i < 8; ++i) r.nonce |= (std::uint64_t)d[2 + i] << (8 * i);
    for (int i = 0; i < 4; ++i) r.seq |= (std::uint32_t)d[10 + i] << (8 * i);
    r.build.assign(reinterpret_cast<const char*>(d + 15), bl);
    std::copy(d + 15 + bl, d + 15 + bl + 32, r.factsHash.begin());
    std::copy(d + 15 + bl + 32, d + 15 + bl + 64, r.tag.begin());
    return true;
}

} // namespace attest
} // namespace okayac
