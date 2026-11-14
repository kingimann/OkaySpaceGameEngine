// A fixed transcript of okayac::ed25519 (public key, sign, verify, IsWeakKey) and okayac::sha512 on many deterministic inputs
// (messages of many lengths, many keypairs from fixed seeds, tampered signatures, tampered keys and messages, small-order and
// non-canonical keys, S >= L signatures). Every byte of every output, and every verdict, goes into one transcript; the
// transcript's SHA-512 is compared with kExpected.
//
// kExpected was recorded ONCE from the previous implementation (our C++ port of public-domain TweetNaCl that lived in
// shared/AntiCheatCrypto.hpp, with the "no libsodium, no OKAY_OWN_CRYPTO" path) before that port was deleted. So this test
// proves the replacement (okay::crypto, or libsodium where it is built) gives byte for byte the same answers the old code did.
// The inputs come from the small generator below (splitmix64); no key here is a real key.
//
// Not a security proof: it fixes behaviour on these inputs. The RFC 8032 vectors, the strictness checks and the rest are in
// test_anticheat_integrity.cpp; the big differential tests against libsodium are crypto_difftest.
#include "test_framework.hpp"
#include "AntiCheatCrypto.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace okayac;

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint64_t Next() {   // splitmix64
        std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    void Fill(std::uint8_t* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) p[i] = (std::uint8_t)(Next() >> 24);
    }
};

std::vector<std::uint8_t> g_t;   // the transcript
void Put(const void* p, std::size_t n) {
    const std::uint8_t* b = static_cast<const std::uint8_t*>(p);
    g_t.insert(g_t.end(), b, b + n);
}
void PutV(bool v) { g_t.push_back(v ? 1 : 0); }
void PutLen(std::size_t n) { for (int i = 0; i < 4; ++i) g_t.push_back((std::uint8_t)(n >> (8 * i))); }

// What the old implementation answered for the transcript below, as the SHA-512 of the whole transcript.
const char* kExpected =
    "ad5bd8187e3df945ceff3a8ee321bab30c150130731579e15e3ae647d948477212d930812a7a792b459019d0b39b538317c6d27d0560a63c19eb90f7a0e7b61a";

void Sha512Part() {
    Rng r(0x5a512);
    // every length 0..300 (all padding edges), then larger ones
    std::vector<std::size_t> lens;
    for (std::size_t n = 0; n <= 300; ++n) lens.push_back(n);
    for (std::size_t n : {511u, 512u, 513u, 1000u, 4095u, 4096u, 4097u, 65536u, 100003u}) lens.push_back(n);
    for (std::size_t n : lens) {
        std::vector<std::uint8_t> m(n);
        r.Fill(m.data(), n);
        const auto d = sha512::Hash(m.data(), n);
        PutLen(n); Put(d.data(), d.size());
        if (n) {   // the multi-piece form must equal the one-piece form
            const std::size_t cut = n / 3;
            const auto d2 = sha512::Hash({{m.data(), cut}, {m.data() + cut, n - cut}});
            CHECK(d2 == d);
        }
    }
}

void Ed25519Part() {
    Rng r(0xed25519);
    const std::size_t mlens[] = {0, 1, 2, 31, 32, 33, 63, 64, 65, 111, 112, 127, 128, 129, 255, 1000};
    for (int k = 0; k < 40; ++k) {
        ed25519::Seed seed;
        r.Fill(seed.data(), 32);
        if (k == 0) seed.fill(0);
        if (k == 1) seed.fill(0xff);
        const ed25519::PublicKey pk = ed25519::PublicKeyFromSeed(seed);
        Put(pk.data(), 32);
        PutV(ed25519::IsWeakKey(pk));
        CHECK(!ed25519::IsWeakKey(pk));
        ed25519::PublicKey other;   // some other key, for "wrong key" verdicts
        { ed25519::Seed s2; r.Fill(s2.data(), 32); other = ed25519::PublicKeyFromSeed(s2); }
        for (std::size_t ml : mlens) {
            std::vector<std::uint8_t> m(ml);
            r.Fill(m.data(), ml);
            const ed25519::Signature sig = ed25519::Sign(m.data(), ml, seed);
            const ed25519::Signature sig2 = ed25519::Sign(m.data(), ml, seed);
            CHECK(sig == sig2);   // deterministic
            Put(sig.data(), 64);
            const bool ok = ed25519::Verify(m.data(), ml, sig, pk);
            CHECK(ok);
            PutV(ok);
            // wrong key
            const bool wk = ed25519::Verify(m.data(), ml, sig, other);
            CHECK(!wk);
            PutV(wk);
            // tampered signature: one flipped bit at a chosen place in R and in S
            for (int bit : {0, 7, 100, 255, 256, 300, 511}) {
                ed25519::Signature t = sig;
                t[bit / 8] ^= (std::uint8_t)(1u << (bit % 8));
                const bool v = ed25519::Verify(m.data(), ml, t, pk);
                CHECK(!v);
                PutV(v);
            }
            // tampered message
            if (ml) {
                std::vector<std::uint8_t> t = m;
                t[r.Next() % ml] ^= 0x01;
                const bool v = ed25519::Verify(t.data(), ml, sig, pk);
                CHECK(!v);
                PutV(v);
            }
            // tampered key
            { ed25519::PublicKey t = pk; t[r.Next() % 32] ^= 0x10; const bool v = ed25519::Verify(m.data(), ml, sig, t); PutV(v); CHECK(!v); }
        }
        // S >= L: S + L is the same number modulo L but not canonical; it must be refused
        {
            std::vector<std::uint8_t> m(17);
            r.Fill(m.data(), m.size());
            ed25519::Signature sig = ed25519::Sign(m.data(), m.size(), seed);
            static const std::uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                               0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                                               0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};
            unsigned carry = 0;
            for (int i = 0; i < 32; ++i) { const unsigned v = sig[32 + i] + L[i] + carry; sig[32 + i] = (std::uint8_t)v; carry = v >> 8; }
            const bool v = ed25519::Verify(m.data(), m.size(), sig, pk);
            CHECK(!v);
            PutV(v);
        }
    }
    // Keys with no signature to be trusted: small-order and non-canonical encodings, then random 32-byte strings.
    {
        std::vector<std::array<std::uint8_t, 32>> keys;
        auto key = [&](std::uint8_t first, std::uint8_t mid, std::uint8_t last) {
            std::array<std::uint8_t, 32> a; a.fill(mid); a[0] = first; a[31] = last; keys.push_back(a);
        };
        key(0x01, 0x00, 0x00);   // y = 1 (identity)
        key(0x01, 0x00, 0x80);   // identity, sign bit set
        key(0x00, 0x00, 0x00);   // y = 0 (order 4)
        key(0x00, 0x00, 0x80);
        key(0xec, 0xff, 0x7f);   // y = p - 1 (order 2)
        key(0xec, 0xff, 0xff);
        key(0xed, 0xff, 0x7f);   // y = p (non-canonical)
        key(0xee, 0xff, 0x7f);   // y = p + 1 (non-canonical identity)
        key(0xef, 0xff, 0xff);
        key(0xff, 0xff, 0xff);
        for (int i = 0; i < 300; ++i) { std::array<std::uint8_t, 32> a; r.Fill(a.data(), 32); keys.push_back(a); }
        std::vector<std::uint8_t> m(23);
        r.Fill(m.data(), m.size());
        for (const auto& kk : keys) {
            Put(kk.data(), 32);
            PutV(ed25519::IsWeakKey(kk));
            ed25519::Signature id{};   // R = identity encoding, S = 0: what the forgery against the identity key looks like
            id[0] = 1;
            PutV(ed25519::Verify(m.data(), m.size(), id, kk));
            ed25519::Signature rnd;
            r.Fill(rnd.data(), 64);
            rnd[63] &= 0x0f;           // keep S small, so it is below L and the point maths decides
            PutV(ed25519::Verify(m.data(), m.size(), rnd, kk));
        }
    }
}
} // namespace

int main() {
    RUN_SUITE("transcript of sha512 and ed25519 on deterministic inputs");
    Sha512Part();
    Ed25519Part();
    const auto d = sha512::Hash(g_t.data(), g_t.size());
    const std::string got = ToHex(d.data(), d.size());
    if (std::getenv("OKAY_GOLDEN_PRINT")) std::printf("transcript %zu bytes, sha512 %s\n", g_t.size(), got.c_str());
    CHECK(got == kExpected);
    TEST_MAIN_RESULT();
}
