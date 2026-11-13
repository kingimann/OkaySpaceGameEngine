#include "test_framework.hpp"
#include "AntiCheatAttest.hpp"
#include <random>

// Heartbeat attestation against a fake server: honest answers, replay, forged and altered
// answers, expiry, unlisted builds, dirty facts, silence, and the wire format.
namespace {
using namespace okayac;
using namespace okayac::attest;

const std::string kKey = "the session key from the encrypted handshake";

// A fake server and client joined by "the wire": byte buffers, so serialisation is in the path.
struct Wire {
    Verifier server;
    Client client;
    std::uint64_t now = 100000;
    Wire(const std::string& serverKey = kKey, const std::string& clientKey = kKey, std::set<std::string> builds = {"1.0.0+abc"},
         const std::string& clientBuild = "1.0.0+abc")
        : server(serverKey, std::move(builds), 15000), client(clientKey, clientBuild) {}
    std::vector<std::uint8_t> ChallengeBytes() { return Serialize(server.Issue(now)); }
    std::vector<std::uint8_t> ClientReply(const std::vector<std::uint8_t>& ch, std::uint32_t bits) {
        Challenge c;
        CHECK(Parse(ch.data(), ch.size(), c));
        return Serialize(client.Answer(c, bits));
    }
    Result ServerAccept(const std::vector<std::uint8_t>& reply) {
        Response r;
        if (!Parse(reply.data(), reply.size(), r)) { Result x; x.verdict = Verdict::Unknown; return x; }
        return server.Accept(r, now);
    }
};

void TestHonest() {
    RUN_SUITE("attest: an honest client");
    Wire w;
    for (int i = 0; i < 20; ++i) {
        w.now += 5000;
        const auto r = w.ServerAccept(w.ClientReply(w.ChallengeBytes(), kCleanFacts));
        CHECK(r.verdict == Verdict::Ok && r.bits == kCleanFacts && r.build == "1.0.0+abc");
    }
    CHECK(w.server.Unanswered() == 0 && w.server.Outstanding() == 0);
    // Before the first code scan finishes the facts are 0: young, not dirty.
    CHECK(w.ServerAccept(w.ClientReply(w.ChallengeBytes(), 0)).verdict == Verdict::Ok);
}

void TestFactsDecoded() {
    RUN_SUITE("attest: dirty facts are decoded exactly");
    Wire w;
    for (std::uint32_t bits = 0; bits <= kAllFactBits; bits += 7) {
        const auto r = w.ServerAccept(w.ClientReply(w.ChallengeBytes(), bits));
        CHECK(r.bits == bits);
        CHECK((r.verdict == Verdict::Ok) == ((bits & ~kCodeScanned) == 0));
    }
    IntegrityFacts f;
    f.codeScanned = true; f.apiHooked = true; f.clockSuspect = true;
    CHECK(FactsToBits(f) == (kCodeScanned | kApiHooked | kClockSuspect));
    const auto r = w.server.Accept(w.client.Answer(w.server.Issue(w.now), f), w.now);
    CHECK(r.verdict == Verdict::Dirty && (r.bits & kApiHooked) && (r.bits & kClockSuspect) && !(r.bits & kCodeModified));
}

void TestReplay() {
    RUN_SUITE("attest: replay and forgery");
    Wire w;
    const auto ch = w.ChallengeBytes();
    const auto reply = w.ClientReply(ch, kCleanFacts);
    CHECK(w.ServerAccept(reply).verdict == Verdict::Ok);
    CHECK(w.ServerAccept(reply).verdict == Verdict::Replay);            // the same bytes again
    CHECK(w.ServerAccept(reply).verdict == Verdict::Replay);
    // An old answer can't answer a new challenge: different nonce.
    const auto ch2 = w.ChallengeBytes();
    Response old;
    Parse(reply.data(), reply.size(), old);
    Challenge c2;
    Parse(ch2.data(), ch2.size(), c2);
    Response spliced = old;
    spliced.nonce = c2.nonce; spliced.seq = c2.seq;                      // old tag and hash under the new nonce
    CHECK(w.server.Accept(spliced, w.now).verdict == Verdict::BadTag);
    // ... and a failed forgery doesn't use up the real answer.
    CHECK(w.ServerAccept(w.ClientReply(ch2, kCleanFacts)).verdict == Verdict::Ok);
    // A made-up nonce.
    Response fake = old; fake.nonce ^= 0xFFFF;
    CHECK(w.server.Accept(fake, w.now).verdict == Verdict::Unknown);
}

void TestWrongKeyAndTamper() {
    RUN_SUITE("attest: wrong key and altered fields");
    {   // a client without the session key (a relay, a bot that never did the handshake)
        Wire w(kKey, "some other key");
        CHECK(w.ServerAccept(w.ClientReply(w.ChallengeBytes(), kCleanFacts)).verdict == Verdict::BadTag);
    }
    Wire w;
    const auto ch = w.ChallengeBytes();
    Challenge c;
    Parse(ch.data(), ch.size(), c);
    Response good = w.client.Answer(c, kCleanFacts);
    {   // a man in the middle flips the facts hash to hide a bit, or changes the build, or the tag
        Response r = good; r.factsHash[0] ^= 1;
        CHECK(w.server.Accept(r, w.now).verdict == Verdict::BadTag);
        r = good; r.build = "9.9.9";
        CHECK(w.server.Accept(r, w.now).verdict == Verdict::BadTag);
        r = good; r.tag[31] ^= 0x80;
        CHECK(w.server.Accept(r, w.now).verdict == Verdict::BadTag);
        r = good; r.seq += 1;
        CHECK(w.server.Accept(r, w.now).verdict != Verdict::Ok);
    }
    CHECK(w.server.Accept(good, w.now).verdict == Verdict::Ok);          // the real one still works
    // The attestation key is derived: using the session key itself as the HMAC key doesn't verify.
    const auto ch2 = w.ChallengeBytes();
    Challenge c2; Parse(ch2.data(), ch2.size(), c2);
    Response raw = w.client.Answer(c2, kCleanFacts);
    raw.tag = okayac::detail::HmacSha256(kKey, "okayac-attest/1|" + std::to_string(c2.nonce) + "|" + std::to_string(c2.seq) + "|" + raw.build + "|" +
                                                   ToHex(raw.factsHash.data(), 32));
    CHECK(w.server.Accept(raw, w.now).verdict == Verdict::BadTag);
}

void TestBuildAndFactsHonesty() {
    RUN_SUITE("attest: builds, a client that isn't computing the facts");
    {
        Wire w(kKey, kKey, {"1.0.0+abc"}, "0.9.0+old");
        const auto r = w.ServerAccept(w.ClientReply(w.ChallengeBytes(), kCleanFacts));
        CHECK(r.verdict == Verdict::BadBuild && r.build == "0.9.0+old");
    }
    {   // any build when none are listed
        Wire w(kKey, kKey, {}, "whatever");
        CHECK(w.ServerAccept(w.ClientReply(w.ChallengeBytes(), kCleanFacts)).verdict == Verdict::Ok);
    }
    {   // a correctly keyed answer whose hash is of nothing the protocol defines
        Wire w;
        const auto ch = w.ChallengeBytes();
        Challenge c; Parse(ch.data(), ch.size(), c);
        Response r;
        r.nonce = c.nonce; r.seq = c.seq; r.build = "1.0.0+abc";
        r.factsHash.fill(0x42);
        r.tag = attest::detail::Tag(attest::detail::AttestKey(kKey), r.nonce, r.seq, r.build, r.factsHash);
        CHECK(w.server.Accept(r, w.now).verdict == Verdict::BadFacts);
    }
    {   // a facts hash that's right for another challenge's nonce is wrong here
        Wire w;
        const auto a = w.ChallengeBytes(), b = w.ChallengeBytes();
        Challenge ca, cb; Parse(a.data(), a.size(), ca); Parse(b.data(), b.size(), cb);
        Response ra = w.client.Answer(ca, kCleanFacts);
        Response rb = w.client.Answer(cb, kCleanFacts);
        Response mix = rb;
        mix.factsHash = ra.factsHash;
        mix.tag = attest::detail::Tag(attest::detail::AttestKey(kKey), mix.nonce, mix.seq, mix.build, mix.factsHash);   // even with a valid tag
        CHECK(w.server.Accept(mix, w.now).verdict == Verdict::BadFacts);
    }
}

void TestExpiryAndSilence() {
    RUN_SUITE("attest: expiry and silence");
    Wire w;
    const auto ch = w.ChallengeBytes();
    const auto reply = w.ClientReply(ch, kCleanFacts);
    w.now += 16000;   // past the 15 s limit
    CHECK(w.ServerAccept(reply).verdict == Verdict::Expired);
    CHECK(w.server.Unanswered() == 1);
    // A client that never answers: unanswered challenges are counted, and never pile up.
    for (int i = 0; i < 50; ++i) { w.now += 1000; w.server.Issue(w.now); }
    CHECK(w.server.Outstanding() <= 8);
    CHECK(w.server.Unanswered() >= 30);
    // Answer in time: fine.
    w.now += 20000;
    CHECK(w.ServerAccept(w.ClientReply(w.ChallengeBytes(), kCleanFacts)).verdict == Verdict::Ok);
}

void TestWireFormat() {
    RUN_SUITE("attest: wire format round trip and garbage");
    Wire w;
    Challenge c = w.server.Issue(0);
    auto cb = Serialize(c);
    Challenge c2;
    CHECK(cb.size() == 14 && Parse(cb.data(), cb.size(), c2) && c2.nonce == c.nonce && c2.seq == c.seq);
    Response r = w.client.Answer(c, kCleanFacts);
    auto rb = Serialize(r);
    Response r2;
    CHECK(Parse(rb.data(), rb.size(), r2) && r2.nonce == r.nonce && r2.build == r.build && r2.tag == r.tag && r2.factsHash == r.factsHash);
    // Every truncation and extension is refused; random bytes never crash.
    for (std::size_t n = 0; n < rb.size(); ++n) { Response x; CHECK(!Parse(rb.data(), n, x)); }
    { auto longer = rb; longer.push_back(0); Response x; CHECK(!Parse(longer.data(), longer.size(), x)); }
    { auto bad = rb; bad[0] = 0x00; Response x; CHECK(!Parse(bad.data(), bad.size(), x)); }
    { auto bad = rb; bad[1] = 9; Response x; CHECK(!Parse(bad.data(), bad.size(), x)); }
    { auto bad = rb; bad[14] = 200; Response x; CHECK(!Parse(bad.data(), bad.size(), x)); }   // build length lies
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        std::vector<std::uint8_t> g(rng() % 120);
        for (auto& b : g) b = (std::uint8_t)rng();
        if (!g.empty() && rng() % 2) g[0] = rng() % 2 ? kMsgResponse : kMsgChallenge;
        if (g.size() > 1 && rng() % 2) g[1] = kVersion;
        Response x; Challenge y;
        Parse(g.data(), g.size(), x); Parse(g.data(), g.size(), y);
    }
    CHECK(true);
    // A long build id is cut to the limit on both ends and still verifies.
    Wire lw(kKey, kKey, {}, std::string(200, 'x'));
    const auto res = lw.ServerAccept(lw.ClientReply(lw.ChallengeBytes(), kCleanFacts));
    CHECK(res.verdict == Verdict::Ok && res.build.size() == kMaxBuildId);
}

void TestNoncesDiffer() {
    RUN_SUITE("attest: nonces and sequence numbers");
    Verifier v(kKey);
    std::set<std::uint64_t> seen;
    std::uint32_t last = 0;
    for (int i = 0; i < 1000; ++i) {
        const Challenge c = v.Issue((std::uint64_t)i * 20000);
        CHECK(seen.insert(c.nonce).second);
        CHECK(c.seq == last + 1);
        last = c.seq;
    }
}
} // namespace

int main() {
    TestHonest();
    TestFactsDecoded();
    TestReplay();
    TestWrongKeyAndTamper();
    TestBuildAndFactsHonesty();
    TestExpiryAndSilence();
    TestWireFormat();
    TestNoncesDiffer();
    TEST_MAIN_RESULT();
}
