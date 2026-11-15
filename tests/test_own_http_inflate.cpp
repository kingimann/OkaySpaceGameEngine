// Tests for the table-driven inflate in engine/include/okay/Platform/OwnHttpInflate.hpp (written from RFC 1950, 1951, 1952).
//  - the length/distance tables, derived from the RFC 1951 3.2.5 rule, against the printed table
//  - the slow path for Huffman codes longer than the 10-bit first-level table
//  - a differential run against Python's zlib (needs python3; SKIPs with a message when it is missing,
//    unless OKAY_REQUIRE_DIFFERENTIAL=1, which turns the skip into a failure)
//  - the zip-bomb cap, with the peak heap use measured (global operator new is counted)
//  - every strict prefix refused, mutation fuzz, Crc32 from two threads
// Standalone: g++ -std=c++17 -pthread -I engine/include tests/test_own_http_inflate.cpp  (also run under ASan/UBSan).
#include "okay/Platform/OwnHttpInflate.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace okay::ownhttp;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- heap accounting: live and peak bytes through operator new ------------------------------------
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

struct Rng { uint64_t s; uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } size_t Below(size_t n) { return n ? size_t(Next() % n) : 0; } };

// ---- small encoder helpers (fixed and stored blocks; written from RFC 1951 3.2.3 and 3.2.4) ---------
struct BitWriter {
    std::string out; uint32_t acc = 0; int n = 0;
    void Bits(uint32_t v, int c) { for (int i = 0; i < c; ++i) { acc |= ((v >> i) & 1u) << n; if (++n == 8) { out.push_back(char(acc)); acc = 0; n = 0; } } }
    void Code(uint32_t code, int len) { for (int i = len - 1; i >= 0; --i) Bits((code >> i) & 1u, 1); }   // codes go MSB first
    std::string Finish() { if (n) out.push_back(char(acc)); return out; }
};
static std::string Gzip(const std::string& raw, const std::string& plain) {
    std::string g("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
    g += raw;
    uint32_t c = inflate_detail::Crc32(plain), sz = uint32_t(plain.size());
    for (int i = 0; i < 4; ++i) g.push_back(char(c >> (8 * i)));
    for (int i = 0; i < 4; ++i) g.push_back(char(sz >> (8 * i)));
    return g;
}
// One literal 'a' then `matches` length-258 distance-1 matches, as a fixed-Huffman final block.
static std::string BombRaw(size_t matches) {
    BitWriter w;
    w.Bits(1, 1); w.Bits(1, 2);
    w.Code(0x30 + 'a', 8);
    for (size_t i = 0; i < matches; ++i) { w.Code(0xC0 + (285 - 280), 8); w.Code(0, 5); }
    w.Code(0, 7);
    return w.Finish();
}

// ---- 1. tables: derived rule against the table printed in RFC 1951 section 3.2.5 ---------------------
static void TestTables() {
    using namespace inflate_detail;
    // typed from the RFC's table: code 257..285 -> (extra bits, smallest length)
    static const int lenExtraRfc[29] = {0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5, 0};
    static const int lenBaseRfc[29] = {3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31, 35,43,51,59, 67,83,99,115, 131,163,195,227, 258};
    // code 0..29 -> (extra bits, smallest distance)
    static const int distExtraRfc[30] = {0,0,0,0, 1,1, 2,2, 3,3, 4,4, 5,5, 6,6, 7,7, 8,8, 9,9, 10,10, 11,11, 12,12, 13,13};
    static const int distBaseRfc[30] = {1,2,3,4, 5,7, 9,13, 17,25, 33,49, 65,97, 129,193, 257,385, 513,769, 1025,1537, 2049,3073, 4097,6145, 8193,12289, 16385,24577};
    for (int i = 0; i < 29; ++i) {
        CHECK(kSym.lenExtra[i] == lenExtraRfc[i]);
        CHECK(kSym.lenBase[i] == lenBaseRfc[i]);
    }
    for (int j = 0; j < 30; ++j) {
        CHECK(kSym.distExtra[j] == distExtraRfc[j]);
        CHECK(kSym.distBase[j] == distBaseRfc[j]);
    }
    // the rule itself, stated independently of the constructor: extra = (index / 4) - 1 (lengths, 8 <= i < 28),
    // extra = (index / 2) - 1 (distances, 4 <= j); ranges tile with no gap: base[i+1] = base[i] + 2^extra[i]
    for (int i = 8; i < 28; ++i) CHECK(kSym.lenExtra[i] == i / 4 - 1);
    for (int j = 4; j < 30; ++j) CHECK(kSym.distExtra[j] == j / 2 - 1);
    for (int i = 0; i + 1 < 28; ++i) CHECK(kSym.lenBase[i + 1] == kSym.lenBase[i] + (1 << kSym.lenExtra[i]));
    for (int j = 0; j + 1 < 30; ++j) CHECK(kSym.distBase[j + 1] == kSym.distBase[j] + (1 << kSym.distExtra[j]));
    CHECK(kSym.lenBase[27] + (1 << kSym.lenExtra[27]) - 1 == 258);   // code 284 spans 227..258 by the rule (the RFC prints 227..257; 258 is code 285)
    CHECK(kSym.distBase[29] + (1 << kSym.distExtra[29]) - 1 == 32768);
    // the code-length order of RFC 1951 3.2.7 is a permutation of 0..18
    int seen[19] = {0};
    for (int i = 0; i < 19; ++i) ++seen[kClOrder[i]];
    for (int i = 0; i < 19; ++i) CHECK(seen[i] == 1);
    CHECK(kClOrder[0] == 16 && kClOrder[1] == 17 && kClOrder[2] == 18 && kClOrder[3] == 0 && kClOrder[18] == 15);
    // checksums against well-known values
    CHECK(Crc32("123456789") == 0xCBF43926u);
    CHECK(Crc32("") == 0u);
    CHECK(Adler32("Wikipedia") == 0x11E60398u);
    CHECK(Adler32("") == 1u);
    {   uint32_t a = 1, b = 0;   // long input, checked against the definition (sums modulo 65521)
        for (int i = 0; i < 100000; ++i) { a = (a + 255) % 65521; b = (b + a) % 65521; }
        const uint32_t want = (b << 16) | a;
        CHECK(Adler32(std::string(100000, '\xff')) == want); }
}

// ---- 2. long codes: the slow path ------------------------------------------------------------------
static void TestLongCodes() {
    using namespace inflate_detail;
    // lengths 1,2,...,14,15,15: a complete code with codes of every length up to 15
    uint8_t lens[16];
    for (int i = 0; i < 14; ++i) lens[i] = uint8_t(i + 1);
    lens[14] = 15; lens[15] = 15;
    Decoder d;
    CHECK(d.Build(lens, 16) == Decoder::Shape::Complete);
    // canonical codes by RFC 1951 3.2.2, computed here the plain way
    uint32_t code[16] = {0}; int blCount[16] = {0}; uint32_t nextCode[17] = {0};
    for (int i = 0; i < 16; ++i) ++blCount[lens[i]];
    uint32_t c = 0;
    for (int b = 1; b <= 15; ++b) { c = (c + blCount[b - 1]) << 1; nextCode[b] = c; }
    for (int i = 0; i < 16; ++i) code[i] = nextCode[lens[i]]++;
    CHECK(code[0] == 0 && lens[0] == 1 && code[1] == 2);   // 0, 10, 110, ...
    BitWriter w;
    std::vector<int> order;
    Rng rng{12345};
    for (int i = 0; i < 400; ++i) { int s = int(rng.Below(16)); order.push_back(s); w.Code(code[s], lens[s]); }
    std::string bytes = w.Finish();
    BitReader br(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    for (size_t i = 0; i < order.size(); ++i) { int s = d.Next(br); CHECK(s == order[i]); if (s != order[i]) break; }
    // an over-subscribed set and an incomplete set
    uint8_t over[3] = {1, 1, 1};
    CHECK(d.Build(over, 3) == Decoder::Shape::Oversubscribed);
    uint8_t inc[3] = {2, 2, 2};
    CHECK(d.Build(inc, 3) == Decoder::Shape::Incomplete && d.used == 3);
    // the starved case: the stream ends in the middle of a 15-bit code
    const uint8_t one[1] = {0xff};   // eight 1-bits: prefix of the longest codes, not a whole code
    d.Build(lens, 16);
    BitReader b2(one, 1);
    CHECK(d.Next(b2) == Decoder::kStarved);
}

// ---- 3. zip bomb: the cap holds and memory stays near it ------------------------------------------------
static void TestBomb() {
    std::string out, why;
    // pure C++ bomb: fixed block, 60000 maximum-length matches (about 97 KB in, about 15.5 MB out)
    std::string raw = BombRaw(60000);
    const size_t fullSize = 1 + 60000 * 258, cap = 100000;
    std::string gz = Gzip(raw, "");   // the trailer is wrong on purpose: the cap must stop it first
    std::printf("bomb: %zu bytes in, %zu bytes would come out, cap %zu\n", gz.size(), fullSize, cap);
    CHECK(gz.size() * 100 < fullSize);   // at least 100:1
    g_peak = g_live.load();
    size_t before = g_live.load();
    bool ok = InflateBody(gz, true, cap, out, why);
    size_t peak = g_peak.load() - before;
    CHECK(!ok);
    CHECK(why.find("larger than the limit") != std::string::npos);
    CHECK(out.size() <= cap);
    CHECK(out.capacity() <= cap + 4096);
    CHECK(peak <= 2 * cap + 8192);   // growing copies the old buffer into the new one, so at most old + new (both <= cap)
    std::printf("bomb: refused (%s), peak extra heap %zu bytes\n", why.c_str(), peak);
    // exactly at the cap is fine, one over is refused
    std::string plainSmall(1 + 50 * 258, 'a');
    std::string gzSmall = Gzip(BombRaw(50), plainSmall);
    CHECK(InflateBody(gzSmall, true, plainSmall.size(), out, why) && out == plainSmall);
    CHECK(!InflateBody(gzSmall, true, plainSmall.size() - 1, out, why) && why.find("larger than the limit") != std::string::npos);
    CHECK(!InflateBody(gzSmall, true, 0, out, why));
    // stored blocks over the cap are refused before anything is copied
    {   std::string stored = std::string("\x01\x10\x00\xef\xff", 5) + std::string(16, 'x');
        CHECK(!InflateBody(Gzip(stored, std::string(16, 'x')), true, 15, out, why) && why.find("larger than the limit") != std::string::npos);
        CHECK(InflateBody(Gzip(stored, std::string(16, 'x')), true, 16, out, why) && out == std::string(16, 'x')); }
    // a very large cap with a tiny input must not reserve the cap up front
    {   g_peak = g_live.load(); before = g_live.load();
        CHECK(InflateBody(gzSmall, true, size_t(1) << 40, out, why) && out == plainSmall);
        CHECK(g_peak.load() - before < (1u << 20)); }
}

// ---- 4. thread safety of the checksums (the table is built at compile time) -----------------------------
static void TestThreads() {
    std::string data(50000, 'q');
    for (size_t i = 0; i < data.size(); ++i) data[i] = char(i * 31 + 7);
    const uint32_t want = inflate_detail::Crc32(data);
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back([&] { for (int i = 0; i < 50; ++i) if (inflate_detail::Crc32(data) != want) ++bad; });
    for (auto& t : ts) t.join();
    CHECK(bad == 0);
    // the fixed-table decoder is built on first use: start four threads on a fresh fixed-block stream
    std::string plain(1 + 20 * 258, 'a'), gz = Gzip(BombRaw(20), plain);
    std::vector<std::thread> us;
    for (int t = 0; t < 4; ++t) us.emplace_back([&] { std::string o, w; for (int i = 0; i < 50; ++i) if (!InflateBody(gz, true, 1 << 20, o, w) || o != plain) ++bad; });
    for (auto& t : us) t.join();
    CHECK(bad == 0);
}

// ---- 5. differential against Python's zlib ---------------------------------------------------------------
static const char* kPyCorpus = R"PY(
import sys, os, zlib, random
d = sys.argv[1]
rnd = random.Random(20261114)
def data(kind, n):
    if n == 0: return b''
    if kind == 'zeros': return bytes(n)
    if kind == 'random': return bytes(rnd.getrandbits(8) for _ in range(n))
    if kind == 'text':
        words = [b'the', b'quick', b'brown', b'fox', b'jumps', b'over', b'lazy', b'dog', b'engine', b'okay', b'space', b'\n']
        out = bytearray()
        while len(out) < n: out += rnd.choice(words) + b' '
        return bytes(out[:n])
    if kind == 'runs':
        out = bytearray()
        while len(out) < n: out += bytes([rnd.getrandbits(8)]) * rnd.randint(1, 600)
        return bytes(out[:n])
    if kind == 'skew':   # geometric byte frequencies: many different code lengths, long ones too
        return bytes(min(255, int(rnd.expovariate(0.12))) for _ in range(n))
    raise SystemExit('kind')
def comp(raw, wrapper, level, strategy, wbits, mem, chunked):
    w = {'zlib': wbits, 'gzip': 16 + wbits}[wrapper]
    c = zlib.compressobj(level, zlib.DEFLATED, w, mem, strategy)
    if not chunked: return c.compress(raw) + c.flush()
    out = b''; step = max(1, len(raw) // 5)
    for i in range(0, len(raw), step):
        out += c.compress(raw[i:i + step]) + c.flush(zlib.Z_SYNC_FLUSH if (i // step) % 2 else zlib.Z_FULL_FLUSH)
    return out + c.flush()
sizes = [0, 1, 2, 3, 7, 100, 1000, 4095, 4096, 4097, 65535, 65536, 65537, 120000, 300000]
kinds = ['zeros', 'random', 'text', 'runs', 'skew']
strategies = [zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE, zlib.Z_FIXED]
count = 0
def emit(raw, wrapper, blob):
    global count
    open('%s/%d.raw' % (d, count), 'wb').write(raw)
    open('%s/%d.%s' % (d, count, 'gz' if wrapper == 'gzip' else 'zz'), 'wb').write(blob)
    count += 1
for n in sizes:
    for kind in kinds:
        raw = data(kind, n)
        for wrapper in ('zlib', 'gzip'):
            for level in range(10):
                emit(raw, wrapper, comp(raw, wrapper, level, zlib.Z_DEFAULT_STRATEGY, 15, 8, False))
            for st in strategies[1:]:
                emit(raw, wrapper, comp(raw, wrapper, 6, st, 15, 8, False))
            emit(raw, wrapper, comp(raw, wrapper, 9, zlib.Z_DEFAULT_STRATEGY, 9, 1, False))
            emit(raw, wrapper, comp(raw, wrapper, 5, zlib.Z_DEFAULT_STRATEGY, 15, 9, True))
print(count)
)PY";

static bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

static void TestDifferential() {
    const bool require = std::getenv("OKAY_REQUIRE_DIFFERENTIAL") && std::string(std::getenv("OKAY_REQUIRE_DIFFERENTIAL")) == "1";
#if defined(_WIN32)
    std::printf("SKIP differential test: POSIX only\n"); CHECK(!require);
    return;
#else
    char tmpl[] = "/tmp/okay_inflate_diff_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("SKIP differential test: no temp directory\n"); CHECK(!require); return; }
    const std::string dir = tmpl;
    {   std::ofstream f(dir + "/gen.py"); f << kPyCorpus; }
    // the generator is run from a different directory than the data, with -I so nothing is imported from the cwd
    std::string cmd = "python3 -I " + dir + "/gen.py " + dir + " > " + dir + "/count.txt 2>" + dir + "/err.txt";
    int rc = std::system(cmd.c_str());
    std::string countText;
    if (rc != 0 || !ReadFile(dir + "/count.txt", countText) || countText.empty()) {
        std::printf("SKIP differential test: python3 with zlib is not available (rc=%d)\n", rc);
        CHECK(!require);
        (void)!std::system(("rm -rf " + dir).c_str());
        return;
    }
    const int count = std::atoi(countText.c_str());
    int match = 0, capOk = 0, prefixRefused = 0, mutRejected = 0, mutAccepted = 0;
    Rng rng{777};
    std::string out, why;
    for (int i = 0; i < count; ++i) {
        std::string raw, blob; bool gzip = true;
        if (!ReadFile(dir + "/" + std::to_string(i) + ".raw", raw)) { CHECK(false); continue; }
        if (!ReadFile(dir + "/" + std::to_string(i) + ".gz", blob)) { gzip = false; CHECK(ReadFile(dir + "/" + std::to_string(i) + ".zz", blob)); }
        bool ok = InflateBody(blob, gzip, raw.size(), out, why);
        CHECK(ok);
        if (!ok) { std::printf("  case %d (%s, %zu bytes) failed: %s\n", i, gzip ? "gzip" : "zlib", raw.size(), why.c_str()); continue; }
        CHECK(out == raw);
        if (out == raw) ++match;
        // the cap: one byte short is refused, with no output over the cap
        if (!raw.empty()) {
            bool r = InflateBody(blob, gzip, raw.size() - 1, out, why);
            CHECK(!r && out.size() <= raw.size() - 1);
            if (!r) ++capOk;
        }
        // small streams: every strict prefix refused
        if (blob.size() <= 400) {
            for (size_t k = 0; k < blob.size(); ++k) { bool r = InflateBody(blob.substr(0, k), gzip, raw.size() + 10, out, why); CHECK(!r); if (!r) ++prefixRefused; }
        }
        // a handful of damaged copies: refused, or the same bytes back (a flip in a header field the format ignores)
        for (int m = 0; m < 3 && !blob.empty(); ++m) {
            std::string bad = blob; bad[rng.Below(bad.size())] ^= char(1 << rng.Below(8));
            if (InflateBody(bad, gzip, raw.size() + 10, out, why)) { ++mutAccepted; CHECK(out == raw); } else ++mutRejected;
        }
    }
    CHECK(count >= 1500);
    CHECK(match == count);
    CHECK(mutRejected > mutAccepted * 5);
    std::printf("differential vs Python zlib: %d streams, %d decoded identically, %d cap refusals, %d prefixes refused, "
                "%d damaged copies rejected, %d accepted\n", count, match, capOk, prefixRefused, mutRejected, mutAccepted);
    // zip bomb from python: 120,000,000 zero bytes in a few hundred KB, cap 100,000 (deflate cannot go beyond about 1000:1)
    std::string pyBomb = "python3 -I -c \"import zlib,sys;sys.stdout.buffer.write(zlib.compress(bytes(120000000),9))\" > " + dir + "/bomb.zz 2>/dev/null";
    if (std::system(pyBomb.c_str()) == 0) {
        std::string bomb;
        if (ReadFile(dir + "/bomb.zz", bomb) && bomb.size() > 100) {
            g_peak = g_live.load();
            size_t before = g_live.load();
            bool ok = InflateBody(bomb, false, 100000, out, why);
            size_t peak = g_peak.load() - before;
            CHECK(!ok && why.find("larger than the limit") != std::string::npos);
            CHECK(out.size() <= 100000 && out.capacity() <= 100000 + 4096);
            CHECK(peak <= 2 * 100000 + 8192);
            std::printf("python bomb: %zu bytes in, 120000000 would come out, cap 100000: refused, peak extra heap %zu bytes\n", bomb.size(), peak);
            // and with room it decodes (the cap is the only thing in the way)
            CHECK(InflateBody(bomb, false, 120000000, out, why) && out.size() == 120000000);
        } else CHECK(false);
    }
    (void)!std::system(("rm -rf " + dir).c_str());
#endif
}

// ---- 6. fuzz around valid streams of every block kind --------------------------------------------------
static void TestFuzz(int iters) {
    Rng rng{0xC0FFEEull};
    std::vector<std::string> seeds;
    seeds.push_back(Gzip(BombRaw(30), std::string(1 + 30 * 258, 'a')));
    seeds.push_back(Gzip(std::string("\x01\x05\x00\xfa\xff", 5) + "hello", "hello"));
    size_t acc = 0, rej = 0;
    std::string out, why;
    for (int i = 0; i < iters; ++i) {
        std::string m;
        if (i % 3 == 2) {
            m = std::string("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
            for (size_t k = 0, c = 2 + rng.Below(200); k < c; ++k) m.push_back(char(rng.Next()));
            m[10] = char((rng.Next() & 0xf8) | 4 | (rng.Next() & 1));
        } else {
            m = seeds[i % 2];
            for (size_t k = 0, c = 1 + rng.Below(5); k < c; ++k) m[rng.Below(m.size())] = char(rng.Next());
            if (rng.Below(5) == 0) m.resize(rng.Below(m.size() + 1));
        }
        if (InflateBody(m, true, 8192, out, why)) ++acc; else ++rej;
        CHECK(out.size() <= 8192);
    }
    CHECK(rej > 0);
    std::printf("fuzz: %d inputs, %zu accepted, %zu rejected\n", iters, acc, rej);
    // strict prefixes of the seeds
    for (const auto& s : seeds)
        for (size_t k = 0; k < s.size(); ++k) CHECK(!InflateBody(s.substr(0, k), true, 1 << 20, out, why));
    // the unmutated seeds are accepted
    CHECK(InflateBody(seeds[0], true, 1 << 20, out, why) && out.size() == 1 + 30 * 258);
    CHECK(InflateBody(seeds[1], true, 1 << 20, out, why) && out == "hello");
}

int main(int argc, char** argv) {
    int iters = 20000;
    if (const char* e = std::getenv("OKAY_FUZZ_ITERS")) iters = std::atoi(e);
    if (argc > 1) iters = std::atoi(argv[1]);
    TestTables(); TestLongCodes(); TestBomb(); TestThreads(); TestDifferential(); TestFuzz(iters);
    std::printf("own_http_inflate: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
