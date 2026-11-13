// okay::crypto known-answer tests: every published vector from RFC 8439, RFC 7748,
// RFC 8032, RFC 7693 and the XChaCha draft (tests/crypto_rfc_vectors.hpp is extracted
// from the RFC text), plus tamper tests (every bit of tag and ciphertext flipped),
// streaming-versus-one-shot checks, and the malformed-input edge cases.
// The code under test is UNREVIEWED (see okay/Crypto/Crypto.hpp).
#include "test_framework.hpp"
#include "crypto_rfc_vectors.hpp"
#include <okay/Crypto/Crypto.hpp>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay::crypto;
using Bytes = std::vector<std::uint8_t>;

static Bytes Hex(const std::string& s) {
    Bytes o;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) o.push_back(static_cast<std::uint8_t>(nib(s[i]) << 4 | nib(s[i + 1])));
    return o;
}
static Bytes V(const char* name) {
    for (const auto& kv : okay_crypto_vectors::kVectors)
        if (std::strcmp(kv.name, name) == 0) return Hex(kv.hex);
    std::cerr << "  missing vector " << name << "\n";
    CHECK(false);
    return {};
}
static std::string N(const char* base, int i, const char* suffix) { return std::string(base) + std::to_string(i) + suffix; }
static Bytes VN(const char* base, int i, const char* suffix) { return V(N(base, i, suffix).c_str()); }

static std::uint64_t g_s = 0x9e3779b97f4a7c15ull;
static std::uint32_t Rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return static_cast<std::uint32_t>(g_s >> 16); }
static Bytes RndBytes(std::size_t n) { Bytes b(n); for (auto& x : b) x = static_cast<std::uint8_t>(Rnd()); return b; }

static std::uint32_t CounterOf(const Bytes& c) { return c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3]; }

int main() {
    RUN_SUITE("crypto");

    // ---- ChaCha20 (RFC 8439 A.1, A.2, 2.4.2) ------------------------------------------
    for (int k = 0; k < 5; ++k) {
        Bytes key = VN("cc_blk", k, "_key"), nonce = VN("cc_blk", k, "_nonce"), ks = VN("cc_blk", k, "_ks");
        Bytes zero(64, 0), out(64);
        ChaCha20Xor(out.data(), zero.data(), 64, key.data(), nonce.data(), CounterOf(VN("cc_blk", k, "_ctr")));
        CHECK(out == ks);
    }
    for (int k = 0; k < 3; ++k) {
        Bytes key = VN("cc_enc", k, "_key"), nonce = VN("cc_enc", k, "_nonce"), pt = VN("cc_enc", k, "_pt"), ct = VN("cc_enc", k, "_ct");
        Bytes out(pt.size());
        ChaCha20Xor(out.data(), pt.data(), pt.size(), key.data(), nonce.data(), CounterOf(VN("cc_enc", k, "_ctr")));
        CHECK(out == ct);
        ChaCha20Xor(out.data(), out.data(), out.size(), key.data(), nonce.data(), CounterOf(VN("cc_enc", k, "_ctr")));   // in place, back
        CHECK(out == pt);
    }
    {
        Bytes key = Hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
        Bytes nonce = Hex("000000000000004a00000000");
        Bytes pt = V("cc_sun_pt"), ct = V("cc_sun_ct"), out(pt.size());
        ChaCha20Xor(out.data(), pt.data(), pt.size(), key.data(), nonce.data(), 1);
        CHECK(out == ct);
    }
    // Lengths around the block size, and the counter carried across a split call.
    {
        Bytes key = RndBytes(32), nonce = RndBytes(12);
        Bytes zero(300, 0), whole(300);
        ChaCha20Xor(whole.data(), zero.data(), 300, key.data(), nonce.data(), 7);
        for (std::size_t n : {0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 255u, 256u, 299u}) {
            Bytes part(300, 0);
            ChaCha20Xor(part.data(), zero.data(), n, key.data(), nonce.data(), 7);
            CHECK(std::memcmp(part.data(), whole.data(), n) == 0);
        }
    }

    // ---- HChaCha20 (xchacha draft 2.2.1) -------------------------------------------------
    {
        Bytes key = V("hch_key"), in = V("hch_in"), out(32);
        HChaCha20(out.data(), key.data(), in.data());
        CHECK(out == V("hch_out"));
    }

    // ---- Poly1305 (RFC 8439 2.5.2, A.3 #1..#11) --------------------------------------------
    {
        Bytes key = V("poly_rfc_key"), msg = V("poly_rfc_msg"), tag(16);
        Poly1305Mac(tag.data(), msg.data(), msg.size(), key.data());
        CHECK(tag == V("poly_rfc_tag"));
    }
    for (int k = 1; k <= 11; ++k) {
        Bytes key = VN("poly", k, "_key"), msg = VN("poly", k, "_msg"), want = VN("poly", k, "_tag"), tag(16);
        Poly1305Mac(tag.data(), msg.data(), msg.size(), key.data());
        CHECK(tag == want);
        // Streaming at every split point gives the same tag.
        for (std::size_t cut = 0; cut <= msg.size(); ++cut) {
            Poly1305 p(key.data());
            p.Update(msg.data(), cut);
            p.Update(msg.data() + cut, msg.size() - cut);
            Bytes t2(16);
            p.Final(t2.data());
            CHECK(t2 == want);
        }
    }
    // Poly1305 one-time keys from ChaCha20 (A.4 and 2.6.2): block counter 0, first 32 bytes.
    for (int k = 0; k < 4; ++k) {
        Bytes key = VN("pkg", k, "_key"), nonce = VN("pkg", k, "_nonce"), want = VN("pkg", k, "_otk");
        Bytes z(64, 0), blk(64);
        ChaCha20Xor(blk.data(), z.data(), 64, key.data(), nonce.data(), 0);
        CHECK(Bytes(blk.begin(), blk.begin() + 32) == want);
    }

    // ---- ChaCha20-Poly1305 AEAD (RFC 8439 2.8.2, A.5) ----------------------------------------
    for (int k = 1; k <= 2; ++k) {
        Bytes key = VN("aead", k, "_key"), nonce = VN("aead", k, "_nonce"), aad = VN("aead", k, "_aad"), pt = VN("aead", k, "_pt"),
              ct = VN("aead", k, "_ct"), tag = VN("aead", k, "_tag");
        Bytes sealed(pt.size() + 16);
        ChaCha20Poly1305Seal(sealed.data(), pt.data(), pt.size(), aad.data(), aad.size(), nonce.data(), key.data());
        Bytes wantAll = ct; wantAll.insert(wantAll.end(), tag.begin(), tag.end());
        CHECK(sealed == wantAll);
        Bytes opened(pt.size());
        CHECK(ChaCha20Poly1305Open(opened.data(), wantAll.data(), wantAll.size(), aad.data(), aad.size(), nonce.data(), key.data()));
        CHECK(opened == pt);
        // Tamper: every bit of the ciphertext and tag, and of the associated data, must fail.
        std::size_t rejected = 0, tried = 0;
        for (std::size_t bit = 0; bit < wantAll.size() * 8; ++bit) {
            Bytes bad = wantAll; bad[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            Bytes o(pt.size(), 0xaa);
            ++tried;
            if (!ChaCha20Poly1305Open(o.data(), bad.data(), bad.size(), aad.data(), aad.size(), nonce.data(), key.data())) ++rejected;
            bool allZero = true; for (auto b : o) if (b) allZero = false;
            CHECK(allZero);   // nothing but zeros handed back for a bad tag
        }
        CHECK(rejected == tried);
        for (std::size_t bit = 0; bit < aad.size() * 8; ++bit) {
            Bytes a = aad; a[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            Bytes o(pt.size());
            CHECK(!ChaCha20Poly1305Open(o.data(), wantAll.data(), wantAll.size(), a.data(), a.size(), nonce.data(), key.data()));
        }
        for (std::size_t bit = 0; bit < 96; ++bit) {          // nonce
            Bytes nn = nonce; nn[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            Bytes o(pt.size());
            CHECK(!ChaCha20Poly1305Open(o.data(), wantAll.data(), wantAll.size(), aad.data(), aad.size(), nn.data(), key.data()));
        }
        for (std::size_t bit = 0; bit < 256; ++bit) {         // key
            Bytes kk = key; kk[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            Bytes o(pt.size());
            CHECK(!ChaCha20Poly1305Open(o.data(), wantAll.data(), wantAll.size(), aad.data(), aad.size(), nonce.data(), kk.data()));
        }
        // Truncated input (shorter than a tag) is refused.
        Bytes o(1);
        CHECK(!ChaCha20Poly1305Open(o.data(), wantAll.data(), 15, aad.data(), aad.size(), nonce.data(), key.data()));
    }
    // Empty plaintext and empty AAD.
    {
        Bytes key = RndBytes(32), nonce = RndBytes(12), sealed(16), o(1);
        ChaCha20Poly1305Seal(sealed.data(), nullptr, 0, nullptr, 0, nonce.data(), key.data());
        CHECK(ChaCha20Poly1305Open(o.data(), sealed.data(), 16, nullptr, 0, nonce.data(), key.data()));
        for (std::size_t bit = 0; bit < 128; ++bit) {
            Bytes bad = sealed; bad[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            CHECK(!ChaCha20Poly1305Open(o.data(), bad.data(), 16, nullptr, 0, nonce.data(), key.data()));
        }
    }

    // ---- XChaCha20-Poly1305 (draft A.3.1) ------------------------------------------------------
    {
        Bytes key = V("xaead_key"), nonce = V("xaead_nonce"), aad = V("xaead_aad"), pt = V("xaead_pt"), ct = V("xaead_ct"), tag = V("xaead_tag");
        Bytes want = ct; want.insert(want.end(), tag.begin(), tag.end());
        Bytes sealed(pt.size() + 16);
        XChaCha20Poly1305Seal(sealed.data(), pt.data(), pt.size(), aad.data(), aad.size(), nonce.data(), key.data());
        CHECK(sealed == want);
        Bytes o(pt.size());
        CHECK(XChaCha20Poly1305Open(o.data(), want.data(), want.size(), aad.data(), aad.size(), nonce.data(), key.data()));
        CHECK(o == pt);
        for (std::size_t bit = 0; bit < want.size() * 8; ++bit) {
            Bytes bad = want; bad[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            CHECK(!XChaCha20Poly1305Open(o.data(), bad.data(), bad.size(), aad.data(), aad.size(), nonce.data(), key.data()));
        }
        for (std::size_t bit = 0; bit < 192; ++bit) {
            Bytes nn = nonce; nn[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            CHECK(!XChaCha20Poly1305Open(o.data(), want.data(), want.size(), aad.data(), aad.size(), nn.data(), key.data()));
        }
        // In-place seal/open.
        Bytes buf = pt; buf.resize(pt.size() + 16);
        XChaCha20Poly1305Seal(buf.data(), buf.data(), pt.size(), aad.data(), aad.size(), nonce.data(), key.data());
        CHECK(buf == want);
    }

    // ---- BLAKE2b (RFC 7693) -----------------------------------------------------------------------
    {
        Bytes out(64);
        Blake2bHash(out.data(), 64, reinterpret_cast<const std::uint8_t*>("abc"), 3);
        CHECK(out == V("b2b_abc"));
        // Appendix E self-test: hash of hashes over unkeyed and keyed inputs.
        auto seq = [](std::uint8_t* o, std::size_t len, std::uint32_t seed) {
            std::uint32_t a = 0xDEAD4BADu * seed, b = 1;
            for (std::size_t i = 0; i < len; ++i) { std::uint32_t t = a + b; a = b; b = t; o[i] = static_cast<std::uint8_t>((t >> 24) & 0xff); }
        };
        const std::size_t mdLen[4] = {20, 32, 48, 64}, inLen[6] = {0, 3, 128, 129, 255, 1024};
        Blake2b grand(32);
        std::uint8_t in[1024], md[64], key[64];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 6; ++j) {
                seq(in, inLen[j], static_cast<std::uint32_t>(inLen[j]));
                Blake2bHash(md, mdLen[i], in, inLen[j]);
                grand.Update(md, mdLen[i]);
                seq(key, mdLen[i], static_cast<std::uint32_t>(mdLen[i]));
                Blake2bHash(md, mdLen[i], in, inLen[j], key, mdLen[i]);
                grand.Update(md, mdLen[i]);
            }
        Bytes g(32);
        grand.Final(g.data());
        CHECK(g == V("b2b_grand"));
        // Streaming at every split point equals one-shot (also across the 128-byte block edge).
        Bytes msg = RndBytes(300), ref(64), got(64);
        Blake2bHash(ref.data(), 64, msg.data(), msg.size());
        for (std::size_t cut = 0; cut <= msg.size(); ++cut) {
            Blake2b b(64);
            b.Update(msg.data(), cut); b.Update(msg.data() + cut, msg.size() - cut);
            b.Final(got.data());
            CHECK(got == ref);
        }
    }

    // ---- SHA-256 / SHA-512 --------------------------------------------------------------------------
    for (const char* n : {"abc", "empty", "two", "long"}) {
        Bytes m = V((std::string("sha_") + n + "_msg").c_str()), d256(32), d512(64);
        Sha256Hash(d256.data(), m.data(), m.size());
        Sha512Hash(d512.data(), m.data(), m.size());
        CHECK(d256 == V((std::string("sha256_") + n).c_str()));
        CHECK(d512 == V((std::string("sha512_") + n).c_str()));
    }
    {
        Bytes a(1000000, 'a'), d256(32), d512(64);
        Sha256Hash(d256.data(), a.data(), a.size());
        Sha512Hash(d512.data(), a.data(), a.size());
        CHECK(d256 == V("sha256_million_a"));
        CHECK(d512 == V("sha512_million_a"));
        Bytes m = RndBytes(500), r256(32), r512(64), g(64);
        Sha256Hash(r256.data(), m.data(), m.size()); Sha512Hash(r512.data(), m.data(), m.size());
        for (std::size_t cut = 0; cut <= m.size(); cut += 7) {
            Sha256 s; s.Update(m.data(), cut); s.Update(m.data() + cut, m.size() - cut); s.Final(g.data());
            CHECK(Bytes(g.begin(), g.begin() + 32) == r256);
            Sha512 t; t.Update(m.data(), cut); t.Update(m.data() + cut, m.size() - cut); t.Final(g.data());
            CHECK(g == r512);
        }
    }

    // ---- X25519 (RFC 7748 5.2, 6.1) ---------------------------------------------------------------------
    for (int k = 0; k < 2; ++k) {
        Bytes s = VN("x25519_", k, "_k"), u = VN("x25519_", k, "_u"), out(32);
        CHECK(X25519(out.data(), s.data(), u.data()));
        CHECK(out == VN("x25519_", k, "_out"));
    }
    {
        Bytes a = V("dh_a"), b = V("dh_b"), pa(32), pb(32), k1(32), k2(32);
        X25519Base(pa.data(), a.data()); X25519Base(pb.data(), b.data());
        CHECK(pa == V("dh_apub")); CHECK(pb == V("dh_bpub"));
        CHECK(X25519(k1.data(), a.data(), pb.data())); CHECK(X25519(k2.data(), b.data(), pa.data()));
        CHECK(k1 == V("dh_k")); CHECK(k2 == V("dh_k"));
    }
    {
        // Iterated: k = X25519(k, u), u = old k, from k = u = 9.
        Bytes k(32, 0), u(32, 0), o(32);
        k[0] = 9; u[0] = 9;
        const int iters = std::getenv("OKAY_CRYPTO_SLOW") ? 1000000 : 1000;
        for (int i = 1; i <= iters; ++i) {
            X25519(o.data(), k.data(), u.data());
            u = k; k = o;
            if (i == 1) CHECK(k == V("x25519_iter1"));
            if (i == 1000) CHECK(k == V("x25519_iter1000"));
        }
        if (iters == 1000000) CHECK(k == V("x25519_iter1000000"));
    }
    {
        // Small-order peer points give an all-zero result, which is reported as failure.
        Bytes s = RndBytes(32), out(32);
        Bytes zero(32, 0), one(32, 0); one[0] = 1;
        CHECK(!X25519(out.data(), s.data(), zero.data()));
        CHECK(!X25519(out.data(), s.data(), one.data()));
        Bytes order8 = Hex("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800");
        CHECK(!X25519(out.data(), s.data(), order8.data()));
        Bytes order8b = Hex("5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157");
        CHECK(!X25519(out.data(), s.data(), order8b.data()));
        // The top bit of the u-coordinate is ignored (RFC 7748).
        Bytes u = V("x25519_0_u"), u2 = u; u2[31] ^= 0x80;
        Bytes a(32), b(32);
        X25519(a.data(), V("x25519_0_k").data(), u.data()); X25519(b.data(), V("x25519_0_k").data(), u2.data());
        CHECK(a == b);
    }

    // ---- Kx session keys -------------------------------------------------------------------------------------
    {
        std::uint8_t cpk[32], csk[32], spk[32], ssk[32], crx[32], ctx[32], srx[32], stx[32];
        KxKeypair(cpk, csk); KxKeypair(spk, ssk);
        CHECK(KxClientSessionKeys(crx, ctx, cpk, csk, spk));
        CHECK(KxServerSessionKeys(srx, stx, spk, ssk, cpk));
        CHECK(std::memcmp(crx, stx, 32) == 0);           // what the client receives, the server sends
        CHECK(std::memcmp(ctx, srx, 32) == 0);
        CHECK(std::memcmp(crx, ctx, 32) != 0);           // one key per direction
        std::uint8_t zeroPk[32] = {0}, a[32], b[32];
        CHECK(!KxClientSessionKeys(a, b, cpk, csk, zeroPk));
        bool z = true; for (int i = 0; i < 32; ++i) if (a[i] || b[i]) z = false;
        CHECK(z);
    }

    // ---- Ed25519 (RFC 8032 7.1) ----------------------------------------------------------------------------------
    for (const char* n : {"1", "2", "3", "1024", "sha"}) {
        const std::string p = std::string("ed_") + n;
        Bytes seed = V((p + "_sk").c_str()), pk = V((p + "_pk").c_str()), msg = V((p + "_msg").c_str()), sig = V((p + "_sig").c_str());
        Bytes gotPk(32), sk(64), gotSig(64);
        Ed25519SeedKeypair(gotPk.data(), sk.data(), seed.data());
        CHECK(gotPk == pk);
        CHECK(Bytes(sk.begin(), sk.begin() + 32) == seed);
        CHECK(Bytes(sk.begin() + 32, sk.end()) == pk);
        Ed25519Sign(gotSig.data(), msg.data(), msg.size(), sk.data());
        CHECK(gotSig == sig);
        CHECK(Ed25519Verify(sig.data(), msg.data(), msg.size(), pk.data()));
        // Every flipped bit of the signature, the message and the public key fails.
        // (Verify costs milliseconds, so the longer vectors are sampled; vectors 1 and 2 flip every bit.)
        const std::size_t step = (std::strcmp(n, "1") == 0 || std::strcmp(n, "2") == 0) ? 1 : 7;
        for (std::size_t bit = 0; bit < 512; bit += step) {
            Bytes bad = sig; bad[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            CHECK(!Ed25519Verify(bad.data(), msg.data(), msg.size(), pk.data()));
        }
        for (std::size_t bit = 0; bit < msg.size() * 8; bit += msg.size() * 8 / 64 + 1) {
            Bytes m = msg; m[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            CHECK(!Ed25519Verify(sig.data(), m.data(), m.size(), pk.data()));
        }
        for (std::size_t bit = 0; bit < 256; bit += step * 3) {
            Bytes bad = pk; bad[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            CHECK(!Ed25519Verify(sig.data(), msg.data(), msg.size(), bad.data()));
        }
    }
    {
        // Strictness: the malleable S + L form, small-order R, and weak public keys are refused.
        Bytes seed = V("ed_2_sk"), pk = V("ed_2_pk"), msg = V("ed_2_msg"), sig = V("ed_2_sig");
        const std::uint8_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
        Bytes mal = sig;
        unsigned carry = 0;
        for (int i = 0; i < 32; ++i) { unsigned v = mal[32 + i] + kL[i] + carry; mal[32 + i] = static_cast<std::uint8_t>(v); carry = v >> 8; }
        CHECK(carry == 0);   // S + L still fits in 256 bits for this vector, so the test is meaningful
        CHECK(!Ed25519Verify(mal.data(), msg.data(), msg.size(), pk.data()));
        // The all-identity forgery: R = identity, S = 0, A = identity "verifies" for any message in naive code.
        Bytes ident(32, 0); ident[0] = 1;
        Bytes forged(64, 0); forged[0] = 1;
        CHECK(!Ed25519Verify(forged.data(), msg.data(), msg.size(), ident.data()));
        CHECK(!Ed25519Verify(sig.data(), msg.data(), msg.size(), ident.data()));
        // Non-canonical public key: y = p (encodes the same as y = 0).
        Bytes nonCanon = Hex("edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f");
        CHECK(!Ed25519Verify(sig.data(), msg.data(), msg.size(), nonCanon.data()));
        // Public key that is not a curve point.
        Bytes notPoint = Hex("0200000000000000000000000000000000000000000000000000000000000000");
        bool anyAccept = false;
        for (int y = 2; y < 40; ++y) { notPoint[0] = static_cast<std::uint8_t>(y); if (Ed25519Verify(sig.data(), msg.data(), msg.size(), notPoint.data())) anyAccept = true; }
        CHECK(!anyAccept);
        // Sign/verify round trip with random keys, lengths around the hash block size.
        for (std::size_t n : {0u, 1u, 55u, 56u, 111u, 112u, 127u, 128u, 129u, 300u}) {
            Bytes pk2(32), sk2(64), m = RndBytes(n), s(64);
            Ed25519Keypair(pk2.data(), sk2.data());
            Ed25519Sign(s.data(), m.data(), m.size(), sk2.data());
            CHECK(Ed25519Verify(s.data(), m.data(), m.size(), pk2.data()));
            Bytes pk3(32);
            Ed25519PublicFromSeed(pk3.data(), sk2.data());
            CHECK(pk3 == pk2);
        }
    }

    // ---- Utilities -----------------------------------------------------------------------------------------------
    {
        Bytes a = RndBytes(64), b = a;
        CHECK(ConstantTimeEqual(a.data(), b.data(), 64));
        CHECK(ConstantTimeEqual(a.data(), b.data(), 0));
        for (std::size_t bit = 0; bit < 512; ++bit) {
            b = a; b[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            CHECK(!ConstantTimeEqual(a.data(), b.data(), 64));
        }
        MemZero(a.data(), a.size());
        bool z = true; for (auto x : a) if (x) z = false;
        CHECK(z);
        Bytes r1(64, 0), r2(64, 0);
        RandomBytes(r1.data(), 64); RandomBytes(r2.data(), 64);
        CHECK(r1 != r2);                                  // 2^-512 false-failure odds
        CHECK(r1 != Bytes(64, 0));
        Bytes big(5000, 0);
        RandomBytes(big.data(), big.size());              // larger than any single OS call's limit
        std::size_t nz = 0; for (auto x : big) if (x) ++nz;
        CHECK(nz > 4500);
        RandomBytes(nullptr, 0);
    }

    RUN_SUITE("crypto Ed25519IsWeakKey");
    {   // the key checks Ed25519Verify makes, exposed for vetting key lists (shared/AntiCheatCrypto.hpp)
        const char* weak[] = {
            "0000000000000000000000000000000000000000000000000000000000000000",   // order 4
            "0100000000000000000000000000000000000000000000000000000000000000",   // identity
            "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",   // order 8
            "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",   // order 8
            "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",   // p-1 (order 2)
            "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",   // p (non-canonical 0)
            "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",   // p+1 (non-canonical 1)
        };
        for (const char* h : weak)
            for (int sign = 0; sign < 2; ++sign) {   // with the sign bit too (0100..80 and ecff..ff are not even decodable)
                Bytes k = Hex(h);
                if (sign) k[31] |= 0x80;
                CHECK(Ed25519IsWeakKey(k.data()));
                Bytes sig(64, 0); sig[0] = 1;
                CHECK(!Ed25519Verify(sig.data(), nullptr, 0, k.data()));
            }
        for (int i = 0; i < 20; ++i) {
            const Bytes seed = RndBytes(32);
            std::uint8_t pk[32];
            Ed25519PublicFromSeed(pk, seed.data());
            CHECK(!Ed25519IsWeakKey(pk));
        }
    }

    TEST_MAIN_RESULT();
}
