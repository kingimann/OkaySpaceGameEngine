// SHA-256 comparison tool (not a test): runs the OLD copies (tests/sha256_old_refs.hpp) and the one new
// implementation (okay/Crypto/Sha256.hpp) side by side. See docs/sha256_comparison.md.
//   sha256_compare check <cases.bin>   verify every implementation against Python hashlib/hmac answers
//                                      (cases made by scripts/sha256_python_diff.py)
//   sha256_compare bench               speed (MB/s), PBKDF2 rate, allocation
//   sha256_compare big                 a 4.3 GB stream (length field past 2^32 bytes) through each implementation
// Written by us; the old copies are our own earlier code.
#include <okay/Crypto/Sha256.hpp>
#include <okay/Platform/Account/AccountService.hpp>
#include "sha256_old_refs.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <random>

// ---- allocation counter -----------------------------------------------------------------------------------------------
static std::size_t g_live = 0, g_peak = 0, g_total = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) throw std::bad_alloc();
    *p = n; g_live += n; g_total += n; if (g_live > g_peak) g_peak = g_live;
    return p + 2;
}
void operator delete(void* q) noexcept { if (!q) return; auto* p = static_cast<std::size_t*>(q) - 2; g_live -= *p; std::free(p); }
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }

namespace nw = okay::crypto;
using Bytes = std::vector<std::uint8_t>;
using Dig = std::array<std::uint8_t, 32>;

static Dig NewHash(const Bytes& m) { Dig d; nw::Sha256Hash(d.data(), m.data(), m.size()); return d; }
static Dig Old1Hash(const Bytes& m) { Dig d; okay_old::crypto::Sha256 s; if (!m.empty()) s.Update(m.data(), m.size()); s.Final(d.data()); return d; }
static Dig Old2Hash(const Bytes& m) { okay_old::account::Sha256 s; s.Update(m.data(), m.size()); return s.Digest(); }
static Dig Old3Hash(const Bytes& m) { return okay_old::voice::VoiceSha256(std::string(m.begin(), m.end())); }
static Dig NewStream(const Bytes& m, std::mt19937& r) {
    nw::Sha256 s; std::size_t i = 0;
    while (i < m.size()) { std::size_t n = std::min<std::size_t>(m.size() - i, r() % 200); s.Update(m.data() + i, n); i += n; if (!n && r() % 4 == 0) { n = std::min<std::size_t>(m.size() - i, 1); s.Update(m.data() + i, n); i += n; } }
    Dig d; s.Final(d.data()); return d;
}

static std::uint32_t Rd32(std::istream& f) { unsigned char b[4]; f.read((char*)b, 4); return b[0] | b[1] << 8 | b[2] << 16 | (std::uint32_t)b[3] << 24; }
static Bytes RdN(std::istream& f, std::size_t n) { Bytes b(n); if (n) f.read((char*)b.data(), (std::streamsize)n); return b; }

static int Check(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::puts("cannot open cases"); return 2; }
    std::mt19937 rng(12345);
    int nd = 0, nh = 0, np = 0, bad = 0;
    int c = 0;
    while ((c = f.get()) != EOF) {
        if (c == 0) {                       // digest
            Bytes m = RdN(f, Rd32(f)); Bytes want = RdN(f, 32);
            Dig w; std::copy(want.begin(), want.end(), w.begin());
            const bool ok = NewHash(m) == w && Old1Hash(m) == w && Old2Hash(m) == w && Old3Hash(m) == w && NewStream(m, rng) == w;
            if (!ok) { ++bad; std::printf("DIGEST MISMATCH len=%zu\n", m.size()); }
            ++nd;
        } else if (c == 1) {                // hmac
            const std::uint32_t kl = Rd32(f), ml = Rd32(f);
            Bytes k = RdN(f, kl), m = RdN(f, ml), want = RdN(f, 32);
            Dig w; std::copy(want.begin(), want.end(), w.begin());
            Dig n; nw::HmacSha256Mac(n.data(), k.data(), k.size(), m.data(), m.size());
            const Dig o = okay_old::anticheat::HmacSha256(std::string(k.begin(), k.end()), std::string(m.begin(), m.end()));
            if (n != w || o != w) { ++bad; std::printf("HMAC MISMATCH klen=%u mlen=%u new=%d old=%d\n", kl, ml, n == w, o == w); }
            ++nh;
        } else {                            // pbkdf2
            const std::uint32_t pl = Rd32(f), sl = Rd32(f), it = Rd32(f), dl = Rd32(f);
            Bytes p = RdN(f, pl), s = RdN(f, sl), want = RdN(f, dl);
            Bytes got(dl); nw::Pbkdf2HmacSha256(got.data(), dl, p.data(), p.size(), s.data(), s.size(), it);
            bool ok = got == want;
            if (dl == 32) {
                const Dig o = okay_old::account::Pbkdf2Sha256(std::string(p.begin(), p.end()), s, it);
                ok = ok && std::equal(o.begin(), o.end(), want.begin());
            }
            if (!ok) { ++bad; std::printf("PBKDF2 MISMATCH plen=%u slen=%u it=%u dk=%u\n", pl, sl, it, dl); }
            ++np;
        }
    }
    std::printf("checked %d digests (4 implementations + streaming), %d HMACs, %d PBKDF2: %d mismatches\n", nd, nh, np, bad);
    return bad ? 1 : 0;
}

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
template <class F> static double Mbps(std::size_t bytes, int reps, F fn) {
    fn();                                    // warm up
    double best = 1e30;
    for (int i = 0; i < 9; ++i) { const double t = Now(); for (int r = 0; r < reps; ++r) fn(); best = std::min(best, Now() - t); }
    return (double)bytes * reps / best / 1e6;
}
static volatile std::uint8_t g_sink;
static std::uint8_t Fin(okay_old::crypto::Sha256& s) { std::uint8_t d[32]; s.Final(d); return d[0]; }
static std::uint8_t Fin(okay_old::account::Sha256& s) { return s.Digest()[0]; }
static std::uint8_t Fin(okay::crypto::Sha256& s) { std::uint8_t d[32]; s.Final(d); return d[0]; }

static void Bench() {
    for (std::size_t size : {std::size_t(1) << 20, std::size_t(50) << 20}) {
        Bytes m(size); std::mt19937 r(1); for (auto& b : m) b = (std::uint8_t)r();
        const int reps = size < (2u << 20) ? 40 : 2;
        std::printf("one-shot %zu MB (MB/s): old1=%.0f old2=%.0f old3=%.0f new=%.0f\n", size >> 20,
            Mbps(size, reps, [&] { g_sink = Old1Hash(m)[0]; }), Mbps(size, reps, [&] { g_sink = Old2Hash(m)[0]; }),
            Mbps(size, reps, [&] { g_sink = Old3Hash(m)[0]; }), Mbps(size, reps, [&] { g_sink = NewHash(m)[0]; }));
    }
    {   // file-style streaming: 64 KB chunks (what FileSha256 does) over 50 MB
        const std::size_t size = std::size_t(50) << 20; Bytes m(size, 7);
        auto run = [&](auto mk) { return Mbps(size, 2, [&] { auto s = mk(); for (std::size_t i = 0; i < size; i += 65536) s.Update(m.data() + i, 65536); g_sink = Fin(s); }); };
        std::printf("stream 50 MB in 64 KB updates (MB/s): old1=%.0f old2=%.0f new=%.0f\n",
            run([] { return okay_old::crypto::Sha256(); }), run([] { return okay_old::account::Sha256(); }), run([] { return nw::Sha256(); }));
    }
    {   // many small updates: 16 bytes at a time over 8 MB
        const std::size_t size = std::size_t(8) << 20; Bytes m(size, 9);
        auto run = [&](auto mk) { return Mbps(size, 2, [&] { auto s = mk(); for (std::size_t i = 0; i < size; i += 16) s.Update(m.data() + i, 16); g_sink = Fin(s); }); };
        std::printf("stream 8 MB in 16-byte updates (MB/s): old1=%.0f old2=%.0f new=%.0f\n",
            run([] { return okay_old::crypto::Sha256(); }), run([] { return okay_old::account::Sha256(); }), run([] { return nw::Sha256(); }));
    }
    {   // many tiny messages: 64-byte one-shot x 1e6
        Bytes m(64, 5); const int n = 1000000;
        auto t = [&](auto fn) { const double a = Now(); for (int i = 0; i < n; ++i) { m[0] = (std::uint8_t)i; g_sink = fn(m)[0]; } return Now() - a; };
        std::printf("1e6 x 64-byte messages (seconds): old1=%.2f old2=%.2f old3=%.2f new=%.2f\n", t(Old1Hash), t(Old2Hash), t(Old3Hash), t(NewHash));
    }
    {   // PBKDF2 at the account service's rounds
        const std::uint32_t rounds = okay::account::detail::kPasswordRounds;
        const std::vector<std::uint8_t> salt(16, 3);
        double a = Now(); auto od = okay_old::account::Pbkdf2Sha256("hunter2hunter2", salt, rounds); const double to = Now() - a;
        a = Now(); Dig nd; nw::Pbkdf2HmacSha256(nd.data(), 32, (const std::uint8_t*)"hunter2hunter2", 14, salt.data(), salt.size(), rounds); const double tn = Now() - a;
        std::printf("PBKDF2-HMAC-SHA256, %u rounds (seconds): old=%.3f new=%.3f same=%d\n", rounds, to, tn, od == nd);
    }
    {   // allocation: one-shot 1 MB
        Bytes m(1 << 20, 1);
        auto alloc = [&](auto fn) { g_peak = g_live; const std::size_t before = g_live, tot = g_total; fn(); return std::make_pair(g_peak - before, g_total - tot); };
        auto a1 = alloc([&] { g_sink = Old1Hash(m)[0]; }); auto a2 = alloc([&] { g_sink = Old2Hash(m)[0]; });
        auto a3 = alloc([&] { g_sink = Old3Hash(m)[0]; }); auto a4 = alloc([&] { g_sink = NewHash(m)[0]; });
        std::printf("heap peak bytes hashing 1 MB (the std::string copy for old3 is made by the caller and counted): old1=%zu old2=%zu old3=%zu new=%zu\n",
            a1.first, a2.first, a3.first, a4.first);
        std::printf("object sizes: old1=%zu old2=%zu new=%zu\n", sizeof(okay_old::crypto::Sha256), sizeof(okay_old::account::Sha256), sizeof(nw::Sha256));
    }
}

static void Big() {   // 4,300,000,000 bytes of 0..255 pattern in 1 MB pieces: the length field passes 2^32 bytes (2^35 bits)
    Bytes blk(1 << 20); for (std::size_t i = 0; i < blk.size(); ++i) blk[i] = (std::uint8_t)(i * 7 + (i >> 8));
    const std::size_t full = 4100;                       // 4,299,161,600 bytes
    const std::size_t tail = 12345;
    nw::Sha256 n; okay_old::crypto::Sha256 o1; okay_old::account::Sha256 o2;
    for (std::size_t i = 0; i < full; ++i) { n.Update(blk.data(), blk.size()); o1.Update(blk.data(), blk.size()); o2.Update(blk.data(), blk.size()); }
    n.Update(blk.data(), tail); o1.Update(blk.data(), tail); o2.Update(blk.data(), tail);
    Dig dn, d1, d2; n.Final(dn.data()); o1.Final(d1.data()); d2 = o2.Digest();
    auto hex = [](const Dig& d) { return okay::account::detail::ToHex(d.data(), 32); };
    std::printf("total bytes %zu\nnew  %s\nold1 %s\nold2 %s\n", full * blk.size() + tail, hex(dn).c_str(), hex(d1).c_str(), hex(d2).c_str());
}

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "check")) return Check(argv[2]);
    if (argc >= 2 && !std::strcmp(argv[1], "bench")) { Bench(); return 0; }
    if (argc >= 2 && !std::strcmp(argv[1], "big")) { Big(); return 0; }
    std::puts("usage: sha256_compare check <cases.bin> | bench | big");
    return 2;
}
