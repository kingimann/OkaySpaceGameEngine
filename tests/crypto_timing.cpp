// Constant-time audit, method 2 of 2: a statistical timing test in the fixed-versus-random style published by Reparaz,
// Balasch and Verbauwhede ("Dude, is my code constant time?"). For each operation two classes of SECRET input are
// timed in random interleaving (for example a fixed key against random keys); Welch's t-test on the two
// timing distributions, raw, cropped at several percentiles (to ignore interrupts) and as centred
// squares (second order), says whether the timing depends on the class. |t| < 4.5 over hundreds of thousands to millions of
// measurements is "no leak detected", |t| > 10 is "leak"; in between is inconclusive. A test like this can
// only ever FAIL to find a leak; it proves nothing. See docs/crypto_review_package.md section 4.
//
// Positive controls: a comparison that exits at the first difference and a multiply that is skipped for a
// zero operand run through the same code and MUST be flagged, or the harness is not sensitive enough on
// this machine and its results must not be trusted.
// Usage: crypto_timing [measurements-per-case=20000] [case-name-substring]   (each case runs in three modes: fixed pattern, fixed random, A/A control)
//        run it on a quiet machine, pinned to one core, e.g.  taskset -c 2 ./crypto_timing 2000000
// UNREVIEWED code under test (see okay/Crypto/Crypto.hpp).
#include <okay/Crypto/Crypto.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif
#ifdef OKAY_TIMING_SODIUM
#include <sodium.h>
#endif

using namespace okay::crypto;
using Bytes = std::vector<std::uint8_t>;

static std::uint64_t g_s = 0xa4093822299f31d0ull;
static std::uint64_t Rnd64() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }
static std::uint64_t g_cs = 0x2545F4914F6CDD1Dull;   // a separate generator (splitmix64) for the class labels, so a label is never a function of the data
static int NextClass() { g_cs += 0x9e3779b97f4a7c15ull; std::uint64_t z = g_cs; z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; return static_cast<int>((z ^ (z >> 31)) >> 63); }
static Bytes RndBytes(std::size_t n) { Bytes b(n); for (auto& x : b) x = static_cast<std::uint8_t>(Rnd64() >> 24); return b; }

static inline std::uint64_t Now() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_lfence();
    const std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
#else
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

struct Case {
    std::string name;
    int reps;                                   // timed repetitions of run() per measurement (for very short operations)
    bool control;                               // positive control: a leak is EXPECTED
    std::function<void(int cls)> prep;          // build the inputs of the given class (not timed)
    std::function<void()> run;                  // the timed operation
};

struct Stat { double n = 0, mean = 0, m2 = 0; void Add(double x) { n += 1; const double d = x - mean; mean += d / n; m2 += d * (x - mean); } double Var() const { return n > 1 ? m2 / (n - 1) : 0; } };
static double Welch(const Stat& a, const Stat& b) {
    if (a.n < 2 || b.n < 2) return 0;
    const double den = std::sqrt(a.Var() / a.n + b.Var() / b.n);
    return den > 0 ? (a.mean - b.mean) / den : 0;
}

struct Result { double tmax = 0; std::string which; double med0 = 0, med1 = 0; std::size_t n0 = 0, n1 = 0; };

static Result Analyse(const std::vector<double>& t0, const std::vector<double>& t1) {
    Result r;
    r.n0 = t0.size(); r.n1 = t1.size();
    {   std::vector<double> a(t0), b(t1); std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
        r.med0 = a.empty() ? 0 : a[a.size() / 2]; r.med1 = b.empty() ? 0 : b[b.size() / 2]; }
    std::vector<double> all(t0); all.insert(all.end(), t1.begin(), t1.end());
    std::sort(all.begin(), all.end());
    auto upd = [&](double t, const std::string& w) { if (std::fabs(t) > std::fabs(r.tmax)) { r.tmax = t; r.which = w; } };
    const double crops[] = {1.0, 0.99, 0.95, 0.90, 0.75, 0.50};
    for (double q : crops) {
        const double thr = all[std::min<std::size_t>(all.size() - 1, static_cast<std::size_t>(q * double(all.size() - 1)))];
        Stat a, b;
        for (double x : t0) if (x <= thr) a.Add(x);
        for (double x : t1) if (x <= thr) b.Add(x);
        char w[40]; std::snprintf(w, sizeof w, q == 1.0 ? "raw" : "crop %.0f%%", q * 100);
        upd(Welch(a, b), w);
    }
    {   // second order: centred squares of the (50%-cropped) data, per class
        const double thr = all[all.size() / 2];
        Stat a, b, ca, cb;
        for (double x : t0) if (x <= thr) ca.Add(x);
        for (double x : t1) if (x <= thr) cb.Add(x);
        for (double x : t0) if (x <= thr) { const double d = x - ca.mean; a.Add(d * d); }
        for (double x : t1) if (x <= thr) { const double d = x - cb.mean; b.Add(d * d); }
        upd(Welch(a, b), "2nd order");
    }
    return r;
}

static volatile std::uint64_t g_sink = 0;

static int RunCase(const Case& c, std::size_t n) {
    std::vector<double> t[2];
    t[0].reserve(n / 2 + 8); t[1].reserve(n / 2 + 8);
    for (int i = 0; i < 50; ++i) { c.prep(i & 1); c.run(); }          // warm up caches, tables, branch predictors
    for (std::size_t i = 0; i < n; ++i) {
        const int cls = NextClass();
        c.prep(cls);
        const std::uint64_t a = Now();
        for (int r = 0; r < c.reps; ++r) c.run();
        const std::uint64_t b = Now();
        t[cls].push_back(static_cast<double>(b - a) / c.reps);
    }
    const Result r = Analyse(t[0], t[1]);
    const double at = std::fabs(r.tmax);
    const char* verdict = at > 10 ? "LEAK" : at > 4.5 ? "inconclusive" : "no leak detected";
    std::printf("  %-52s |t| = %7.2f (%-9s)  median %8.1f vs %8.1f  n = %zu/%zu  %s\n", c.name.c_str(), at, r.which.c_str(),
                r.med0, r.med1, r.n0, r.n1, verdict);
    if (at > 4.5 && !c.control) {   // show the shape of the two distributions when something is flagged
        for (int k = 0; k < 2; ++k) {
            std::vector<double> v(t[k]); std::sort(v.begin(), v.end());
            std::printf("    class %d quantiles 10/50/90/99/99.9%%: %.1f %.1f %.1f %.1f %.1f\n", k, v[v.size() / 10], v[v.size() / 2], v[v.size() * 9 / 10], v[v.size() * 99 / 100], v[v.size() * 999 / 1000]);
        }
    }
    if (c.control) {
        const bool found = at > 10;
        if (!found) std::printf("    -> positive control NOT flagged: this machine/run is too noisy to trust the other lines\n");
        return found ? 0 : 1;
    }
    return 0;
}

// ---- shared input slots ---------------------------------------------------------------------------
static Bytes g_a, g_b, g_c, g_out;
// Which class-0 pattern the run uses (the same cases are run once per mode):
//   0 "fixed pattern":      class 0 is all zeros or all ones (what many leaks of the "special value" kind look like),
//   1 "fixed random":       class 0 is one fixed pseudo-random value, class 1 is fresh random (the usual fixed-versus-random test),
//   2 "A/A control":        both classes are fresh random: no difference exists, so any flag is a false positive of this machine.
static int g_mode = 1;
static std::uint64_t FixedByte(std::uint64_t site, std::size_t i) {   // a constant pseudo-random byte per (call site, index)
    std::uint64_t z = site * 0x9e3779b97f4a7c15ull + i * 0xbf58476d1ce4e5b9ull + 0x94d049bb133111ebull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; return z ^ (z >> 31);
}
// b = n bytes. Every mode draws the same random numbers and touches the same buffer, so only the VALUES differ.
static void FillClass(Bytes& b, std::size_t n, int cls, std::uint8_t fixed) {
    b.resize(n);
    const std::uint64_t site = reinterpret_cast<std::uintptr_t>(&b) * 31 + n * 7 + fixed;
    // keep = 0xff when the byte is fresh random, 0x00 when it is the class-0 constant: a mask, not a branch, so the
    // work (and what it leaves in the caches and predictors) is the same for both classes
    const volatile int mode = g_mode;
    const std::uint8_t keep = static_cast<std::uint8_t>(0u - static_cast<unsigned>((mode == 2) | (cls != 0)));
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t v = static_cast<std::uint8_t>(Rnd64() >> 24);
        const std::uint8_t c0 = mode == 1 ? static_cast<std::uint8_t>(FixedByte(site, i)) : fixed;
        b[i] = static_cast<std::uint8_t>((v & keep) | (c0 & static_cast<std::uint8_t>(~keep)));
    }
}

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 20000;
    const std::string only = argc > 2 ? argv[2] : "";
    std::printf("crypto_timing: %zu measurements per case\n", n);
    std::printf("  thresholds: |t| < 4.5 no leak detected, > 10 leak. Timer: %s\n",
#if defined(__x86_64__) || defined(__i386__)
                "rdtsc");
#else
                "steady_clock (coarse; prefer an x86 machine)");
#endif
    std::vector<Case> cases;
    auto add = [&](const char* name, int reps, bool control, std::function<void(int)> prep, std::function<void()> run) {
        cases.push_back({name, reps, control, std::move(prep), std::move(run)});
    };

    // ---- positive controls
    add("CONTROL early-exit memcmp, 256 B (diff first vs last)", 8, true,
        [](int cls) { g_a = RndBytes(256); g_b = g_a; g_b[cls ? 255 : 0] ^= 1; },
        [] { const std::uint8_t* x = g_a.data(); const std::uint8_t* y = g_b.data(); std::size_t i = 0;
             while (i < 256 && x[i] == y[i]) { ++i; __asm__ __volatile__("" ::: "memory"); } g_sink = g_sink + i; });
    add("CONTROL Poly1305-like with skipped zero-limb multiply", 8, true,
        [](int cls) { g_a = Bytes(64, cls ? 0xff : 0x00); },
        [] { std::uint64_t acc = 1; for (int i = 0; i < 64; ++i) { if (g_a[i]) { acc *= 0x9e3779b97f4a7c15ull; acc ^= acc >> 7; acc *= 3; } __asm__ __volatile__("" ::: "memory"); } g_sink = g_sink + acc; });

    // ---- the real thing
    // Every prep() does the SAME work and touches the SAME buffers for both classes (FillClass draws the same
    // number of random bytes whichever class it is; only what lands in the buffer differs). Otherwise the harness would
    // measure allocation and cache effects of building the inputs, not the operation.
    add("ConstantTimeEqual 256 B (equal vs differ at random byte)", 16, false,
        [](int cls) { FillClass(g_a, 256, 1, 0); g_b = g_a; const std::size_t pos = Rnd64() % 256; const std::uint8_t x = static_cast<std::uint8_t>(1 + Rnd64() % 255); g_b[pos] ^= static_cast<std::uint8_t>(x & (0u - static_cast<unsigned>(cls))); },
        [] { g_sink = g_sink + ConstantTimeEqual(g_a.data(), g_b.data(), 256); });
    add("ConstantTimeEqual 16 B tag (differ first vs last byte)", 32, false,
        [](int cls) { FillClass(g_a, 16, 1, 0); g_b = g_a; g_b[15 * cls] ^= 0x80; },
        [] { g_sink = g_sink + ConstantTimeEqual(g_a.data(), g_b.data(), 16); });
    add("X25519 (fixed scalar A vs random scalar; fixed point)", 1, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); FillClass(g_b, 32, 0, 0); g_b[0] = 9; g_out.assign(32, 0); },
        [] { g_sink = g_sink + X25519(g_out.data(), g_a.data(), g_b.data()); });
    add("X25519 (fixed scalar B vs random scalar; fixed point)", 1, false,
        [](int cls) { FillClass(g_a, 32, cls, 0xff); FillClass(g_b, 32, 0, 0); g_b[0] = 9; g_out.assign(32, 0); },
        [] { g_sink = g_sink + X25519(g_out.data(), g_a.data(), g_b.data()); });
    add("X25519Base (fixed scalar vs random scalar)", 1, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); g_out.assign(32, 0); },
        [] { X25519Base(g_out.data(), g_a.data()); });
    add("Ed25519PublicFromSeed (fixed seed vs random seed)", 1, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); g_out.assign(32, 0); },
        [] { Ed25519PublicFromSeed(g_out.data(), g_a.data()); });
    add("Ed25519Sign 64 B (fixed key A vs random key)", 1, false,
        [](int cls) { FillClass(g_c, 32, cls, 0); Bytes pk(32); g_a.assign(64, 0); Ed25519SeedKeypair(pk.data(), g_a.data(), g_c.data());
                      FillClass(g_b, 64, 0, 0x42); g_out.assign(64, 0); },
        [] { Ed25519Sign(g_out.data(), g_b.data(), 64, g_a.data()); });
    add("Ed25519Sign 64 B (fixed key B vs random key)", 1, false,
        [](int cls) { FillClass(g_c, 32, cls, 0xff); Bytes pk(32); g_a.assign(64, 0); Ed25519SeedKeypair(pk.data(), g_a.data(), g_c.data());
                      FillClass(g_b, 64, 0, 0x42); g_out.assign(64, 0); },
        [] { Ed25519Sign(g_out.data(), g_b.data(), 64, g_a.data()); });
    add("XChaCha20-Poly1305 seal 256 B (fixed key+data vs random)", 4, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); FillClass(g_b, 256, cls, 0); FillClass(g_c, 24, 0, 7); g_out.assign(272, 0); },
        [] { XChaCha20Poly1305Seal(g_out.data(), g_b.data(), 256, nullptr, 0, g_c.data(), g_a.data()); });
    add("XChaCha20-Poly1305 open, forged tag (byte 0 vs byte 15)", 4, false,
        [](int cls) { FillClass(g_a, 32, 1, 0); FillClass(g_c, 24, 0, 7); FillClass(g_b, 272, 1, 0); g_b[256 + 15 * cls] ^= 0x01; g_out.assign(256, 0); },
        [] { g_sink = g_sink + XChaCha20Poly1305Open(g_out.data(), g_b.data(), 272, nullptr, 0, g_c.data(), g_a.data()); });
    add("ChaCha20Xor 256 B (fixed key+data vs random)", 4, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); FillClass(g_b, 256, cls, 0); FillClass(g_c, 12, 0, 1); g_out.assign(256, 0); },
        [] { ChaCha20Xor(g_out.data(), g_b.data(), 256, g_a.data(), g_c.data(), 1); });
    add("Poly1305 256 B (fixed key+msg B vs random)", 4, false,
        [](int cls) { FillClass(g_a, 32, cls, 0xff); FillClass(g_b, 256, cls, 0xff); g_out.assign(16, 0); },
        [] { Poly1305Mac(g_out.data(), g_b.data(), 256, g_a.data()); });
    add("Poly1305 256 B (fixed key+msg A vs random)", 4, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); FillClass(g_b, 256, cls, 0); g_out.assign(16, 0); },
        [] { Poly1305Mac(g_out.data(), g_b.data(), 256, g_a.data()); });
    add("BLAKE2b keyed 256 B (fixed key+msg vs random)", 4, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); FillClass(g_b, 256, cls, 0); g_out.assign(64, 0); },
        [] { Blake2bHash(g_out.data(), 64, g_b.data(), 256, g_a.data(), 32); });
    add("SHA-512 256 B (fixed vs random message)", 4, false,
        [](int cls) { FillClass(g_b, 256, cls, 0); g_out.assign(64, 0); },
        [] { Sha512Hash(g_out.data(), g_b.data(), 256); });
#ifdef OKAY_TIMING_SODIUM
    // Reference rows: libsodium's own SHA-512 and BLAKE2b under the identical harness. A shift that shows up here too is a
    // property of this CPU or of the measurement, not of our code.
    add("REFERENCE libsodium SHA-512 256 B (fixed vs random)", 4, false,
        [](int cls) { FillClass(g_b, 256, cls, 0); g_out.assign(64, 0); },
        [] { crypto_hash_sha512(g_out.data(), g_b.data(), 256); });
    add("REFERENCE libsodium X25519 (fixed vs random scalar)", 1, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); FillClass(g_b, 32, 0, 0); g_b[0] = 9; g_out.assign(32, 0); },
        [] { g_sink = g_sink + crypto_scalarmult(g_out.data(), g_a.data(), g_b.data()); });
#endif
    add("Kx client session keys (fixed vs random secret key)", 1, false,
        [](int cls) { FillClass(g_a, 32, cls, 0); FillClass(g_b, 32, 0, 0); g_b[0] = 9; g_out.assign(64, 0); FillClass(g_c, 32, 0, 3); },
        [] { g_sink = g_sink + KxClientSessionKeys(g_out.data(), g_out.data() + 32, g_c.data(), g_a.data(), g_b.data()); });

    int bad = 0;
    const char* modeName[] = {"fixed pattern (zeros / all-ones) vs random", "fixed random value vs random", "A/A control (both random)"};
    for (int mode : {1, 0, 2}) {
        g_mode = mode;
        std::printf("\n== class 0 = %s ==\n", modeName[mode]);
        for (const Case& c : cases) {
            if (!only.empty() && c.name.find(only) == std::string::npos) continue;
            if (c.control && mode != 1) continue;                   // the controls do not use FillClass: run them once
            if (mode == 2 && c.reps == 1 && c.name.find("Ed25519Sign 64 B (fixed key A") == std::string::npos &&
                c.name.find("X25519 (fixed scalar A") == std::string::npos) continue;   // A/A: the cheap rows and two of the slow ones are enough
            bad += RunCase(c, n);
        }
    }
    // Exit status only reflects "a positive control was missed" (the harness is blind); the verdict on the
    // library is the printed table, read by a person, because timing noise on shared machines makes an
    // automatic pass/fail on the real operations meaningless.
    return bad ? 1 : 0;
}
