// Differential test: okay::crypto (ours, UNREVIEWED) against the system libsodium on thousands
// of random inputs, in BOTH directions (ours seals / theirs opens, and theirs seals / ours
// opens), plus the malformed-input edge cases where both must agree on accept or reject.
// Only built when libsodium is found. OKAY_CRYPTO_XCHECK_N sets the cases per primitive
// (default 3000; the Ed25519 ones are 1/10 of it because each costs milliseconds).
#include "test_framework.hpp"
#include <okay/Crypto/Crypto.hpp>
#include <sodium.h>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace okay::crypto;
using Bytes = std::vector<std::uint8_t>;

static std::uint64_t g_s = 0x243f6a8885a308d3ull;
static std::uint32_t Rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return static_cast<std::uint32_t>(g_s >> 16); }
// (Capacity of at least one byte, so data() is never null: libsodium declares some pointers non-null
// even for zero lengths, and the sanitizer build holds the test to that.)
static Bytes Buf(std::size_t n) { Bytes b; b.reserve(n + 1); b.resize(n); return b; }
static Bytes RndBytes(std::size_t n) { Bytes b = Buf(n); for (auto& x : b) x = static_cast<std::uint8_t>(Rnd()); return b; }
static std::size_t RndLen() {                 // biased to the interesting sizes
    switch (Rnd() % 4) {
        case 0: return Rnd() % 17;
        case 1: return 60 + Rnd() % 10;
        case 2: return 120 + Rnd() % 20;
        default: return Rnd() % 700;
    }
}
static Bytes Hex(const char* s) {
    Bytes o;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (; s[0] && s[1]; s += 2) o.push_back(static_cast<std::uint8_t>(nib(s[0]) << 4 | nib(s[1])));
    return o;
}

int main() {
    RUN_SUITE("crypto_sodium_xcheck");
    CHECK(sodium_init() >= 0);
    int N = 3000;
    if (const char* e = std::getenv("OKAY_CRYPTO_XCHECK_N")) N = std::atoi(e);
    const int NE = N / 10 > 0 ? N / 10 : 1;
    std::cout << "libsodium " << sodium_version_string() << ", " << N << " cases per primitive\n";

    // ChaCha20 stream
    for (int i = 0; i < N; ++i) {
        Bytes key = RndBytes(32), nonce = RndBytes(12), in = RndBytes(RndLen());
        const std::uint32_t ctr = (i % 5 == 0) ? 0xfffffffeu : Rnd();
        if (static_cast<std::uint64_t>(ctr) + (in.size() + 63) / 64 > 0xffffffffull) continue;   // libsodium aborts on a counter wrap
        Bytes a = Buf(in.size()), b = Buf(in.size());
        ChaCha20Xor(a.data(), in.data(), in.size(), key.data(), nonce.data(), ctr);
        crypto_stream_chacha20_ietf_xor_ic(b.data(), in.data(), in.size(), nonce.data(), ctr, key.data());
        CHECK(a == b);
    }
    // HChaCha20
    for (int i = 0; i < N; ++i) {
        Bytes key = RndBytes(32), in = RndBytes(16), a(32), b(32);
        HChaCha20(a.data(), key.data(), in.data());
        crypto_core_hchacha20(b.data(), in.data(), key.data(), nullptr);
        CHECK(a == b);
    }
    // Poly1305
    for (int i = 0; i < N; ++i) {
        Bytes key = RndBytes(32), m = RndBytes(RndLen()), a(16), b(16);
        if (i % 7 == 0) std::fill(m.begin(), m.end(), 0xff);
        Poly1305Mac(a.data(), m.data(), m.size(), key.data());
        crypto_onetimeauth(b.data(), m.data(), m.size(), key.data());
        CHECK(a == b);
    }
    // AEAD, IETF ChaCha20-Poly1305 and XChaCha20-Poly1305, both directions
    for (int i = 0; i < N; ++i) {
        Bytes key = RndBytes(32), m = RndBytes(RndLen()), ad = RndBytes(Rnd() % 4 == 0 ? 0 : RndLen() % 80);
        const bool x = (i & 1) != 0;
        Bytes nonce = RndBytes(x ? 24 : 12);
        Bytes ours(m.size() + 16), theirs(m.size() + 16 + 16);
        unsigned long long tl = 0;
        const std::uint8_t* adp = ad.empty() ? nullptr : ad.data();
        if (x) {
            XChaCha20Poly1305Seal(ours.data(), m.data(), m.size(), adp, ad.size(), nonce.data(), key.data());
            crypto_aead_xchacha20poly1305_ietf_encrypt(theirs.data(), &tl, m.data(), m.size(), adp, ad.size(), nullptr, nonce.data(), key.data());
        } else {
            ChaCha20Poly1305Seal(ours.data(), m.data(), m.size(), adp, ad.size(), nonce.data(), key.data());
            crypto_aead_chacha20poly1305_ietf_encrypt(theirs.data(), &tl, m.data(), m.size(), adp, ad.size(), nullptr, nonce.data(), key.data());
        }
        CHECK(tl == m.size() + 16);
        CHECK(std::memcmp(ours.data(), theirs.data(), m.size() + 16) == 0);
        // theirs opens ours
        Bytes back(m.size() + 1);
        unsigned long long ml = 0;
        int rc = x ? crypto_aead_xchacha20poly1305_ietf_decrypt(back.data(), &ml, nullptr, ours.data(), ours.size(), adp, ad.size(), nonce.data(), key.data())
                   : crypto_aead_chacha20poly1305_ietf_decrypt(back.data(), &ml, nullptr, ours.data(), ours.size(), adp, ad.size(), nonce.data(), key.data());
        CHECK(rc == 0); CHECK(ml == m.size()); CHECK(std::memcmp(back.data(), m.data(), m.size()) == 0);
        // ours opens theirs
        Bytes back2(m.size() + 1);
        bool ok = x ? XChaCha20Poly1305Open(back2.data(), theirs.data(), m.size() + 16, adp, ad.size(), nonce.data(), key.data())
                    : ChaCha20Poly1305Open(back2.data(), theirs.data(), m.size() + 16, adp, ad.size(), nonce.data(), key.data());
        CHECK(ok); CHECK(std::memcmp(back2.data(), m.data(), m.size()) == 0);
        // a flipped bit: both reject
        Bytes bad = ours; const std::size_t bit = Rnd() % (bad.size() * 8); bad[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
        rc = x ? crypto_aead_xchacha20poly1305_ietf_decrypt(back.data(), &ml, nullptr, bad.data(), bad.size(), adp, ad.size(), nonce.data(), key.data())
               : crypto_aead_chacha20poly1305_ietf_decrypt(back.data(), &ml, nullptr, bad.data(), bad.size(), adp, ad.size(), nonce.data(), key.data());
        CHECK(rc != 0);
        ok = x ? XChaCha20Poly1305Open(back2.data(), bad.data(), bad.size(), adp, ad.size(), nonce.data(), key.data())
               : ChaCha20Poly1305Open(back2.data(), bad.data(), bad.size(), adp, ad.size(), nonce.data(), key.data());
        CHECK(!ok);
    }
    // BLAKE2b with random output length and optional key, and streaming
    for (int i = 0; i < N; ++i) {
        const std::size_t outLen = 16 + Rnd() % 49;          // libsodium's range: 16..64
        const std::size_t keyLen = (Rnd() % 2) ? 0 : 16 + Rnd() % 49;
        Bytes key = RndBytes(keyLen), m = RndBytes(RndLen()), a(outLen), b(outLen);
        Blake2bHash(a.data(), outLen, m.data(), m.size(), keyLen ? key.data() : nullptr, keyLen);
        crypto_generichash(b.data(), outLen, m.data(), m.size(), keyLen ? key.data() : nullptr, keyLen);
        CHECK(a == b);
        Blake2b s(outLen, keyLen ? key.data() : nullptr, keyLen);
        const std::size_t cut = m.empty() ? 0 : Rnd() % (m.size() + 1);
        s.Update(m.data(), cut); s.Update(m.data() + cut, m.size() - cut);
        Bytes c(outLen);
        s.Final(c.data());
        CHECK(c == b);
    }
    // SHA-2
    for (int i = 0; i < N; ++i) {
        Bytes m = RndBytes(RndLen()), a(32), b(32), c(64), d(64);
        Sha256Hash(a.data(), m.data(), m.size()); crypto_hash_sha256(b.data(), m.data(), m.size());
        Sha512Hash(c.data(), m.data(), m.size()); crypto_hash_sha512(d.data(), m.data(), m.size());
        CHECK(a == b); CHECK(c == d);
    }
    // X25519: random scalars (clamping is done by both) and random points, including
    // top-bit-set and non-canonical u; libsodium refuses an all-zero result, so do we.
    for (int i = 0; i < N; ++i) {
        Bytes s = RndBytes(32), u = RndBytes(32), a(32), b(32);
        if (i % 11 == 0) u[31] |= 0x80;
        if (i % 13 == 0) { std::fill(u.begin() + 1, u.begin() + 31, 0xff); u[31] = 0x7f; u[0] = static_cast<std::uint8_t>(0xed + Rnd() % 19); }   // u near / above p
        const bool okA = X25519(a.data(), s.data(), u.data());
        const int rc = crypto_scalarmult_curve25519(b.data(), s.data(), u.data());
        CHECK(okA == (rc == 0));
        if (rc == 0) CHECK(a == b);
        X25519Base(a.data(), s.data());
        crypto_scalarmult_curve25519_base(b.data(), s.data());
        CHECK(a == b);
    }
    {   // the small-order points: libsodium refuses them, so must we
        const char* pts[] = {"0000000000000000000000000000000000000000000000000000000000000000",
                             "0100000000000000000000000000000000000000000000000000000000000000",
                             "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800",
                             "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157",
                             "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
                             "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
                             "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"};
        for (const char* h : pts) {
            Bytes u = Hex(h), s = RndBytes(32), a(32), b(32);
            CHECK(X25519(a.data(), s.data(), u.data()) == (crypto_scalarmult_curve25519(b.data(), s.data(), u.data()) == 0));
        }
    }
    // Kx
    for (int i = 0; i < N / 3; ++i) {
        Bytes cpk(32), csk(32), spk(32), ssk(32);
        KxKeypair(cpk.data(), csk.data()); KxKeypair(spk.data(), ssk.data());
        Bytes o_crx(32), o_ctx(32), o_srx(32), o_stx(32), t_crx(32), t_ctx(32), t_srx(32), t_stx(32);
        CHECK(KxClientSessionKeys(o_crx.data(), o_ctx.data(), cpk.data(), csk.data(), spk.data()));
        CHECK(KxServerSessionKeys(o_srx.data(), o_stx.data(), spk.data(), ssk.data(), cpk.data()));
        CHECK(crypto_kx_client_session_keys(t_crx.data(), t_ctx.data(), cpk.data(), csk.data(), spk.data()) == 0);
        CHECK(crypto_kx_server_session_keys(t_srx.data(), t_stx.data(), spk.data(), ssk.data(), cpk.data()) == 0);
        CHECK(o_crx == t_crx); CHECK(o_ctx == t_ctx); CHECK(o_srx == t_srx); CHECK(o_stx == t_stx);
        // our public key from our keypair equals libsodium's from the same secret (crypto_kx_keypair is scalarmult_base)
        Bytes tpk(32);
        crypto_scalarmult_base(tpk.data(), csk.data());
        CHECK(tpk == cpk);
    }
    // Ed25519: same keys and signatures from a seed; each side verifies the other's.
    for (int i = 0; i < NE; ++i) {
        Bytes seed = RndBytes(32), m = RndBytes(RndLen());
        Bytes opk(32), osk(64), tpk(32), tsk(64), osig(64), tsig(64);
        Ed25519SeedKeypair(opk.data(), osk.data(), seed.data());
        crypto_sign_seed_keypair(tpk.data(), tsk.data(), seed.data());
        CHECK(opk == tpk); CHECK(osk == tsk);
        Ed25519Sign(osig.data(), m.data(), m.size(), osk.data());
        crypto_sign_detached(tsig.data(), nullptr, m.data(), m.size(), tsk.data());
        CHECK(osig == tsig);
        CHECK(crypto_sign_verify_detached(osig.data(), m.data(), m.size(), tpk.data()) == 0);   // theirs verifies ours
        CHECK(Ed25519Verify(tsig.data(), m.data(), m.size(), opk.data()));                       // ours verifies theirs
        // random corruption: both agree it is rejected
        Bytes bad = osig; const std::size_t bit = Rnd() % 512; bad[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
        CHECK(crypto_sign_verify_detached(bad.data(), m.data(), m.size(), tpk.data()) != 0);
        CHECK(!Ed25519Verify(bad.data(), m.data(), m.size(), opk.data()));
    }
    // Ed25519 on garbage and malformed encodings: accept/reject must match libsodium exactly.
    {
        Bytes seed = RndBytes(32), m = RndBytes(40), pk(32), sk(64), sig(64);
        Ed25519SeedKeypair(pk.data(), sk.data(), seed.data());
        Ed25519Sign(sig.data(), m.data(), m.size(), sk.data());
        const char* small[] = {
            "0100000000000000000000000000000000000000000000000000000000000000",   // identity
            "0100000000000000000000000000000000000000000000000000000000000080",   // identity, sign bit set
            "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",   // order 2 (y = -1)
            "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            "0000000000000000000000000000000000000000000000000000000000000000",   // order 4
            "0000000000000000000000000000000000000000000000000000000000000080",
            "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",   // order 8
            "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
            "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
            "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
            "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",   // y = p (non-canonical)
            "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            "efffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"};
        std::vector<Bytes> encs;
        for (const char* h : small) encs.push_back(Hex(h));
        for (int y = 0; y < 60; ++y) { Bytes e(32, 0); e[0] = static_cast<std::uint8_t>(y); encs.push_back(e); e[31] = 0x80; encs.push_back(e); }
        for (const Bytes& e : encs) {
            for (int variant = 0; variant < 4; ++variant) {
                Bytes s = sig, p = pk;
                if (variant == 0) p = e;                                  // weird public key, real signature
                if (variant == 1) std::memcpy(s.data(), e.data(), 32);    // weird R
                if (variant == 2) { std::memcpy(s.data(), e.data(), 32); p = e; std::memset(s.data() + 32, 0, 32); }   // forgery shape
                if (variant == 3) { p = e; std::memcpy(s.data(), e.data(), 32); }
                const bool t = crypto_sign_verify_detached(s.data(), m.data(), m.size(), p.data()) == 0;
                const bool o = Ed25519Verify(s.data(), m.data(), m.size(), p.data());
                CHECK(t == o);
            }
        }
        // S >= L: S + L, S + 2L, and S with the top bits set.
        const std::uint8_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
        for (int k = 1; k <= 3; ++k) {
            Bytes s = sig;
            for (int rep = 0; rep < k; ++rep) {
                unsigned carry = 0;
                for (int j = 0; j < 32; ++j) { unsigned v = s[32 + j] + kL[j] + carry; s[32 + j] = static_cast<std::uint8_t>(v); carry = v >> 8; }
            }
            CHECK((crypto_sign_verify_detached(s.data(), m.data(), m.size(), pk.data()) == 0) == Ed25519Verify(s.data(), m.data(), m.size(), pk.data()));
        }
        // random garbage signatures and keys
        for (int i = 0; i < NE; ++i) {
            Bytes s = RndBytes(64), p = RndBytes(32);
            CHECK((crypto_sign_verify_detached(s.data(), m.data(), m.size(), p.data()) == 0) == Ed25519Verify(s.data(), m.data(), m.size(), p.data()));
            CHECK((crypto_sign_verify_detached(s.data(), m.data(), m.size(), pk.data()) == 0) == Ed25519Verify(s.data(), m.data(), m.size(), pk.data()));
        }
    }
    TEST_MAIN_RESULT();
}
