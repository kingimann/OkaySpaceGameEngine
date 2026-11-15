// Differential test for the ONE inflate (engine/include/okay/Core/Inflate.hpp) against the three decoders it replaced.
// The old decoders are kept ONLY here, in tests/inflate_reference_old.hpp, as reference implementations:
//   ref1 = the old okay::Inflate / InflateCore (ImageDecode.cpp)   <-> new Zlib/Raw with Options::Lenient
//   ref2 = the old okay::fbx::Inflate (FbxReader.cpp)               <-> new Zlib with Options::Lenient
//   ref3 = the old okay::ownhttp::InflateBody (OwnHttpInflate.hpp)  <-> new Zlib/Gzip with strict Options
// Streams: (a) zlib streams made by Python's zlib (levels 0/1/6/9, window bits 9..15, strategies default/filtered/
// huffman-only/RLE/fixed, plain/sync-flush/full-flush) when python3 is available; (b) streams built here from RFC 1951
// (stored, fixed-Huffman with matches), which always run. Then bit-flipped, truncated, extended and spliced copies of
// all of them. Every old/new pair must give the same accept/reject decision and the same bytes. The only allowed
// differences are the ones the OLD lenient copies had by design versus the strict one (checksums, trailing bytes,
// incomplete codes): those are counted and reported, and checked to be one-directional (strict accepts => lenient accepts).
// Standalone: g++ -std=c++17 -I engine/include tests/test_inflate_differential.cpp (also run under ASan/UBSan).
// OKAY_REQUIRE_DIFFERENTIAL=1 turns a missing python3 into a failure.
#include "okay/Core/Inflate.hpp"
#include "okay/Platform/OwnHttpInflate.hpp"
#include "inflate_reference_old.hpp"

#include <cstdio>
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
    long compared = 0, bothOk = 0, bothBad = 0, strictOnly = 0, lenientOnly = 0;
    long pngTailDiff = 0, gzCompared = 0, gzOk = 0, capCompared = 0, pngCompared = 0, newRejectsOldAccepts = 0;
};
static Stats g_st;
static const uint8_t* P(const Bytes& b) { static const uint8_t z = 0; return b.empty() ? &z : b.data(); }
static std::string AsStr(const Bytes& b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

// One input, one cap: run every old/new pair.
static void CompareOne(const Bytes& z, size_t cap, bool alsoRaw) {
    using namespace okay::inflate;
    ++g_st.compared;
    std::string why;
    Bytes nw, o1, o2;
    // lenient pairs (ref1 zlib, ref2 zlib)
    {
        bool a = ref1::Inflate(P(z), z.size(), true, o1, cap);
        bool b = ref2::Inflate(P(z), z.size(), o2, cap);
        bool c = Zlib(P(z), z.size(), nw, Options::Lenient(cap), why);
        CHECK(a == c); CHECK(b == c);
        if (a == c && c) CHECK(o1 == nw);
        if (b == c && c) CHECK(o2 == nw);
        if (a != c || b != c) std::printf("  lenient zlib disagreement: ref1=%d ref2=%d new=%d size=%zu cap=%zu why=%s\n", a, b, c, z.size(), cap, why.c_str());
    }
    // raw (ref1 only)
    if (alsoRaw && z.size() >= 2) {
        bool a = ref1::Inflate(P(z) + 2, z.size() - 2, false, o1, cap);
        size_t used = 0;
        bool c = Raw(P(z) + 2, z.size() - 2, nw, Options::Lenient(cap), used, why);
        CHECK(a == c);
        if (a == c && c) CHECK(o1 == nw);
    }
    // PNG mode: stop at the wanted size, with the hint the PNG path gives
    if (cap) {
        ++g_st.pngCompared;
        bool a = ref1::InflateCore(P(z), z.size(), true, o1, cap, true, cap);
        Options po = Options::Lenient(cap, cap); po.stopAtMax = true;
        bool c = Zlib(P(z), z.size(), nw, po, why);
        // The one intended difference: once the wanted bytes are all decoded the new decoder stops and does not examine
        // the tail, where the old one still decoded one more symbol (and could refuse a cut-off or damaged tail).
        // So: old accepts => new accepts with the same bytes; new accepts and old refuses => new has exactly `cap` bytes.
        CHECK(!a || c);
        if (a && c) CHECK(o1 == nw);
        if (!a && c) { CHECK(nw.size() == cap); ++g_st.pngTailDiff; }
        if (a && !c) { std::printf("  PNG-mode disagreement: ref1=%d new=%d size=%zu cap=%zu out1=%zu why=%s\n", a, c, z.size(), cap, o1.size(), why.c_str()); if (getenv("DUMP")) { for (auto b : z) std::printf("%02x", b); std::printf("\n"); } }
    }
    // strict pair: ref3 zlib <-> new strict (the HTTP wrapper over the shared decoder). Must be IDENTICAL.
    {
        const std::string in = AsStr(z);
        std::string out3, why3, outN, whyN;
        bool a = ref3::InflateBody(in, false, cap, out3, why3);
        bool c = okay::ownhttp::InflateBody(in, false, cap, outN, whyN);
        CHECK(a == c);
        if (a && c) CHECK(out3 == outN);
        if (a != c) std::printf("  STRICT zlib disagreement: ref3=%d new=%d (%s / %s)\n", a, c, why3.c_str(), whyN.c_str());
        if (!a && !c) CHECK(why3 == whyN);   // same error text, the tests and logs rely on it
        // strict relation to the lenient decoders: strict accepts => lenient accepts, same bytes
        bool len = ref1::Inflate(P(z), z.size(), true, o1, cap);
        if (a) { CHECK(len); if (len) CHECK(AsStr(o1) == out3); }
        if (a && len) ++g_st.bothOk; else if (!a && !len) ++g_st.bothBad; else if (!a && len) ++g_st.strictOnly; else ++g_st.lenientOnly;
        if (a && !len) ++g_st.newRejectsOldAccepts;
    }
}

// gzip: ref3 <-> new, identical.
static void CompareGzip(const Bytes& g, size_t cap) {
    ++g_st.gzCompared;
    const std::string in = AsStr(g);
    std::string o3, w3, oN, wN;
    bool a = ref3::InflateBody(in, true, cap, o3, w3);
    bool c = okay::ownhttp::InflateBody(in, true, cap, oN, wN);
    CHECK(a == c);
    if (a && c) { CHECK(o3 == oN); ++g_st.gzOk; }
    if (!a && !c) CHECK(w3 == wN);
    if (a != c) std::printf("  gzip disagreement: ref3=%d new=%d (%s / %s)\n", a, c, w3.c_str(), wN.c_str());
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

// valid stream: every pair must decode it, with caps at, below and above its size.
static void ValidCase(Rng& rng, const Bytes& z, const Bytes& plain, int flips, bool exhaustive) {
    using namespace okay::inflate;
    std::string why; Bytes out;
    CHECK(Zlib(P(z), z.size(), out, Options(), why) && out == plain);             // strict decode gives the original
    CHECK(Zlib(P(z), z.size(), out, Options::Lenient(plain.size()), why) && out == plain);
    CompareOne(z, plain.size(), true);
    CompareOne(z, plain.size() + 1, false);
    if (!plain.empty()) { CompareOne(z, plain.size() - 1, false); CompareOne(z, plain.size() / 2, false); }
    CompareOne(z, 0, false);
    Mutations(rng, z, plain, flips, exhaustive, plain.size() + 64);
}

int main() {
    Rng rng{0x9E3779B97F4A7C15ull};
    const bool require = std::getenv("OKAY_REQUIRE_DIFFERENTIAL") && std::string(std::getenv("OKAY_REQUIRE_DIFFERENTIAL")) == "1";
    // (b) streams built here: always run
    int built = 0;
    for (int i = 0; i < 400; ++i) {
        Bytes plain; Bytes raw = RandomRaw(rng, plain, 1 + int(rng.Below(4)));
        Bytes z = ZlibWrap(raw, plain);
        ValidCase(rng, z, plain, 8, z.size() <= 150);
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
                    ValidCase(rng, z, plain, z.size() > 20000 ? 3 : 6, z.size() <= 120);
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
    std::printf("compared inputs (old vs new, every pair): %ld | PNG-mode: %ld | gzip: %ld (%ld valid)\n", g_st.compared, g_st.pngCompared, g_st.gzCompared, g_st.gzOk);
    std::printf("PNG mode: old refused but new accepted (cut-off or damaged tail after all wanted bytes were decoded, by design): %ld\n", g_st.pngTailDiff);
    std::printf("strict ref3 vs lenient ref1: both accept %ld, both refuse %ld, only lenient accepts (checksum/trailing/incomplete code, by design) %ld, only strict accepts (MUST be 0) %ld\n",
                g_st.bothOk, g_st.bothBad, g_st.strictOnly, g_st.lenientOnly);
    CHECK(g_st.lenientOnly == 0);
    CHECK(g_st.compared >= 1500);
    if (g_fail) { std::printf("FAILED: %d of %d checks\n", g_fail, g_checks); return 1; }
    std::printf("ok: %d checks\n", g_checks);
    return 0;
}
