// Constant-time audit, method 1 of 2: secret-dependent branches and memory addresses, found with
// valgrind (the "ctgrind" technique). Every secret is marked as undefined memory; valgrind's memcheck
// then reports "conditional jump or move depends on uninitialised value" for a branch on a secret and
// "use of uninitialised value" for a table index / address built from one. Run under valgrind:
//     valgrind -q ./crypto_ct_valgrind        (exit status 1 if any case is not as expected; the two positive
// controls print their expected valgrind reports, which is normal)
// Method 2 (a statistical timing test of the real timings) is tests/crypto_timing.cpp.
// The library must be built with -DOKAY_CRYPTO_CTGRIND (this target does that) so the few values that
// are public BY DESIGN (accept/reject of a tag comparison, "is this X25519 result all zero") are marked
// public with OKAY_CT_PUBLIC; anything else reported is a finding.
//
// Positive controls: functions with a deliberate secret-dependent branch and a deliberate secret table
// index are run the same way and MUST be reported, otherwise the harness is not looking.
// What this does NOT show: valgrind tracks definedness bit-precisely only for some operations, and says
// nothing about variable-time instructions (a division, a multiply that is slow on some CPUs) or about
// microarchitectural effects; see the timing test and docs/crypto_review_package.md.
// UNREVIEWED code under test (see okay/Crypto/Crypto.hpp).
#include <okay/Crypto/Crypto.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
#include <valgrind/memcheck.h>

using namespace okay::crypto;
using Bytes = std::vector<std::uint8_t>;

static void Secret(void* p, std::size_t n) { VALGRIND_MAKE_MEM_UNDEFINED(p, n); }
static void Public(void* p, std::size_t n) { VALGRIND_MAKE_MEM_DEFINED(p, n); }
template <class T> static void Secret(T& v) { Secret(&v[0], v.size()); }

static std::uint64_t g_s = 0x13198a2e03707344ull;
static std::uint8_t Rb() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return static_cast<std::uint8_t>(g_s >> 20); }
static Bytes Rnd(std::size_t n) { Bytes b(n); for (auto& x : b) x = Rb(); return b; }

static int g_fail = 0;
static bool g_under = false;

// Runs `body` and compares the valgrind error count before and after.
template <class F> static void Case(const char* name, bool expectLeak, F body) {
    const unsigned long before = VALGRIND_COUNT_ERRORS;
    body();
    const unsigned long found = VALGRIND_COUNT_ERRORS - before;
    const bool ok = expectLeak ? found > 0 : found == 0;
    std::printf("  %-46s %s%s\n", name, ok ? "ok" : "FAIL", expectLeak ? "  (positive control: leak expected and found)" : "");
    if (!ok) { ++g_fail; std::printf("    -> %lu valgrind error(s)%s\n", found, expectLeak ? " expected, 0 found: the harness is blind" : ""); }
}

// ---- positive controls: deliberately leaky code --------------------------------------------------
__attribute__((noinline)) static int LeakyBranch(const std::uint8_t* secret) {
    // A real branch on a secret byte; the asm statements in the arms keep the compiler from turning it into a select.
    if (secret[0] > 100) { __asm__ __volatile__("nop"); return 1; }
    __asm__ __volatile__("nop; nop");
    return 2;
}
__attribute__((noinline)) static std::uint8_t LeakyIndex(const std::uint8_t* secret, const std::uint8_t* table) {
    return table[secret[0]];                                 // memory address from a secret
}

int main() {
    g_under = RUNNING_ON_VALGRIND != 0;
    if (!g_under) { std::printf("crypto_ct_valgrind: not running under valgrind; nothing checked (run: valgrind -q %s)\n", "crypto_ct_valgrind"); return 0; }
    std::printf("[ctgrind] secrets are marked undefined; any branch or address that depends on them is an error\n");
    // Warm up everything that lazily builds public tables (so those are not under test).
    {
        std::uint8_t s[32] = {1}, pk[32], sk[64], sig[64];
        Ed25519SeedKeypair(pk, sk, s); Ed25519Sign(sig, s, 32, sk); (void)Ed25519Verify(sig, s, 32, pk);
    }

    // Controls first.
    Case("control: branch on a secret bit", true, [] {
        Bytes s = Rnd(16); Secret(s);
        volatile int r = LeakyBranch(s.data()); (void)r;
    });
    Case("control: table index from a secret", true, [] {
        Bytes t = Rnd(256), s = Rnd(1); Secret(s);
        volatile std::uint8_t r = LeakyIndex(s.data(), t.data()); (void)r;
    });

    Case("ConstantTimeEqual (both secret)", false, [] {
        Bytes a = Rnd(32), b = Rnd(32); Secret(a); Secret(b);
        bool eq = ConstantTimeEqual(a.data(), b.data(), 32);   // result is public by design
        volatile bool v = eq; (void)v;
    });
    Case("X25519 (secret scalar, public point)", false, [] {
        Bytes k = Rnd(32), p = Rnd(32), o(32); p[31] &= 0x7f; Secret(k);
        bool ok = X25519(o.data(), k.data(), p.data()); volatile bool v = ok; (void)v;
        Public(o.data(), 32);
    });
    Case("X25519Base (secret scalar)", false, [] {
        Bytes k = Rnd(32), o(32); Secret(k);
        X25519Base(o.data(), k.data());
        Public(o.data(), 32);
    });
    Case("Kx client + server session keys", false, [] {
        Bytes csk = Rnd(32), ssk = Rnd(32), cpk(32), spk(32), rx(32), tx(32);
        X25519Base(cpk.data(), csk.data()); X25519Base(spk.data(), ssk.data());
        Secret(csk); Secret(ssk);
        bool a = KxClientSessionKeys(rx.data(), tx.data(), cpk.data(), csk.data(), spk.data());
        bool b = KxServerSessionKeys(rx.data(), tx.data(), spk.data(), ssk.data(), cpk.data());
        volatile bool v = a && b; (void)v;
    });
    Case("Ed25519PublicFromSeed (secret seed)", false, [] {
        Bytes seed = Rnd(32), pk(32); Secret(seed);
        Ed25519PublicFromSeed(pk.data(), seed.data());
        Public(pk.data(), 32);
    });
    Case("Ed25519Sign (secret key, public message)", false, [] {
        Bytes seed = Rnd(32), pk(32), sk(64), sig(64), m = Rnd(100);
        Ed25519SeedKeypair(pk.data(), sk.data(), seed.data());
        Secret(sk);
        Ed25519Sign(sig.data(), m.data(), m.size(), sk.data());
        Public(sig.data(), 64);
    });
    Case("Ed25519Sign (secret key, secret message)", false, [] {
        Bytes seed = Rnd(32), pk(32), sk(64), sig(64), m = Rnd(200);
        Ed25519SeedKeypair(pk.data(), sk.data(), seed.data());
        Secret(sk); Secret(m);
        Ed25519Sign(sig.data(), m.data(), m.size(), sk.data());
        Public(sig.data(), 64);
    });
    Case("ChaCha20Xor (secret key and data)", false, [] {
        Bytes k = Rnd(32), n = Rnd(12), d = Rnd(200), o(200); Secret(k); Secret(d);
        ChaCha20Xor(o.data(), d.data(), d.size(), k.data(), n.data(), 1);
        Public(o.data(), o.size());
    });
    Case("HChaCha20 (secret key)", false, [] {
        Bytes k = Rnd(32), in = Rnd(16), o(32); Secret(k);
        HChaCha20(o.data(), k.data(), in.data());
        Public(o.data(), 32);
    });
    Case("Poly1305 (secret key and message)", false, [] {
        Bytes k = Rnd(32), m = Rnd(77), t(16); Secret(k); Secret(m);
        Poly1305Mac(t.data(), m.data(), m.size(), k.data());
        Public(t.data(), 16);
    });
    Case("ChaCha20-Poly1305 seal (secret key, data, aad)", false, [] {
        Bytes k = Rnd(32), n = Rnd(12), d = Rnd(100), aad = Rnd(20), o(116); Secret(k); Secret(d); Secret(aad);
        ChaCha20Poly1305Seal(o.data(), d.data(), d.size(), aad.data(), aad.size(), n.data(), k.data());
        Public(o.data(), o.size());
    });
    Case("XChaCha20-Poly1305 seal (secret key and data)", false, [] {
        Bytes k = Rnd(32), n = Rnd(24), d = Rnd(100), o(116); Secret(k); Secret(d);
        XChaCha20Poly1305Seal(o.data(), d.data(), d.size(), nullptr, 0, n.data(), k.data());
        Public(o.data(), o.size());
    });
    Case("XChaCha20-Poly1305 open, valid (secret key)", false, [] {
        Bytes k = Rnd(32), n = Rnd(24), d = Rnd(100), c(116), o(100);
        XChaCha20Poly1305Seal(c.data(), d.data(), d.size(), nullptr, 0, n.data(), k.data());
        Secret(k);
        bool ok = XChaCha20Poly1305Open(o.data(), c.data(), c.size(), nullptr, 0, n.data(), k.data());
        volatile bool v = ok; (void)v;
    });
    Case("XChaCha20-Poly1305 open, forged tag (secret key)", false, [] {
        Bytes k = Rnd(32), n = Rnd(24), c = Rnd(116), o(100); Secret(k);
        bool ok = XChaCha20Poly1305Open(o.data(), c.data(), c.size(), nullptr, 0, n.data(), k.data());
        volatile bool v = ok; (void)v;
    });
    Case("BLAKE2b keyed (secret key and message)", false, [] {
        Bytes k = Rnd(32), m = Rnd(300), o(64); Secret(k); Secret(m);
        Blake2bHash(o.data(), 64, m.data(), m.size(), k.data(), k.size());
        Public(o.data(), 64);
    });
    Case("SHA-256 / SHA-512 (secret message)", false, [] {
        Bytes m = Rnd(300), o(64); Secret(m);
        Sha256Hash(o.data(), m.data(), m.size());
        Sha512Hash(o.data() , m.data(), m.size());
        Public(o.data(), 64);
    });

    std::printf(g_fail ? "FAIL: %d case(s)\n" : "all cases as expected\n", g_fail);
    return g_fail ? 1 : 0;
}
