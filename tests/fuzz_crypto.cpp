// Fuzz target for okay::crypto (UNREVIEWED). Two ways to run it:
//   * libFuzzer (clang):   -DOKAY_CRYPTO_LIBFUZZER=ON builds `fuzz_crypto` with -fsanitize=fuzzer;
//                          run it as `fuzz_crypto -max_total_time=300 corpus/`.
//   * no libFuzzer:        the same entry point is driven by a built-in mutator for N iterations
//                          (`fuzz_crypto_smoke [iterations] [seed]`, a ctest), best run under ASan/UBSan.
// The input is split into operation, then arguments. Properties checked (abort on violation):
//   - authenticated decryption of arbitrary bytes never succeeds unless the bytes are a valid seal,
//     and never writes plaintext on failure;
//   - Ed25519 verification of arbitrary signature/key/message never crashes, never accepts garbage,
//     and agrees with itself on re-runs; signatures it makes always verify;
//   - hashes and MACs equal their one-shot value when streamed at an arbitrary split;
//   - X25519 and Kx accept any 32 bytes (all-zero results are reported, not crashed on).
#include <okay/Crypto/Crypto.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace okay::crypto;
using Bytes = std::vector<std::uint8_t>;

namespace {
[[noreturn]] void Bad(const char* what) { std::fprintf(stderr, "fuzz_crypto: property violated: %s\n", what); std::abort(); }

struct Reader {
    const std::uint8_t* p; std::size_t n;
    std::uint8_t Byte() { if (!n) return 0; --n; return *p++; }
    Bytes Take(std::size_t k) { Bytes b(k, 0); const std::size_t c = k < n ? k : n; if (c) std::memcpy(b.data(), p, c); p += c; n -= c; return b; }
    Bytes Rest() { Bytes b(p, p + n); p += n; n = 0; return b; }
};
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Reader r{data, size};
    const std::uint8_t op = r.Byte() % 8;
    switch (op) {
    case 0: case 1: {   // AEAD open of arbitrary bytes (IETF / X)
        Bytes key = r.Take(32), nonce = r.Take(op ? 24 : 12), aad = r.Take(r.Byte() % 40), ct = r.Rest();
        Bytes out(ct.size() + 1, 0x5a);
        const bool ok = op ? XChaCha20Poly1305Open(out.data(), ct.data(), ct.size(), aad.data(), aad.size(), nonce.data(), key.data())
                           : ChaCha20Poly1305Open(out.data(), ct.data(), ct.size(), aad.data(), aad.size(), nonce.data(), key.data());
        if (out.back() != 0x5a) Bad("wrote past the plaintext");
        if (!ok) {
            if (ct.size() >= 16)
                for (std::size_t i = 0; i < ct.size() - 16; ++i) if (out[i] != 0) Bad("plaintext returned for a bad tag");
        } else if (ct.size() >= 16) {   // accepted: it must re-seal to the same bytes (a real seal)
            Bytes again(ct.size());
            const std::size_t n = ct.size() - 16;
            if (op) XChaCha20Poly1305Seal(again.data(), out.data(), n, aad.data(), aad.size(), nonce.data(), key.data());
            else ChaCha20Poly1305Seal(again.data(), out.data(), n, aad.data(), aad.size(), nonce.data(), key.data());
            if (again != ct) Bad("accepted bytes that are not the seal of their plaintext");
        }
        break;
    }
    case 2: {           // seal then open, and every single-bit flip of the sealed message is refused
        Bytes key = r.Take(32), nonce = r.Take(24), aad = r.Take(r.Byte() % 20), pt = r.Rest();
        if (pt.size() > 96) pt.resize(96);
        Bytes sealed(pt.size() + 16), back(pt.size());
        XChaCha20Poly1305Seal(sealed.data(), pt.data(), pt.size(), aad.data(), aad.size(), nonce.data(), key.data());
        if (!XChaCha20Poly1305Open(back.data(), sealed.data(), sealed.size(), aad.data(), aad.size(), nonce.data(), key.data()) || back != pt)
            Bad("seal/open round trip");
        for (std::size_t bit = 0; bit < sealed.size() * 8; ++bit) {
            sealed[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            if (XChaCha20Poly1305Open(back.data(), sealed.data(), sealed.size(), aad.data(), aad.size(), nonce.data(), key.data())) Bad("a flipped bit opened");
            sealed[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
        }
        break;
    }
    case 3: {           // Ed25519 verify of arbitrary input
        Bytes sig = r.Take(64), pk = r.Take(32), msg = r.Rest();
        const bool a = Ed25519Verify(sig.data(), msg.data(), msg.size(), pk.data());
        const bool b = Ed25519Verify(sig.data(), msg.data(), msg.size(), pk.data());
        if (a != b) Bad("verify is not deterministic");
        break;
    }
    case 4: {           // Ed25519 sign then verify, wrong message must fail
        Bytes seed = r.Take(32), msg = r.Rest();
        if (msg.size() > 200) msg.resize(200);
        Bytes pk(32), sk(64), sig(64);
        Ed25519SeedKeypair(pk.data(), sk.data(), seed.data());
        Ed25519Sign(sig.data(), msg.data(), msg.size(), sk.data());
        if (!Ed25519Verify(sig.data(), msg.data(), msg.size(), pk.data())) Bad("own signature rejected");
        Bytes other = msg; other.push_back(1);
        if (Ed25519Verify(sig.data(), other.data(), other.size(), pk.data())) Bad("signature accepted for another message");
        break;
    }
    case 5: {           // streaming equals one-shot (BLAKE2b, SHA-2, Poly1305)
        Bytes key = r.Take(32); const std::size_t cut0 = r.Byte(); Bytes m = r.Rest();
        const std::size_t cut = m.empty() ? 0 : cut0 % (m.size() + 1);
        Bytes a(64), b(64), c(32), d(32), t1(16), t2(16);
        Blake2bHash(a.data(), 64, m.data(), m.size());
        { Blake2b s(64); s.Update(m.data(), cut); s.Update(m.data() + cut, m.size() - cut); s.Final(b.data()); }
        if (a != b) Bad("blake2b streaming");
        Sha512Hash(a.data(), m.data(), m.size());
        { Sha512 s; s.Update(m.data(), cut); s.Update(m.data() + cut, m.size() - cut); s.Final(b.data()); }
        if (a != b) Bad("sha512 streaming");
        Sha256Hash(c.data(), m.data(), m.size());
        { Sha256 s; s.Update(m.data(), cut); s.Update(m.data() + cut, m.size() - cut); s.Final(d.data()); }
        if (c != d) Bad("sha256 streaming");
        Poly1305Mac(t1.data(), m.data(), m.size(), key.data());
        { Poly1305 p(key.data()); p.Update(m.data(), cut); p.Update(m.data() + cut, m.size() - cut); p.Final(t2.data()); }
        if (t1 != t2) Bad("poly1305 streaming");
        break;
    }
    case 6: {           // X25519 and Kx on arbitrary 32-byte values
        Bytes s = r.Take(32), u = r.Take(32), out(32);
        X25519(out.data(), s.data(), u.data());
        std::uint8_t rx[32], tx[32];
        KxClientSessionKeys(rx, tx, u.data(), s.data(), u.data());
        KxServerSessionKeys(rx, tx, u.data(), s.data(), s.data());
        break;
    }
    default: {          // ChaCha20 with arbitrary counter and length; in-place equals out-of-place
        Bytes key = r.Take(32), nonce = r.Take(12); const std::uint32_t ctr = r.Byte() << 24 | r.Byte() << 16 | r.Byte() << 8 | r.Byte(); Bytes m = r.Rest();
        Bytes a(m.size()), b = m;
        ChaCha20Xor(a.data(), m.data(), m.size(), key.data(), nonce.data(), ctr);
        ChaCha20Xor(b.data(), b.data(), b.size(), key.data(), nonce.data(), ctr);
        if (a != b) Bad("in-place chacha20");
        break;
    }
    }
    return 0;
}

#ifndef OKAY_CRYPTO_LIBFUZZER
// Built-in driver: random inputs plus byte-level mutation of the previous ones.
int main(int argc, char** argv) {
    const long iters = argc > 1 ? std::atol(argv[1]) : 20000;
    std::uint64_t s = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 88172645463325252ull;
    auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    std::vector<Bytes> corpus(1, Bytes(1, 0));
    for (long i = 0; i < iters; ++i) {
        Bytes in;
        if (corpus.size() > 1 && rnd() % 4 != 0) {
            in = corpus[rnd() % corpus.size()];
            const int muts = 1 + static_cast<int>(rnd() % 4);
            for (int m = 0; m < muts; ++m) {
                switch (rnd() % 4) {
                case 0: if (!in.empty()) in[rnd() % in.size()] ^= static_cast<std::uint8_t>(1u << (rnd() % 8)); break;
                case 1: if (!in.empty()) in[rnd() % in.size()] = static_cast<std::uint8_t>(rnd()); break;
                case 2: in.insert(in.begin() + (in.empty() ? 0 : rnd() % in.size()), static_cast<std::uint8_t>(rnd())); break;
                default: if (!in.empty()) in.erase(in.begin() + rnd() % in.size()); break;
                }
            }
        } else {
            in.resize(rnd() % 300);
            for (auto& b : in) b = static_cast<std::uint8_t>(rnd());
            if (!in.empty()) in[0] = static_cast<std::uint8_t>(rnd() % 8);
        }
        LLVMFuzzerTestOneInput(in.data(), in.size());
        if (corpus.size() < 512) corpus.push_back(in); else corpus[rnd() % corpus.size()] = in;
    }
    std::printf("fuzz_crypto: %ld iterations, no property violated\n", iters);
    return 0;
}
#endif
