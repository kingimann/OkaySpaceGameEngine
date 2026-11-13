// Large differential test: okay::crypto against libsodium (every primitive) and against the TweetNaCl-derived port in
// shared/AntiCheatCrypto.hpp (Ed25519 and SHA-512, the only things the port has), on millions of random AND structured inputs.
//
//   crypto_difftest [N]        N from the argument, else $OKAY_DIFF_N, else 1500 (the ctest smoke size). $OKAY_DIFF_SEED sets the seed.
//
// Counts per category (N = 1,000,000 is a ten-minute run on one core; run it under -DOKAY_SANITIZE=address,undefined with a smaller N):
//   X25519 scalar mult, structured + random        N         ours vs libsodium (accept/refuse flag AND output)
//   X25519 base                                    N/10
//   Ed25519 public key + sign + verify             N/2       ours vs libsodium, signatures must be byte-identical (deterministic)
//   Ed25519 verify of mutated/structured inputs    N         ours vs libsodium (accept/reject), every bit of R,S,A, structured R,S,A
//   Ed25519 vs the port: pk, sign, verify, weak    N/50      (the port is ~50 times slower)
//   AEAD (both), every truncation and bit flip     N/40 messages, ~1000 opens each, ours vs libsodium
//   SHA-256, SHA-512, BLAKE2b (all lengths/keys)   N         ours vs libsodium (SHA-512 also vs the port)
//   Poly1305 with edge-case keys and messages      N         ours vs libsodium
//   Kx session keys                                N/20      ours vs libsodium crypto_kx
// Every mismatch is printed (first 25 per category) and makes the exit status 1.
//
// Structured inputs are 32-byte little-endian values near the field and group boundaries: 0, 1, p-1, p, p+1, p+18, 2^255-1, 2^255,
// 2^256-1, 2^255-19 +- k, powers of two, the group order L +- k, the eight small-order point encodings with and without the
// sign bit, non-canonical y encodings (y + p), runs of 0x00/0x7f/0x80/0xff, and one-bit and one-byte variants of all of them.
#include <okay/Crypto/Crypto.hpp>
#include <sodium.h>

#include "AntiCheatCrypto.hpp"   // the port (portable::), compiled without sodium/own-crypto defines

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace oc = okay::crypto;
using Bytes = std::vector<std::uint8_t>;
namespace port = okayac::ed25519::portable;

static std::uint64_t g_s = 0x9e3779b97f4a7c15ull;
static std::uint64_t Rnd64() {   // xorshift64*
    g_s ^= g_s >> 12; g_s ^= g_s << 25; g_s ^= g_s >> 27;
    return g_s * 0x2545F4914F6CDD1Dull;
}
static std::uint32_t Rnd(std::uint32_t n) { return static_cast<std::uint32_t>((Rnd64() >> 20) % n); }
static Bytes Buf(std::size_t n) { Bytes b; b.reserve(n + 1); b.resize(n); return b; }
static Bytes RndBytes(std::size_t n) { Bytes b = Buf(n); for (auto& x : b) x = static_cast<std::uint8_t>(Rnd64() >> 33); return b; }
static std::string Hx(const std::uint8_t* p, std::size_t n) {
    std::string s;
    char t[3];
    for (std::size_t i = 0; i < n; ++i) { std::snprintf(t, sizeof t, "%02x", p[i]); s += t; }
    return s;
}

struct Cat {
    const char* name;
    long long runs = 0, bad = 0;
};
static std::vector<Cat*> g_cats;
static Cat* NewCat(const char* n) { g_cats.push_back(new Cat{n}); return g_cats.back(); }
static void Mismatch(Cat* c, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void Mismatch(Cat* c, const char* fmt, ...) {
    ++c->bad;
    if (c->bad > 25) return;
    std::printf("MISMATCH [%s] ", c->name);
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}

// ---------------------------------------------------------------- structured 32-byte values
// little-endian 32-byte from a big number given as: (2^255 - 19) + delta (delta may be negative), wrapping 2^256
static Bytes PPlus(long long delta, bool top = false) {
    Bytes b = Buf(32);
    // p = 2^255 - 19: bytes ed ff ... ff 7f
    b[0] = 0xed; for (int i = 1; i < 31; ++i) b[i] = 0xff; b[31] = 0x7f;
    long long carry = delta;
    for (int i = 0; i < 32 && carry != 0; ++i) {
        long long v = static_cast<long long>(b[i]) + carry;
        b[i] = static_cast<std::uint8_t>(v & 0xff);
        carry = v >> 8;   // arithmetic shift: floor division, right for negative deltas
    }
    if (top) b[31] |= 0x80;
    return b;
}
static Bytes LPlus(int delta) {   // group order L = 2^252 + 27742317777372353535851937790883648493, plus delta
    static const std::uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
    Bytes b(L, L + 32);
    int carry = delta;
    for (int i = 0; i < 32 && carry != 0; ++i) {
        int v = b[i] + carry;
        b[i] = static_cast<std::uint8_t>(v & 0xff);
        carry = v >> 8;
    }
    return b;
}
static Bytes Fill(std::uint8_t v) { return Bytes(32, v); }
static Bytes Pow2(int bit) { Bytes b = Buf(32); b[static_cast<std::size_t>(bit / 8)] = static_cast<std::uint8_t>(1u << (bit % 8)); return b; }

static const char* kSmallOrder[] = {   // the libsodium blocklist (canonical encodings of the points of order 1, 2, 4, 8 and their non-canonical twins)
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0100000000000000000000000000000000000000000000000000000000000000",
    "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
    "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
    "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
    "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
    "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
    "0000000000000000000000000000000000000000000000000000000000000080",
    "0100000000000000000000000000000000000000000000000000000000000080",
    "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
    "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa",
    "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
    "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
    "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
};
static Bytes FromHex(const char* s) {
    Bytes o;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (; s[0] && s[1]; s += 2) o.push_back(static_cast<std::uint8_t>(nib(s[0]) << 4 | nib(s[1])));
    return o;
}

static std::vector<Bytes> g_struct;   // the base structured set
static void Add(Bytes b) { g_struct.push_back(std::move(b)); }
static void BuildStructured() {
    for (long long d = -40; d <= 40; ++d) { Add(PPlus(d)); Add(PPlus(d, true)); }
    Add(Fill(0)); Add(Fill(1)); Add(Fill(0xff)); Add(Fill(0x7f)); Add(Fill(0x80)); Add(Fill(0x55)); Add(Fill(0xaa));
    { Bytes b = Fill(0xff); b[31] = 0x7f; Add(b); }                       // 2^255 - 1
    { Bytes b = Fill(0); b[31] = 0x80; Add(b); }                          // 2^255
    { Bytes b = Fill(0xff); b[0] = 0xed; b[31] = 0x7f; Add(b); }          // p
    for (int bit = 0; bit < 256; bit += 1) Add(Pow2(bit));
    for (int bit = 0; bit < 256; bit += 7) { Bytes b = Fill(0xff); b[static_cast<std::size_t>(bit / 8)] &= static_cast<std::uint8_t>(~(1u << (bit % 8))); Add(b); }
    for (int d = -20; d <= 20; ++d) { Add(LPlus(d)); Bytes b = LPlus(d); b[31] |= 0x80; Add(b); Bytes c = LPlus(d); c[31] |= 0xe0; Add(c); }
    for (const char* h : kSmallOrder) Add(FromHex(h));
    for (const char* h : kSmallOrder) { Bytes b = FromHex(h); b[0] ^= 1; Add(b); b = FromHex(h); b[31] ^= 0x80; Add(b); b = FromHex(h); b[16] ^= 0x10; Add(b); }
    for (int i = 0; i < 32; i += 3) { Bytes b = Fill(0); b[static_cast<std::size_t>(i)] = 0xff; Add(b); b = Fill(0xff); b[static_cast<std::size_t>(i)] = 0; Add(b); }
    // non-canonical y: small y + p for y in 0..40 (the y values of many real points), with and without the sign bit
    for (int y = 0; y < 40; ++y) { Add(PPlus(19 + y)); Add(PPlus(19 + y, true)); }
    // y of the base point 4/5 and of -B, canonical and with p added
    { Bytes b = Fill(0x66); b[0] = 0x58; Add(b); Bytes c = b; c[31] |= 0x80; Add(c); }
}
// A value from the structured set, optionally nudged (one bit or byte), or fully random.
static Bytes Pick() {
    const std::uint32_t r = Rnd(10);
    if (r < 2) return RndBytes(32);
    Bytes b = g_struct[Rnd(static_cast<std::uint32_t>(g_struct.size()))];
    if (r == 2) b[Rnd(32)] ^= static_cast<std::uint8_t>(1u << Rnd(8));
    if (r == 3) b[Rnd(32)] = static_cast<std::uint8_t>(Rnd64());
    if (r == 4) { b[31] |= 0x80; }
    if (r == 5) { b[31] &= 0x7f; }
    return b;
}
static Bytes PickScalar() {   // also: clamped / unclamped, high bits set, low bits set
    Bytes b = Pick();
    switch (Rnd(6)) { case 0: b[0] &= 248; b[31] &= 127; b[31] |= 64; break; case 1: b[31] |= 0xff; break; case 2: b[0] |= 7; break; default: break; }
    return b;
}

// ---------------------------------------------------------------- X25519
static void DiffX25519(long long n) {
    Cat* c = NewCat("x25519");
    for (long long i = 0; i < n; ++i) {
        const Bytes s = PickScalar(), p = Pick();
        std::uint8_t a[32] = {0}, b[32] = {0};
        const bool ok = oc::X25519(a, s.data(), p.data());
        const bool sok = crypto_scalarmult_curve25519(b, s.data(), p.data()) == 0;
        ++c->runs;
        if (ok != sok || (ok && std::memcmp(a, b, 32) != 0))
            Mismatch(c, "scalar %s point %s: ours %s %s, libsodium %s %s", Hx(s.data(), 32).c_str(), Hx(p.data(), 32).c_str(), ok ? "ok" : "refused",
                     Hx(a, 32).c_str(), sok ? "ok" : "refused", Hx(b, 32).c_str());
    }
    Cat* cb = NewCat("x25519 base");
    for (long long i = 0; i < n / 10; ++i) {
        const Bytes s = PickScalar();
        std::uint8_t a[32], b[32];
        oc::X25519Base(a, s.data());
        crypto_scalarmult_curve25519_base(b, s.data());
        ++cb->runs;
        if (std::memcmp(a, b, 32) != 0) Mismatch(cb, "scalar %s: ours %s libsodium %s", Hx(s.data(), 32).c_str(), Hx(a, 32).c_str(), Hx(b, 32).c_str());
    }
    Cat* ck = NewCat("kx session keys");
    for (long long i = 0; i < n / 20; ++i) {
        std::uint8_t cpk[32], csk[32], spk[32], ssk[32];
        crypto_kx_keypair(cpk, csk);
        crypto_kx_keypair(spk, ssk);
        if (i % 7 == 0) { const Bytes q = Pick(); std::memcpy(spk, q.data(), 32); }   // structured / hostile server key
        std::uint8_t crx[32], ctx[32], orx[32], otx[32];
        const bool ok = oc::KxClientSessionKeys(crx, ctx, cpk, csk, spk);
        const bool sok = crypto_kx_client_session_keys(orx, otx, cpk, csk, spk) == 0;
        ++ck->runs;
        if (ok != sok || (ok && (std::memcmp(crx, orx, 32) || std::memcmp(ctx, otx, 32)))) Mismatch(ck, "client keys differ (server pk %s)", Hx(spk, 32).c_str());
        if (i % 7 == 0) continue;
        std::uint8_t srx[32], stx[32], sorx[32], sotx[32];
        const bool ok2 = oc::KxServerSessionKeys(srx, stx, spk, ssk, cpk);
        const bool sok2 = crypto_kx_server_session_keys(sorx, sotx, spk, ssk, cpk) == 0;
        if (ok2 != sok2 || (ok2 && (std::memcmp(srx, sorx, 32) || std::memcmp(stx, sotx, 32)))) Mismatch(ck, "server keys differ");
    }
}

// ---------------------------------------------------------------- Ed25519
static bool SodiumVerify(const Bytes& sig, const Bytes& m, const Bytes& pk) {
    return crypto_sign_verify_detached(sig.data(), m.data(), m.size(), pk.data()) == 0;
}
static bool OurVerify(const Bytes& sig, const Bytes& m, const Bytes& pk) { return oc::Ed25519Verify(sig.data(), m.data(), m.size(), pk.data()); }

static void DiffEd25519(long long n) {
    Cat* cs = NewCat("ed25519 pk+sign+verify");
    for (long long i = 0; i < n / 2; ++i) {
        const Bytes seed = (i % 16 == 0) ? Pick() : RndBytes(32);
        const Bytes m = RndBytes((i % 5 == 0) ? 0 : Rnd(300));
        std::uint8_t opk[32], osk[64], spk[32], ssk[64];
        oc::Ed25519SeedKeypair(opk, osk, seed.data());
        crypto_sign_seed_keypair(spk, ssk, seed.data());
        ++cs->runs;
        if (std::memcmp(opk, spk, 32) || std::memcmp(osk, ssk, 64)) { Mismatch(cs, "keypair from seed %s", Hx(seed.data(), 32).c_str()); continue; }
        Bytes osig = Buf(64), ssig = Buf(64);
        oc::Ed25519Sign(osig.data(), m.data(), m.size(), osk);
        crypto_sign_detached(ssig.data(), nullptr, m.data(), m.size(), ssk);
        if (osig != ssig) { Mismatch(cs, "signature differs: seed %s msg len %zu", Hx(seed.data(), 32).c_str(), m.size()); continue; }
        const Bytes pk(opk, opk + 32);
        if (!OurVerify(osig, m, pk) || !SodiumVerify(osig, m, pk)) Mismatch(cs, "valid signature rejected by one side (seed %s)", Hx(seed.data(), 32).c_str());
        // one wrong message must fail on both
        Bytes m2 = m;
        m2.push_back(0);
        if (OurVerify(osig, m2, pk) != SodiumVerify(osig, m2, pk)) Mismatch(cs, "extended message verdict differs");
    }

    // Mutated and structured verification inputs: the verdict must match libsodium's.
    Cat* cv = NewCat("ed25519 verify mutated");
    long long done = 0;
    while (done < n) {
        const Bytes seed = RndBytes(32), m = RndBytes(Rnd(40));
        std::uint8_t pkb[32], sk[64];
        oc::Ed25519SeedKeypair(pkb, sk, seed.data());
        Bytes sig = Buf(64);
        oc::Ed25519Sign(sig.data(), m.data(), m.size(), sk);
        const Bytes pk(pkb, pkb + 32);
        auto both = [&](const Bytes& sg, const Bytes& msg, const Bytes& key, const char* what) {
            const bool a = OurVerify(sg, msg, key), b = SodiumVerify(sg, msg, key);
            ++cv->runs; ++done;
            if (a != b) Mismatch(cv, "%s: ours %s, libsodium %s: sig %s pk %s msg %s", what, a ? "accepts" : "rejects", b ? "accepts" : "rejects", Hx(sg.data(), 64).c_str(),
                                 Hx(key.data(), 32).c_str(), Hx(msg.data(), msg.size()).c_str());
        };
        both(sig, m, pk, "valid");
        // every single bit of the signature (512) and a stripe of public-key bits
        for (int bit = 0; bit < 512; ++bit) { Bytes s2 = sig; s2[static_cast<std::size_t>(bit / 8)] ^= static_cast<std::uint8_t>(1u << (bit % 8)); both(s2, m, pk, "sig bit flip"); }
        for (int bit = 0; bit < 256; bit += 5) { Bytes k2 = pk; k2[static_cast<std::size_t>(bit / 8)] ^= static_cast<std::uint8_t>(1u << (bit % 8)); both(sig, m, k2, "pk bit flip"); }
        // structured S (valid R): S = L-1, L, L+1, 2^252.., all-ones, 0, s + L (malleable twin)
        for (int k = 0; k < 24; ++k) { Bytes s2 = sig; const Bytes S = Pick(); std::memcpy(s2.data() + 32, S.data(), 32); both(s2, m, pk, "structured S"); }
        { Bytes s2 = sig; const Bytes l = LPlus(0); int carry = 0; for (int i = 0; i < 32; ++i) { int v = s2[32 + static_cast<std::size_t>(i)] + l[static_cast<std::size_t>(i)] + carry; s2[32 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v); carry = v >> 8; } both(s2, m, pk, "S + L malleable twin"); }
        // structured R, structured pk, with the real S and random S
        for (int k = 0; k < 24; ++k) { Bytes s2 = sig; const Bytes R = Pick(); std::memcpy(s2.data(), R.data(), 32); both(s2, m, pk, "structured R"); }
        for (int k = 0; k < 24; ++k) { Bytes s2 = sig; const Bytes R = Pick(); const Bytes A = Pick(); std::memcpy(s2.data(), R.data(), 32); if (k & 1) std::memset(s2.data() + 32, 0, 32); both(s2, m, A, "structured R and A"); }
        for (int k = 0; k < 8; ++k) both(sig, m, Pick(), "structured A");
    }
}

static void DiffEdPort(long long n) {
    Cat* c = NewCat("ed25519 vs port");
    Cat* cw = NewCat("ed25519 weak key vs port");
    for (long long i = 0; i < n; ++i) {
        const Bytes seed = (i % 8 == 0) ? Pick() : RndBytes(32), m = RndBytes(Rnd(200));
        okayac::ed25519::Seed ps;
        std::memcpy(ps.data(), seed.data(), 32);
        const auto ppk = port::PublicKeyFromSeed(ps);
        const auto psig = port::Sign(m.data(), m.size(), ps);
        std::uint8_t opk[32], osk[64];
        oc::Ed25519SeedKeypair(opk, osk, seed.data());
        Bytes osig = Buf(64);
        oc::Ed25519Sign(osig.data(), m.data(), m.size(), osk);
        ++c->runs;
        if (std::memcmp(opk, ppk.data(), 32)) Mismatch(c, "public key from seed %s", Hx(seed.data(), 32).c_str());
        if (std::memcmp(osig.data(), psig.data(), 64)) Mismatch(c, "signature from seed %s", Hx(seed.data(), 32).c_str());
        // verdicts on mutated inputs
        for (int k = 0; k < 6; ++k) {
            Bytes sg = osig, key(opk, opk + 32);
            switch (k) {
                case 0: break;
                case 1: sg[Rnd(64)] ^= static_cast<std::uint8_t>(1u << Rnd(8)); break;
                case 2: { const Bytes S = Pick(); std::memcpy(sg.data() + 32, S.data(), 32); break; }
                case 3: { const Bytes R = Pick(); std::memcpy(sg.data(), R.data(), 32); break; }
                case 4: key = Pick(); break;
                default: key[Rnd(32)] ^= static_cast<std::uint8_t>(1u << Rnd(8)); break;
            }
            okayac::ed25519::Signature psg;
            okayac::ed25519::PublicKey pkk;
            std::memcpy(psg.data(), sg.data(), 64);
            std::memcpy(pkk.data(), key.data(), 32);
            const bool a = OurVerify(sg, m, key), b = port::Verify(m.data(), m.size(), psg, pkk);
            ++c->runs;
            if (a != b) Mismatch(c, "verdict case %d: ours %s, port %s: sig %s pk %s", k, a ? "accepts" : "rejects", b ? "accepts" : "rejects", Hx(sg.data(), 64).c_str(), Hx(key.data(), 32).c_str());
        }
    }
    // weak-key classification is cheap: many more
    for (long long i = 0; i < n * 20; ++i) {
        const Bytes k = Pick();
        okayac::ed25519::PublicKey pkk;
        std::memcpy(pkk.data(), k.data(), 32);
        const bool a = oc::Ed25519IsWeakKey(k.data()), b = port::IsWeakKey(pkk);
        ++cw->runs;
        if (a != b) Mismatch(cw, "key %s: ours %s port %s", Hx(k.data(), 32).c_str(), a ? "weak" : "ok", b ? "weak" : "ok");
    }
}

// ---------------------------------------------------------------- AEAD
static void DiffAead(long long msgs) {
    Cat* c = NewCat("aead");
    for (int variant = 0; variant < 2; ++variant) {
        const bool x = variant == 1;
        const std::size_t nl = x ? 24 : 12;
        for (long long i = 0; i < msgs; ++i) {
            Bytes key = (i % 9 == 0) ? Pick() : RndBytes(32), nonce = RndBytes(nl), aad = RndBytes(Rnd(4) == 0 ? 0 : Rnd(40));
            Bytes m = RndBytes(Rnd(4) == 0 ? Rnd(3) : Rnd(90));
            if (i % 11 == 0) for (auto& b : m) b = 0xff;     // all-ones message: Poly1305 carry edge
            Bytes a = Buf(m.size() + 16), b = Buf(m.size() + 16);
            unsigned long long bl = 0;
            if (x) {
                oc::XChaCha20Poly1305Seal(a.data(), m.data(), m.size(), aad.data(), aad.size(), nonce.data(), key.data());
                crypto_aead_xchacha20poly1305_ietf_encrypt(b.data(), &bl, m.data(), m.size(), aad.data(), aad.size(), nullptr, nonce.data(), key.data());
            } else {
                oc::ChaCha20Poly1305Seal(a.data(), m.data(), m.size(), aad.data(), aad.size(), nonce.data(), key.data());
                crypto_aead_chacha20poly1305_ietf_encrypt(b.data(), &bl, m.data(), m.size(), aad.data(), aad.size(), nullptr, nonce.data(), key.data());
            }
            ++c->runs;
            if (a != b) { Mismatch(c, "%s seal differs (msg len %zu)", x ? "xchacha" : "chacha", m.size()); continue; }
            auto openBoth = [&](const Bytes& ct, const Bytes& ad, const Bytes& nn, const Bytes& kk, const char* what) {
                Bytes o1 = Buf(ct.size() + 1), o2 = Buf(ct.size() + 1);
                unsigned long long ol = 0;
                bool r1, r2;
                if (x) {
                    r1 = oc::XChaCha20Poly1305Open(o1.data(), ct.data(), ct.size(), ad.data(), ad.size(), nn.data(), kk.data());
                    r2 = crypto_aead_xchacha20poly1305_ietf_decrypt(o2.data(), &ol, nullptr, ct.data(), ct.size(), ad.data(), ad.size(), nn.data(), kk.data()) == 0;
                } else {
                    r1 = oc::ChaCha20Poly1305Open(o1.data(), ct.data(), ct.size(), ad.data(), ad.size(), nn.data(), kk.data());
                    r2 = crypto_aead_chacha20poly1305_ietf_decrypt(o2.data(), &ol, nullptr, ct.data(), ct.size(), ad.data(), ad.size(), nn.data(), kk.data()) == 0;
                }
                ++c->runs;
                if (r1 != r2) Mismatch(c, "%s open verdict (%s): ours %s libsodium %s, ct len %zu", x ? "xchacha" : "chacha", what, r1 ? "ok" : "fail", r2 ? "ok" : "fail", ct.size());
                else if (r1 && std::memcmp(o1.data(), o2.data(), ct.size() - 16) != 0) Mismatch(c, "%s open plaintext differs (%s)", x ? "xchacha" : "chacha", what);
                // a failed open must leave no plaintext behind
                if (!r1) for (std::size_t k = 0; k + 1 < o1.size(); ++k) if (o1[k] != 0) { Mismatch(c, "failed open left plaintext (%s)", what); break; }
                return r1;
            };
            if (!openBoth(a, aad, nonce, key, "intact")) Mismatch(c, "intact message does not open");
            for (std::size_t t = 0; t < a.size(); ++t) {    // every truncation (including the empty and the short-of-tag ones)
                const Bytes cut(a.begin(), a.begin() + static_cast<long>(t));
                if (openBoth(cut, aad, nonce, key, "truncated")) Mismatch(c, "truncated message opened (len %zu of %zu)", t, a.size());
            }
            { Bytes ext = a; ext.push_back(0); if (openBoth(ext, aad, nonce, key, "extended")) Mismatch(c, "extended message opened"); }
            for (std::size_t bit = 0; bit < a.size() * 8; ++bit) {     // every bit of ciphertext and tag
                Bytes f = a;
                f[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
                if (openBoth(f, aad, nonce, key, "bit flip")) Mismatch(c, "flipped message opened (bit %zu)", bit);
            }
            if (!aad.empty()) for (std::size_t bit = 0; bit < aad.size() * 8; bit += 3) {
                Bytes f = aad; f[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
                if (openBoth(a, f, nonce, key, "aad flip")) Mismatch(c, "flipped aad accepted");
            }
            { Bytes f = aad; f.push_back(0); if (openBoth(a, f, nonce, key, "aad extended")) Mismatch(c, "extended aad accepted"); }
            for (std::size_t bit = 0; bit < nonce.size() * 8; bit += 5) {
                Bytes f = nonce; f[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
                if (openBoth(a, aad, f, key, "nonce flip")) Mismatch(c, "flipped nonce accepted");
            }
            for (std::size_t bit = 0; bit < 256; bit += 9) {
                Bytes f = key; f[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
                if (openBoth(a, aad, nonce, f, "key flip")) Mismatch(c, "flipped key accepted");
            }
        }
    }
}

// ---------------------------------------------------------------- hashes and Poly1305
static void DiffHash(long long n) {
    Cat* c = NewCat("hashes");
    Cat* cp = NewCat("sha512 vs port");
    Cat* cm = NewCat("poly1305");
    for (long long i = 0; i < n; ++i) {
        std::size_t len = i < 700 ? static_cast<std::size_t>(i) : Rnd(3) == 0 ? Rnd(40) : Rnd(1000);   // every length 0..699 once
        Bytes m = RndBytes(len);
        if (i % 13 == 0) for (auto& b : m) b = 0xff;
        if (i % 17 == 0) for (auto& b : m) b = 0;
        std::uint8_t a[64], b[64];
        oc::Sha256Hash(a, m.data(), m.size());
        crypto_hash_sha256(b, m.data(), m.size());
        ++c->runs;
        if (std::memcmp(a, b, 32)) Mismatch(c, "sha256 len %zu", len);
        oc::Sha512Hash(a, m.data(), m.size());
        crypto_hash_sha512(b, m.data(), m.size());
        if (std::memcmp(a, b, 64)) Mismatch(c, "sha512 len %zu", len);
        if (i % 40 == 0) {
            const auto pd = okayac::sha512::HashPortable(m.data(), m.size());
            ++cp->runs;
            if (std::memcmp(a, pd.data(), 64)) Mismatch(cp, "sha512 len %zu", len);
        }
        // streaming in random chunks must equal one-shot
        {
            oc::Sha512 s5; oc::Sha256 s2;
            std::size_t off = 0;
            while (off < m.size()) { const std::size_t t = 1 + Rnd(150); const std::size_t k = std::min(t, m.size() - off); s5.Update(m.data() + off, k); s2.Update(m.data() + off, k); off += k; }
            std::uint8_t d5[64], d2[32], e2[32];
            s5.Final(d5); s2.Final(d2);
            oc::Sha256Hash(e2, m.data(), m.size());
            if (std::memcmp(d5, b, 64) || std::memcmp(d2, e2, 32)) Mismatch(c, "streaming differs from one-shot, len %zu", len);
        }
        const std::size_t olen = 1 + Rnd(64), klen = Rnd(3) == 0 ? 0 : Rnd(65);
        const Bytes key = RndBytes(klen);
        std::uint8_t g1[64], g2[64];
        oc::Blake2bHash(g1, olen, m.data(), m.size(), klen ? key.data() : nullptr, klen);
        crypto_generichash_blake2b(g2, olen, m.data(), m.size(), klen ? key.data() : nullptr, klen);
        if (std::memcmp(g1, g2, olen)) Mismatch(c, "blake2b len %zu out %zu key %zu", len, olen, klen);
        // Poly1305: random keys, edge-case keys (r with all clamp bits, s all ones) and messages
        Bytes pk = (i % 3 == 0) ? Pick() : RndBytes(32);
        if (i % 7 == 0) std::memset(pk.data(), 0xff, 16);
        if (i % 11 == 0) std::memset(pk.data(), 0, 16);
        if (i % 5 == 0) std::memset(pk.data() + 16, 0xff, 16);
        std::uint8_t t1[16], t2[16];
        oc::Poly1305Mac(t1, m.data(), m.size(), pk.data());
        crypto_onetimeauth_poly1305(t2, m.data(), m.size(), pk.data());
        ++cm->runs;
        if (std::memcmp(t1, t2, 16)) Mismatch(cm, "poly1305 key %s len %zu", Hx(pk.data(), 32).c_str(), len);
    }
}

int main(int argc, char** argv) {
    long long N = 1500;
    if (const char* e = std::getenv("OKAY_DIFF_N")) N = std::atoll(e);
    if (argc > 1) N = std::atoll(argv[1]);
    if (const char* e = std::getenv("OKAY_DIFF_SEED")) g_s ^= std::strtoull(e, nullptr, 0) * 0x9e3779b97f4a7c15ull;
    if (sodium_init() < 0) { std::printf("sodium_init failed\n"); return 2; }
    BuildStructured();
    std::printf("crypto_difftest: N=%lld, libsodium %s, %zu structured 32-byte values\n", N, sodium_version_string(), g_struct.size());
    auto t0 = std::chrono::steady_clock::now();
    auto lap = [&](const char* what) {
        const auto t1 = std::chrono::steady_clock::now();
        std::printf("  %-14s done in %.1f s\n", what, std::chrono::duration<double>(t1 - t0).count());
        std::fflush(stdout);
        t0 = t1;
    };
    DiffX25519(N); lap("x25519/kx");
    DiffEd25519(N); lap("ed25519");
    DiffEdPort(N / 50 > 0 ? N / 50 : 1); lap("ed25519 port");
    DiffAead(N / 40 > 0 ? N / 40 : 1); lap("aead");
    DiffHash(N); lap("hashes");
    long long bad = 0, runs = 0;
    for (const Cat* c : g_cats) {
        std::printf("%-28s %12lld comparisons, %lld mismatches\n", c->name, c->runs, c->bad);
        bad += c->bad; runs += c->runs;
    }
    std::printf("total %lld comparisons, %lld mismatches\n", runs, bad);
    return bad ? 1 : 0;
}
