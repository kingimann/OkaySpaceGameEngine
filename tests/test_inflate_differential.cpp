// Differential test for the ONE inflate (engine/include/okay/Core/Inflate.hpp) against a slow reference decoder written
// fresh from the RFCs (tests/inflate_reference_slow.hpp: one bit at a time, canonical codes computed the long way).
// Three policies are compared, each with identical accept/refuse decisions and identical bytes:
//   lenient (images, models: Options::Lenient), PNG mode (Lenient + stopAtMax), strict (the HTTP client: zlib and gzip).
// Streams: (a) zlib streams made by Python's zlib (levels 0/1/6/9, window bits 9..15, strategies default/filtered/
// huffman-only/RLE/fixed, plain/sync-flush/full-flush) when python3 is available; (b) streams built here from RFC 1951
// (stored, fixed-Huffman with matches), which always run. Then bit-flipped, truncated, extended and spliced copies of
// all of them, and every single-bit flip and truncation of the small ones. Strict accepting must always imply lenient
// accepting, with the same bytes. A last section checks that a size claimed by a PNG header cannot make the decoder
// allocate that much for a tiny stream (peak heap measured with a counting operator new).
// History: before the reference was written fresh, the same test compared against verbatim copies of the three decoders
// that Core/Inflate.hpp replaced (commit 0ea3b26a, file tests/inflate_reference_old.hpp): 257,014 inputs, 252,460 PNG-mode
// inputs and 22,898 gzip inputs, no disagreement. Those copies are no longer in the tree (only in git history).
// Standalone: g++ -std=c++17 -I engine/include tests/test_inflate_differential.cpp (also run under ASan/UBSan).
// OKAY_REQUIRE_DIFFERENTIAL=1 turns a missing python3 into a failure.
#include "okay/Core/Inflate.hpp"
#include "okay/Platform/OwnHttpInflate.hpp"
#include "inflate_reference_slow.hpp"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <new>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; if (g_fail < 40) std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

using Bytes = std::vector<uint8_t>;
struct Rng { uint64_t s; uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } size_t Below(size_t n) { return n ? size_t(Next() % n) : 0; } };

// ---- stream builders (from RFC 1951 3.2.3 / 3.2.4 / 3.2.6) --------------------------------------------------
struct BitWriter {
    Bytes out; uint32_t acc = 0; int n = 0;
    void Bits(uint32_t v, int c) { for (int i = 0; i < c; ++i) { acc |= ((v >> i) & 1u) << n; if (++n == 8) { out.push_back(uint8_t(acc)); acc = 0; n = 0; } } }
    void Code(uint32_t code, int len) { for (int i = len - 1; i >= 0; --i) Bits((code >> i) & 1u, 1); }
    void Align() { if (n) { out.push_back(uint8_t(acc)); acc = 0; n = 0; } }
};
static void FixedLit(BitWriter& w, int sym) {
    if (sym < 144) w.Code(0x30 + sym, 8); else if (sym < 256) w.Code(0x190 + sym - 144, 9);
    else if (sym < 280) w.Code(sym - 256, 7); else w.Code(0xC0 + sym - 280, 8);
}
static const int kLB[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int kLE[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int kDB[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int kDE[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
// Random but valid raw deflate: a mix of stored blocks and fixed-Huffman blocks with literals and matches. Returns the plain bytes too.
static Bytes RandomRaw(Rng& r, Bytes& plain, int blocks) {
    BitWriter w; plain.clear();
    for (int b = 0; b < blocks; ++b) {
        const bool last = b == blocks - 1;
        if (r.Below(3) == 0) {
            w.Bits(last, 1); w.Bits(0, 2); w.Align();
            size_t n = r.Below(200);
            w.out.push_back(uint8_t(n)); w.out.push_back(uint8_t(n >> 8)); w.out.push_back(uint8_t(~n)); w.out.push_back(uint8_t((~n) >> 8));
            for (size_t i = 0; i < n; ++i) { uint8_t c = uint8_t(r.Next()); w.out.push_back(c); plain.push_back(c); }
        } else {
            w.Bits(last, 1); w.Bits(1, 2);
            size_t items = 1 + r.Below(120);
            for (size_t i = 0; i < items; ++i) {
                if (plain.size() >= 3 && r.Below(3) == 0) {
                    int li = int(r.Below(29)); size_t len = kLB[li] + (kLE[li] ? r.Below(size_t(1) << kLE[li]) : 0);
                    if (li == 28) len = 258;
                    int di; size_t back;
                    for (;;) { di = int(r.Below(30)); back = kDB[di] + (kDE[di] ? r.Below(size_t(1) << kDE[di]) : 0); if (back <= plain.size()) break; }
                    FixedLit(w, 257 + li); w.Bits(uint32_t(len - kLB[li]), kLE[li]);
                    w.Code(uint32_t(di), 5); w.Bits(uint32_t(back - kDB[di]), kDE[di]);
                    for (size_t k = 0; k < len; ++k) plain.push_back(plain[plain.size() - back]);
                } else { uint8_t c = uint8_t(r.Next()); FixedLit(w, c); plain.push_back(c); }
            }
            FixedLit(w, 256);
        }
    }
    w.Align();
    return w.out;
}
static Bytes ZlibWrap(const Bytes& raw, const Bytes& plain) {
    Bytes z = {0x78, 0x9c};
    z.insert(z.end(), raw.begin(), raw.end());
    uint32_t a = okay::inflate::detail::Adler32(plain.data(), plain.size());
    for (int i = 3; i >= 0; --i) z.push_back(uint8_t(a >> (8 * i)));
    return z;
}
static Bytes GzipWrap(const Bytes& raw, const Bytes& plain) {
    Bytes g = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3};
    g.insert(g.end(), raw.begin(), raw.end());
    uint32_t c = okay::inflate::detail::Crc32(plain.data(), plain.size()), sz = uint32_t(plain.size());
    for (int i = 0; i < 4; ++i) g.push_back(uint8_t(c >> (8 * i)));
    for (int i = 0; i < 4; ++i) g.push_back(uint8_t(sz >> (8 * i)));
    return g;
}

// ---- comparison ----------------------------------------------------------------------------------------------
struct Stats {
    long compared = 0, lenientOk = 0, strictOk = 0, lenientOnly = 0, strictOnly = 0, pngCompared = 0, pngTailDiff = 0;
    long gzCompared = 0, gzOk = 0;
};
static Stats g_st;
static const uint8_t* P(const Bytes& b) { static const uint8_t z = 0; return b.empty() ? &z : b.data(); }
static std::string AsStr(const Bytes& b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }
static std::string HexOf(const Bytes& b) { std::string s; char t[4]; for (uint8_t c : b) { std::snprintf(t, sizeof t, "%02x", c); s += t; } return s; }

// One input, one cap: the engine decoder (lenient, PNG mode, strict) against the slow reference with the same policy.
// Accept/refuse and the bytes must be identical.
static void CompareOne(const Bytes& z, size_t cap, bool alsoRaw) {
    using namespace okay::inflate;
    ++g_st.compared;
    std::string why;
    Bytes nw, rf;
    auto policy = [&](bool lenient, bool stop) { refslow::Policy p; p.maxOut = cap; p.lenient = lenient; p.stopAtMax = stop; return p; };
    // lenient zlib
    bool len;
    {
        bool a = refslow::Zlib(P(z), z.size(), policy(true, false), rf);
        bool c = Zlib(P(z), z.size(), nw, Options::Lenient(cap), why);
        CHECK(a == c);
        if (a == c && c) CHECK(rf == nw);
        if (a != c) std::printf("  lenient zlib disagreement: ref=%d new=%d size=%zu cap=%zu why=%s hex=%s\n", a, c, z.size(), cap, why.c_str(), z.size() < 200 ? HexOf(z).c_str() : "");
        len = a;
    }
    // raw, lenient
    if (alsoRaw && z.size() >= 2) {
        size_t used = 0, used2 = 0;
        bool a = refslow::Raw(P(z) + 2, z.size() - 2, policy(true, false), rf, used);
        bool c = Raw(P(z) + 2, z.size() - 2, nw, Options::Lenient(cap), used2, why);
        CHECK(a == c);
        if (a == c && c) { CHECK(rf == nw); CHECK(used == used2); }
    }
    // PNG mode: stop at the wanted size (with the hint the PNG path gives)
    if (cap) {
        ++g_st.pngCompared;
        bool a = refslow::Zlib(P(z), z.size(), policy(true, true), rf);
        Options po = Options::Lenient(cap, cap); po.stopAtMax = true;
        bool c = Zlib(P(z), z.size(), nw, po, why);
        CHECK(a == c);
        if (a == c && c) CHECK(rf == nw);
        if (a != c) std::printf("  PNG-mode disagreement: ref=%d new=%d size=%zu cap=%zu\n", a, c, z.size(), cap);
        if (a && c && rf.size() == cap && !len) ++g_st.pngTailDiff;   // accepted only because the tail after the wanted bytes is not examined
    }
    // strict zlib: the engine's Zlib with default options, and the HTTP wrapper over it
    {
        bool a = refslow::Zlib(P(z), z.size(), policy(false, false), rf);
        Options so; so.maxOutput = cap;
        bool c = Zlib(P(z), z.size(), nw, so, why);
        CHECK(a == c);
        if (a == c && c) CHECK(rf == nw);
        if (a != c) std::printf("  STRICT zlib disagreement: ref=%d new=%d size=%zu cap=%zu why=%s\n", a, c, z.size(), cap, why.c_str());
        std::string out2, why2;
        bool d = okay::ownhttp::InflateBody(AsStr(z), false, cap, out2, why2);
        CHECK(d == a);
        if (d && a) CHECK(out2 == AsStr(rf));
        if (!d) CHECK(out2.empty() && !why2.empty());
        // strict accepts => lenient accepts, with the same bytes (the stricter decoder is a subset)
        if (a) { CHECK(len); ++g_st.strictOk; } else if (len) ++g_st.lenientOnly;
        if (len) ++g_st.lenientOk;
    }
}

// gzip: engine (and the HTTP wrapper) against the reference.
static void CompareGzip(const Bytes& g, size_t cap) {
    using namespace okay::inflate;
    ++g_st.gzCompared;
    refslow::Policy p; p.maxOut = cap; p.lenient = false;
    Bytes rf, nw; std::string why;
    bool a = refslow::Gzip(P(g), g.size(), p, rf);
    Options so; so.maxOutput = cap;
    bool c = Gzip(P(g), g.size(), nw, so, why);
    CHECK(a == c);
    if (a == c && c) { CHECK(rf == nw); ++g_st.gzOk; }
    if (a != c) std::printf("  gzip disagreement: ref=%d new=%d (%s)\n", a, c, why.c_str());
    std::string o2, w2;
    bool d = okay::ownhttp::InflateBody(AsStr(g), true, cap, o2, w2);
    CHECK(d == a);
    if (d && a) CHECK(o2 == AsStr(rf));
}

// Damaged copies of one valid zlib stream (the plain bytes are given only to build the gzip form).
static void Mutations(Rng& rng, const Bytes& z, const Bytes& plain, int flips, bool exhaustive, size_t cap) {
    auto one = [&](const Bytes& m) { CompareOne(m, cap, false); };
    if (exhaustive) {
        for (size_t k = 0; k < z.size(); ++k) for (int bit = 0; bit < 8; ++bit) { Bytes m = z; m[k] ^= uint8_t(1 << bit); one(m); }
        for (size_t k = 0; k < z.size(); ++k) one(Bytes(z.begin(), z.begin() + long(k)));   // every truncation
    } else {
        for (int i = 0; i < flips && !z.empty(); ++i) { Bytes m = z; m[rng.Below(m.size())] ^= uint8_t(1 << rng.Below(8)); one(m); }
        if (!z.empty()) one(Bytes(z.begin(), z.begin() + long(rng.Below(z.size()))));
    }
    { Bytes m = z; m.push_back(uint8_t(rng.Next())); one(m); }                       // one extra byte
    if (z.size() > 8) { Bytes m = z; m.erase(m.begin() + long(2 + rng.Below(m.size() - 4))); one(m); }   // a deleted byte
    if (z.size() > 8) { Bytes m = z; m.insert(m.begin() + long(2 + rng.Below(m.size() - 4)), uint8_t(rng.Next())); one(m); }
    for (int i = 0; i < 3 && z.size() > 6; ++i) {                                       // a damaged trailer
        Bytes m = z; m[m.size() - 1 - rng.Below(4)] ^= uint8_t(1 << rng.Below(8)); one(m);
    }
    // gzip form, valid and damaged
    Bytes raw(z.begin() + 2, z.size() >= 6 ? z.end() - 4 : z.begin() + 2);
    Bytes g = GzipWrap(raw, plain);
    CompareGzip(g, cap);
    for (int i = 0; i < flips / 2 + 1; ++i) { Bytes m = g; m[rng.Below(m.size())] ^= uint8_t(1 << rng.Below(8)); CompareGzip(m, cap); }
    CompareGzip(Bytes(g.begin(), g.begin() + long(rng.Below(g.size()))), cap);
    { Bytes m = g; m.push_back(0); CompareGzip(m, cap); }
}

static const char* kPyCorpus = R"PY(
import sys, zlib, random, struct
random.seed(20261115)
out = open(sys.argv[1] + "/corpus.bin", "wb")
def text(n):
    words = [b"the", b"engine", b"deflate", b"stream", b"okay", b"space", b"game", b"mesh", b"texture", b"a", b"of", b"and"]
    b = bytearray()
    while len(b) < n: b += random.choice(words) + b" "
    return bytes(b[:n])
def data_kinds():
    yield b""
    yield b"x"
    yield text(3000)
    yield bytes(random.getrandbits(8) for _ in range(2500))
    yield bytes(20000)
    yield (b"abc" * 5000)
    yield text(1500) + bytes(random.getrandbits(8) for _ in range(500)) + bytes(2000) + text(1500)
    yield bytes((i * 7 + i // 13) & 255 for i in range(40000))
count = 0
for level in (0, 1, 6, 9):
    for wbits in range(9, 16):
        for strat in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE, zlib.Z_FIXED):
            for flushmode in (None, zlib.Z_SYNC_FLUSH, zlib.Z_FULL_FLUSH):
                for k, d in enumerate(data_kinds()):
                    if (k + level + wbits + strat) % 3 != 0 and flushmode is None and k not in (0, 1, 2): continue
                    c = zlib.compressobj(level, zlib.DEFLATED, wbits, 8, strat)
                    z = b""
                    if flushmode is None:
                        z = c.compress(d) + c.flush()
                    else:
                        step = max(1, len(d) // 5)
                        for i in range(0, len(d), step):
                            z += c.compress(d[i:i + step]) + c.flush(flushmode)
                        z += c.flush()
                    assert zlib.decompress(z) == d
                    out.write(struct.pack("<II", len(d), len(z))); out.write(d); out.write(z)
                    count += 1
print(count)
)PY";

static bool ReadFile(const std::string& path, Bytes& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); std::string s = ss.str();
    out.assign(s.begin(), s.end());
    return true;
}
static uint32_t U32(const Bytes& b, size_t at) { return b[at] | (uint32_t(b[at + 1]) << 8) | (uint32_t(b[at + 2]) << 16) | (uint32_t(b[at + 3]) << 24); }

// valid stream: every policy must decode it, with caps at, below and above its size, and its damaged copies must agree.
// The slow reference costs time in proportion to the OUTPUT size, so streams that decode to more than 4000 bytes get a
// lighter treatment (full-size decode in every policy, the cap one byte short, and damaged copies for one stream in four).
// That keeps the whole test inside a fixed budget (about 15 s optimised, about a minute under ASan+UBSan on a loaded
// machine); small streams, where the header and table logic live, get every variant.
static void ValidCase(Rng& rng, const Bytes& z, const Bytes& plain, int flips, bool exhaustive, int index) {
    using namespace okay::inflate;
    std::string why; Bytes out;
    CHECK(Zlib(P(z), z.size(), out, Options(), why) && out == plain);             // strict decode gives the original
    CHECK(Zlib(P(z), z.size(), out, Options::Lenient(plain.size()), why) && out == plain);
    const bool big = plain.size() > 4000;
    CompareOne(z, plain.size(), !big);
    if (big) {
        CompareOne(z, plain.size() - 1, false);
        if (index % 4 == 0) Mutations(rng, z, plain, 2, false, plain.size() + 64);
        return;
    }
    CompareOne(z, plain.size() + 1, false);
    if (!plain.empty()) { CompareOne(z, plain.size() - 1, false); CompareOne(z, plain.size() / 2, false); }
    CompareOne(z, 0, false);
    Mutations(rng, z, plain, flips, exhaustive, plain.size() + 64);
}

// ---- heap accounting (peak live bytes through operator new) -------------------------------------------------------
static std::atomic<size_t> g_live{0}, g_peak{0};
static void* CountedAlloc(size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<size_t*>(raw) = n;
    size_t now = g_live.fetch_add(n) + n, pk = g_peak.load();
    while (now > pk && !g_peak.compare_exchange_weak(pk, now)) {}
    return static_cast<char*>(raw) + 16;
}
static void CountedFree(void* p) {
    if (!p) return;
    void* raw = static_cast<char*>(p) - 16;
    g_live.fetch_sub(*static_cast<size_t*>(raw));
    std::free(raw);
}
void* operator new(size_t n) { return CountedAlloc(n); }
void* operator new[](size_t n) { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, size_t) noexcept { CountedFree(p); }

// A PNG header can claim an enormous pixel size (up to ~2 GiB of raw bytes) with a few bytes of image data. The PNG path asks
// for exactly that many bytes (maxOutput = hint = claimed size, stopAtMax). The first allocation must be limited by what the
// input could possibly produce (DEFLATE cannot expand more than 1032:1), not by the claim.
static void HintTests() {
    using namespace okay::inflate;
    Rng rng{4242};
    Bytes plain; Bytes raw = RandomRaw(rng, plain, 2);
    Bytes z = ZlibWrap(raw, plain);
    std::string why; Bytes out;
    const size_t claim = size_t(1) << 31;
    for (int mode = 0; mode < 3; ++mode) {
        Options o = Options::Lenient(claim, claim);
        o.stopAtMax = mode == 0;
        if (mode == 2) { o = Options(); o.maxOutput = claim; o.hint = claim; }
        out.clear(); out.shrink_to_fit();
        const size_t before = g_live.load(); g_peak = before;
        bool ok = Zlib(P(z), z.size(), out, o, why);
        const size_t peak = g_peak.load() - before;
        CHECK(ok && out == plain);
        CHECK(peak < (size_t(1) << 20));   // nowhere near the 2 GiB claimed (budget is 1032 x input + slack)
        CHECK(peak <= z.size() * 1032 + 4096 + 4 * plain.size() + 65536);
        std::printf("hint test (mode %d): %zu bytes in, claimed %zu, peak extra heap %zu bytes\n", mode, z.size(), claim, peak);
    }
    // a truncated stream with the same huge claim: refused, still no big allocation
    {
        Options o = Options::Lenient(claim, claim); o.stopAtMax = true;
        Bytes cut(z.begin(), z.begin() + long(z.size() / 2));
        out.clear(); out.shrink_to_fit();
        const size_t before = g_live.load(); g_peak = before;
        bool ok = Zlib(P(cut), cut.size(), out, o, why);
        const size_t peak = g_peak.load() - before;
        CHECK(!ok || out.size() <= plain.size());
        CHECK(peak < (size_t(1) << 20));
    }
    // stopAtMax at the cap: a match that crosses it is cut (success, exactly `cap` bytes); a stored block that crosses it
    // is refused (documented in Core/Inflate.hpp, StoredBlock; the older image decoder did the same)
    {
        BitWriter m; m.Bits(1, 1); m.Bits(1, 2);
        FixedLit(m, 'x'); FixedLit(m, 285); m.Code(0, 5); FixedLit(m, 256); m.Align();   // 'x' then a 258-byte match at distance 1
        Bytes mp(259, 'x');
        Bytes zm = ZlibWrap(m.out, mp);
        Options o = Options::Lenient(100, 100); o.stopAtMax = true;
        CHECK(Zlib(P(zm), zm.size(), out, o, why) && out == Bytes(100, 'x'));
        BitWriter st; st.Bits(1, 1); st.Bits(0, 2); st.Align();
        st.out.push_back(10); st.out.push_back(0); st.out.push_back(uint8_t(~10)); st.out.push_back(0xff);
        for (int i = 0; i < 10; ++i) st.out.push_back('s');
        Bytes zs = ZlibWrap(st.out, Bytes(10, 's'));
        CHECK(Zlib(P(zs), zs.size(), out, o, why) && out == Bytes(10, 's'));   // cap 100: the 10 stored bytes fit
        Options o5 = Options::Lenient(5, 5); o5.stopAtMax = true;
        CHECK(!Zlib(P(zs), zs.size(), out, o5, why));                     // cap 5: the stored block crosses it
        CHECK(Zlib(P(zs), zs.size(), out, Options::Lenient(10), why) && out == Bytes(10, 's'));
    }
    // real data still grows past a too small guess and decodes correctly: 3000 matches of 258 bytes, hint 10
    {
        BitWriter w; w.Bits(1, 1); w.Bits(1, 2);
        FixedLit(w, 97); Bytes big{97};
        for (int i = 0; i < 3000; ++i) { FixedLit(w, 285); w.Code(0, 5); big.insert(big.end(), 258, 97); }
        FixedLit(w, 256); w.Align();
        Bytes zz = ZlibWrap(w.out, big);
        Options o = Options::Lenient(big.size(), 10);
        CHECK(Zlib(P(zz), zz.size(), out, o, why) && out == big);
        o.hint = 0; o.strictCodes = true; o.checkTrailer = true;
        CHECK(Zlib(P(zz), zz.size(), out, o, why) && out == big);
        // the same stream with the real size as the hint is not clamped away (1032 x input is far above it)
        o = Options::Lenient(big.size(), big.size());
        CHECK(Zlib(P(zz), zz.size(), out, o, why) && out == big && out.capacity() >= big.size());
    }
}

int main() {
    Rng rng{0x9E3779B97F4A7C15ull};
    const bool require = std::getenv("OKAY_REQUIRE_DIFFERENTIAL") && std::string(std::getenv("OKAY_REQUIRE_DIFFERENTIAL")) == "1";
    // (b) streams built here: always run
    int built = 0;
    for (int i = 0; i < 400; ++i) {
        Bytes plain; Bytes raw = RandomRaw(rng, plain, 1 + int(rng.Below(4)));
        Bytes z = ZlibWrap(raw, plain);
        ValidCase(rng, z, plain, 8, z.size() <= 150 && i % 8 == 0, i);
        ++built;
    }
    std::printf("built-here streams: %d\n", built);
    // (a) streams from Python's zlib
    int pyCount = 0;
#if !defined(_WIN32)
    {
        char tmpl[] = "/tmp/okay_inflate_dd_XXXXXX";
        if (!mkdtemp(tmpl)) { std::printf("SKIP python corpus: no temp directory\n"); CHECK(!require); }
        else {
            const std::string dir = tmpl;
            { std::ofstream f(dir + "/gen.py"); f << kPyCorpus; }
            std::string cmd = "python3 -I " + dir + "/gen.py " + dir + " > " + dir + "/count.txt 2>" + dir + "/err.txt";
            int rc = std::system(cmd.c_str());
            Bytes blob;
            if (rc != 0 || !ReadFile(dir + "/corpus.bin", blob)) { std::printf("SKIP python corpus: python3 with zlib is not available (rc=%d)\n", rc); CHECK(!require); }
            else {
                size_t at = 0;
                while (at + 8 <= blob.size()) {
                    size_t pl = U32(blob, at), zl = U32(blob, at + 4); at += 8;
                    if (at + pl + zl > blob.size()) { CHECK(false); break; }
                    Bytes plain(blob.begin() + long(at), blob.begin() + long(at + pl)); at += pl;
                    Bytes z(blob.begin() + long(at), blob.begin() + long(at + zl)); at += zl;
                    ValidCase(rng, z, plain, 6, z.size() <= 120 && pyCount % 8 == 0, pyCount);
                    ++pyCount;
                }
            }
            (void)!std::system(("rm -rf " + dir).c_str());
        }
    }
#endif
    std::printf("python zlib streams: %d (levels 0/1/6/9, window bits 9..15, 5 strategies, plain/sync/full flush)\n", pyCount);
    if (pyCount == 0) { std::printf("NOTE: python corpus did not run; only built-here streams were compared\n"); }
    else CHECK(pyCount >= 500);
    std::printf("compared inputs (engine vs slow reference, every policy): %ld | PNG-mode: %ld | gzip: %ld (%ld valid)\n", g_st.compared, g_st.pngCompared, g_st.gzCompared, g_st.gzOk);
    std::printf("lenient accepted %ld, strict accepted %ld, only lenient accepted (checksum / trailing bytes / incomplete code, by design) %ld; PNG-mode accepted a stream that plain lenient refuses (it goes on past the wanted size): %ld\n",
                g_st.lenientOk, g_st.strictOk, g_st.lenientOnly, g_st.pngTailDiff);
    CHECK(g_st.compared >= 1500);
    HintTests();
    std::printf("elapsed %.1f s\n", double(std::clock()) / CLOCKS_PER_SEC);
    if (g_fail) { std::printf("FAILED: %d of %d checks\n", g_fail, g_checks); return 1; }
    std::printf("ok: %d checks\n", g_checks);
    return 0;
}
