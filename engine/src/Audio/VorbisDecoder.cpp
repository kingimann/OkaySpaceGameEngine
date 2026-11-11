// Ogg Vorbis I decoder, written from the Vorbis I specification (xiph.org):
// Ogg framing (pages, CRC, packets), the three header packets, codebooks,
// floor types 0 and 1, residue types 0/1/2, channel coupling, windows with block size
// switching, the inverse MDCT and overlap-add. See VorbisDecoder.hpp.
#include "okay/Audio/VorbisDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace okay {

namespace {

constexpr int kMaxChannels = 32;
constexpr std::uint32_t kMaxBookEntries = 1u << 20;     // per codebook
constexpr std::uint32_t kMaxTotalEntries = 1u << 21;    // over the whole setup header
constexpr int kFastBits = 10;                           // Huffman lookup table width
constexpr double kPi = 3.14159265358979323846;

int Ilog(std::uint32_t x) {
    int n = 0;
    while (x) { ++n; x >>= 1; }
    return n;
}

// ---- Ogg framing -----------------------------------------------------------------------------

const std::uint32_t* CrcTable() {
    static const std::uint32_t* table = [] {
        static std::uint32_t t[256];
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
            t[i] = r;
        }
        return t;
    }();
    return table;
}

std::uint32_t PageCrc(const unsigned char* p, std::size_t n) {
    const std::uint32_t* t = CrcTable();
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char b = (i >= 22 && i < 26) ? 0 : p[i];   // the CRC field counts as zero
        crc = (crc << 8) ^ t[((crc >> 24) ^ b) & 0xFF];
    }
    return crc;
}

std::uint32_t Le32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

struct Page {
    std::size_t lacingOff = 0;     // offset of the segment table
    std::size_t dataOff = 0;       // offset of the page body
    std::uint32_t serial = 0;
    std::uint32_t seq = 0;
    std::int64_t granule = 0;
    unsigned char flags = 0;       // 1 continued packet, 2 first page (BOS), 4 last page (EOS)
    unsigned char nseg = 0;
};

// Finds every page that is intact (right capture pattern, version and CRC),
// resynchronising on the next "OggS" after damage. The CRC work is budgeted so
// that input made of fake page headers cannot cost much more than its size.
void ScanPages(const unsigned char* d, std::size_t size, std::vector<Page>& pages) {
    std::size_t budget = size * 2 + (1u << 20);
    std::size_t pos = 0;
    while (pos + 27 <= size) {
        const void* hit = std::memchr(d + pos, 'O', size - pos - 26);
        if (!hit) break;
        pos = static_cast<std::size_t>(static_cast<const unsigned char*>(hit) - d);
        if (pos + 27 > size) break;
        if (std::memcmp(d + pos, "OggS", 4) == 0 && d[pos + 4] == 0) {
            const unsigned nseg = d[pos + 26];
            const std::size_t hdr = 27 + nseg;
            if (pos + hdr <= size) {
                std::size_t body = 0;
                for (unsigned i = 0; i < nseg; ++i) body += d[pos + 27 + i];
                const std::size_t total = hdr + body;
                if (pos + total <= size) {
                    if (total > budget) return;
                    budget -= total;
                    if (PageCrc(d + pos, total) == Le32(d + pos + 22)) {
                        Page p;
                        p.lacingOff = pos + 27;
                        p.dataOff = pos + hdr;
                        p.flags = d[pos + 5];
                        std::uint64_t g = 0;
                        for (int i = 7; i >= 0; --i) g = (g << 8) | d[pos + 6 + i];
                        p.granule = static_cast<std::int64_t>(g);
                        p.serial = Le32(d + pos + 14);
                        p.seq = Le32(d + pos + 18);
                        p.nseg = static_cast<unsigned char>(nseg);
                        pages.push_back(p);
                        pos += total;
                        continue;
                    }
                }
            }
        }
        ++pos;
    }
}

struct PacketRef { std::uint32_t off, len; };

// The packets of one logical stream, copied into one arena.
struct PacketList {
    std::vector<unsigned char> arena;
    std::vector<PacketRef> packets;
    std::int64_t eosGranule = -1;
    bool sawEos = false;
};

// Collects the packets of the logical stream that starts at page `start`.
// Lost pages (sequence gaps) drop the packet they interrupt. Returns the index
// of the page after the stream's last page.
std::size_t CollectPackets(const unsigned char* d, const std::vector<Page>& pages, std::size_t start, PacketList& pl) {
    const std::uint32_t serial = pages[start].serial;
    bool partial = false;
    std::size_t partialStart = 0;
    bool haveSeq = false;
    std::uint32_t lastSeq = 0;
    for (std::size_t i = start; i < pages.size(); ++i) {
        const Page& p = pages[i];
        if (p.serial != serial) continue;
        if (i != start && (p.flags & 2)) continue;            // a second "first page" of the same stream: ignore
        const bool cont = (p.flags & 1) != 0;
        if (haveSeq && p.seq != lastSeq + 1u && partial) { pl.arena.resize(partialStart); partial = false; }
        if (!cont && partial) { pl.arena.resize(partialStart); partial = false; }
        bool skipping = cont && !partial;                      // the start of this packet is lost
        std::size_t pos = p.dataOff;
        for (unsigned s = 0; s < p.nseg; ++s) {
            const unsigned l = d[p.lacingOff + s];
            if (skipping) {
                pos += l;
                if (l < 255) skipping = false;
                continue;
            }
            if (!partial) { partialStart = pl.arena.size(); partial = true; }
            pl.arena.insert(pl.arena.end(), d + pos, d + pos + l);
            pos += l;
            if (l < 255) {
                pl.packets.push_back({static_cast<std::uint32_t>(partialStart),
                                      static_cast<std::uint32_t>(pl.arena.size() - partialStart)});
                partial = false;
            }
        }
        haveSeq = true;
        lastSeq = p.seq;
        if (p.flags & 4) {
            pl.eosGranule = p.granule;
            pl.sawEos = true;
            return i + 1;
        }
    }
    if (partial) pl.arena.resize(partialStart);
    return pages.size();
}

// ---- bit reader (Vorbis packs bits starting at the least significant bit of each byte) --------

struct BitReader {
    const unsigned char* d;
    std::size_t nbits;
    std::size_t pos = 0;
    bool eop = false;

    BitReader(const unsigned char* data, std::size_t bytes) : d(data), nbits(bytes * 8) {}

    std::size_t Avail() const { return nbits - pos; }

    // Up to 32 bits at the cursor, zero padded past the end; does not move.
    std::uint32_t Peek(int n) const {
        std::uint64_t acc = 0;
        const std::size_t b = pos >> 3;
        const std::size_t have = nbits / 8;
        for (std::size_t i = 0; i < 5 && b + i < have; ++i) acc |= std::uint64_t(d[b + i]) << (8 * i);
        acc >>= (pos & 7);
        return static_cast<std::uint32_t>(acc & ((std::uint64_t(1) << n) - 1));
    }

    std::uint32_t Read(int n) {
        if (n == 0) return 0;
        if (static_cast<std::size_t>(n) > Avail()) { eop = true; pos = nbits; return 0; }
        const std::uint32_t v = Peek(n);
        pos += n;
        return v;
    }

    bool Skip(int n) {
        if (static_cast<std::size_t>(n) > Avail()) { eop = true; pos = nbits; return false; }
        pos += n;
        return true;
    }
};

// ---- setup header structures -----------------------------------------------------------------

struct Codebook {
    int dims = 0;
    std::uint32_t entries = 0;
    int lookupType = 0;
    float minimum = 0, delta = 0;
    bool sequence = false;
    std::uint32_t lookupValues = 0;
    std::vector<std::uint32_t> mult;
    // Huffman decoding
    std::vector<std::uint32_t> fast;         // (entry << 5) | length, 0 when the code is longer or unused
    struct Code { std::uint32_t aligned; std::uint32_t entry; int len; };
    std::vector<Code> slow;                  // codes longer than kFastBits, sorted by aligned code
    bool single = false;
    std::uint32_t singleEntry = 0;
    int singleLen = 0;
    bool usable = false;                     // at least one used entry
};

// double -> float without overflow (a hostile stream can name values beyond float range).
float ToFloat(double d) {
    if (!(d == d)) return 0.0f;
    if (d > 3.0e38) return 3.0e38f;
    if (d < -3.0e38) return -3.0e38f;
    return static_cast<float>(d);
}

float UnpackFloat(std::uint32_t x) {
    const std::int64_t mant = static_cast<std::int64_t>(x & 0x1FFFFF);
    const int exp = static_cast<int>((x & 0x7FE00000u) >> 21) - 788;
    const double m = (x & 0x80000000u) ? -static_cast<double>(mant) : static_cast<double>(mant);
    return ToFloat(std::ldexp(m, exp));
}

std::uint32_t Lookup1Values(std::uint32_t entries, int dims) {
    auto fits = [&](std::uint64_t v) {          // v^dims <= entries
        std::uint64_t acc = 1;
        for (int i = 0; i < dims; ++i) {
            acc *= v;
            if (acc > entries) return false;
            if (v <= 1) break;
        }
        return acc <= entries;
    };
    std::uint64_t v = static_cast<std::uint64_t>(std::pow(static_cast<double>(entries), 1.0 / dims));
    while (v > 0 && !fits(v)) --v;
    while (fits(v + 1)) ++v;
    return static_cast<std::uint32_t>(v);
}

// Assigns codewords by the rule of the specification: entries in order, each
// taking the lowest-valued free codeword of its length. `aligned` receives the
// codeword left-aligned in 32 bits. Fails on over- or underspecified trees
// (a lone used entry may be underspecified).
bool AssignCodes(const std::vector<std::uint8_t>& len, std::vector<std::uint32_t>& aligned, std::uint32_t& used) {
    struct Block { std::uint64_t start; int len; };
    std::vector<Block> freeBlocks;
    freeBlocks.push_back({0, 0});
    aligned.assign(len.size(), 0);
    used = 0;
    for (std::size_t i = 0; i < len.size(); ++i) {
        const int L = len[i];
        if (L == 0) continue;
        ++used;
        std::size_t best = freeBlocks.size();
        for (std::size_t k = 0; k < freeBlocks.size(); ++k)
            if (freeBlocks[k].len <= L && (best == freeBlocks.size() || freeBlocks[k].start < freeBlocks[best].start)) best = k;
        if (best == freeBlocks.size()) return false;          // overspecified
        const Block b = freeBlocks[best];
        freeBlocks[best] = freeBlocks.back();
        freeBlocks.pop_back();
        aligned[i] = static_cast<std::uint32_t>(b.start);
        for (int k = b.len + 1; k <= L; ++k) freeBlocks.push_back({b.start + (std::uint64_t(1) << (32 - k)), k});
        if (freeBlocks.size() > 256) return false;            // cannot happen with a valid tree
    }
    if (used > 1 && !freeBlocks.empty()) return false;        // underspecified
    return true;
}

std::uint32_t ReverseBits(std::uint32_t v, int n) {
    std::uint32_t r = 0;
    for (int i = 0; i < n; ++i) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

bool BuildHuffman(Codebook& b, const std::vector<std::uint8_t>& len) {
    std::vector<std::uint32_t> aligned;
    std::uint32_t used = 0;
    if (!AssignCodes(len, aligned, used)) return false;
    b.usable = used > 0;
    b.fast.assign(std::size_t(1) << kFastBits, 0);
    b.slow.clear();
    if (used == 1) {
        for (std::size_t i = 0; i < len.size(); ++i)
            if (len[i]) { b.single = true; b.singleEntry = static_cast<std::uint32_t>(i); b.singleLen = len[i]; }
        return true;
    }
    for (std::size_t i = 0; i < len.size(); ++i) {
        const int L = len[i];
        if (!L) continue;
        if (L <= kFastBits) {
            const std::uint32_t code = aligned[i] >> (32 - L);
            const std::uint32_t rev = ReverseBits(code, L);
            for (std::uint32_t hi = 0; hi < (1u << (kFastBits - L)); ++hi)
                b.fast[rev | (hi << L)] = (static_cast<std::uint32_t>(i) << 5) | static_cast<std::uint32_t>(L);
        } else {
            b.slow.push_back({aligned[i], static_cast<std::uint32_t>(i), L});
        }
    }
    std::sort(b.slow.begin(), b.slow.end(), [](const Codebook::Code& x, const Codebook::Code& y) { return x.aligned < y.aligned; });
    return true;
}

// Reads one codeword; returns the entry number or -1 when the packet ends
// (or the codebook has no usable code).
int DecodeScalar(const Codebook& b, BitReader& br) {
    if (b.single) return br.Skip(b.singleLen) ? static_cast<int>(b.singleEntry) : -1;
    if (!b.usable) return -1;
    const std::uint32_t f = b.fast[br.Peek(kFastBits)];
    if (f) {
        const int L = static_cast<int>(f & 31);
        if (static_cast<std::size_t>(L) > br.Avail()) { br.eop = true; br.pos = br.nbits; return -1; }
        br.pos += L;
        return static_cast<int>(f >> 5);
    }
    if (b.slow.empty()) {                  // an unused bit pattern
        br.Skip(1);
        return -1;
    }
    const std::size_t start = br.pos;
    std::uint64_t code = 0;
    for (int L = 1; L <= 32; ++L) {
        if (br.Avail() == 0) { br.eop = true; br.pos = br.nbits; return -1; }
        code = (code << 1) | br.Read(1);
        if (L <= kFastBits) continue;
        const std::uint32_t target = static_cast<std::uint32_t>(code << (32 - L));
        std::size_t lo = 0, hi = b.slow.size();
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (b.slow[mid].aligned <= target) lo = mid + 1; else hi = mid;
        }
        if (lo > 0 && b.slow[lo - 1].aligned == target && b.slow[lo - 1].len == L) return static_cast<int>(b.slow[lo - 1].entry);
    }
    br.pos = start;
    br.Skip(1);
    return -1;
}

// The vector a codeword stands for, `dims` floats. Each value is evaluated in
// double precision and rounded once to float (as the reference decoder does);
// that keeps exactly cancelling sums, and the sign decisions of channel
// coupling that depend on them, identical to it. Only the first `count` values
// (at most `dims`) are computed.
void VqValues(const Codebook& b, std::uint32_t entry, float* out, int count) {
    float last = 0.0f;
    if (b.lookupType == 1) {
        std::uint64_t div = 1;
        for (int i = 0; i < count; ++i) {
            const std::uint32_t idx = static_cast<std::uint32_t>((entry / div) % b.lookupValues);
            const float v = ToFloat(static_cast<double>(b.mult[idx]) * static_cast<double>(b.delta) + static_cast<double>(b.minimum) + static_cast<double>(last));
            out[i] = v;
            if (b.sequence) last = v;
            div *= b.lookupValues;
        }
    } else {
        const std::size_t base = static_cast<std::size_t>(entry) * b.dims;
        for (int i = 0; i < count; ++i) {
            const float v = ToFloat(static_cast<double>(b.mult[base + i]) * static_cast<double>(b.delta) + static_cast<double>(b.minimum) + static_cast<double>(last));
            out[i] = v;
            if (b.sequence) last = v;
        }
    }
}

struct Floor1 {
    int partitions = 0;
    int partClass[31] = {};
    int classDim[16] = {};
    int classSub[16] = {};
    int classMaster[16] = {};
    int subBook[16][8] = {};
    int mult = 1;
    int rangeBits = 0;
    int count = 0;                         // number of X values
    int x[260] = {};
    int sorted[260] = {};                  // indices by ascending X
    int lowN[260] = {};
    int highN[260] = {};
};

struct Floor0 {
    int order = 0;
    int rate = 0;
    int barkMapSize = 0;
    int ampBits = 0;
    int ampOffset = 0;
    int numBooks = 0;
    int books[16] = {};
    std::vector<int> map[2];               // Bark scale position of each coefficient, per block size (built by the decoder)
};

// One floor of the setup header (type 0 or 1).
struct Floor {
    int type = 1;
    Floor1 f1;
    Floor0 f0;
};

struct Residue {
    int type = 0;
    std::uint32_t begin = 0, end = 0, partSize = 1;
    int classes = 1;
    int classbook = 0;
    int stages = 0;                         // passes up to the last one that has a codebook
    int books[64][8];
};

struct Mapping {
    int submaps = 1;
    int couplingSteps = 0;
    int mag[256] = {};
    int ang[256] = {};
    int mux[kMaxChannels] = {};
    int submapFloor[16] = {};
    int submapResidue[16] = {};
};

struct Mode { int blockflag = 0; int mapping = 0; };

struct Setup {
    std::vector<Codebook> books;
    std::vector<Floor> floors;
    std::vector<Residue> residues;
    std::vector<Mapping> mappings;
    std::vector<Mode> modes;
};

struct Ident {
    int channels = 0;
    int rate = 0;
    int blocksize[2] = {0, 0};
};

bool IsHeader(const unsigned char* p, std::size_t n, int type) {
    return n >= 7 && p[0] == static_cast<unsigned char>(type) && std::memcmp(p + 1, "vorbis", 6) == 0;
}

bool ParseIdent(const unsigned char* p, std::size_t n, Ident& id, std::string& err) {
    if (!IsHeader(p, n, 1)) { err = "missing Vorbis identification header"; return false; }
    BitReader br(p + 7, n - 7);
    if (br.Read(32) != 0) { err = "unsupported Vorbis version"; return false; }
    const int channels = static_cast<int>(br.Read(8));
    const std::uint32_t rate = br.Read(32);
    br.Read(32); br.Read(32); br.Read(32);              // bitrate maximum, nominal, minimum
    const int b0 = static_cast<int>(br.Read(4));
    const int b1 = static_cast<int>(br.Read(4));
    const int framing = static_cast<int>(br.Read(1));
    if (br.eop || !framing) { err = "damaged Vorbis identification header"; return false; }
    if (channels < 1 || rate < 1 || rate > 0x7FFFFFFFu) { err = "invalid channel count or sample rate"; return false; }
    if (channels > kMaxChannels) { err = "more than 32 channels are not supported"; return false; }
    if (b0 < 6 || b0 > 13 || b1 < 6 || b1 > 13 || b0 > b1) { err = "invalid Vorbis block sizes"; return false; }
    id.channels = channels;
    id.rate = static_cast<int>(rate);
    id.blocksize[0] = 1 << b0;
    id.blocksize[1] = 1 << b1;
    return true;
}

bool ParseCodebook(BitReader& br, Codebook& b, std::uint32_t& totalEntries, std::string& err) {
    if (br.Read(24) != 0x564342u) { err = "bad codebook sync"; return false; }
    b.dims = static_cast<int>(br.Read(16));
    b.entries = br.Read(24);
    const bool ordered = br.Read(1) != 0;
    if (br.eop || b.dims < 1) { err = "bad codebook header"; return false; }
    if (b.entries > kMaxBookEntries || totalEntries + b.entries > kMaxTotalEntries) { err = "codebook too large"; return false; }
    totalEntries += b.entries;
    std::vector<std::uint8_t> len(b.entries, 0);
    if (!ordered) {
        const bool sparse = br.Read(1) != 0;
        for (std::uint32_t i = 0; i < b.entries; ++i) {
            if (sparse && !br.Read(1)) { if (br.eop) break; continue; }
            len[i] = static_cast<std::uint8_t>(br.Read(5) + 1);
            if (br.eop) break;
        }
    } else {
        std::uint32_t cur = 0;
        int curLen = static_cast<int>(br.Read(5)) + 1;
        while (cur < b.entries && !br.eop) {
            const std::uint32_t number = br.Read(Ilog(b.entries - cur));
            if (number > b.entries - cur || curLen > 32) { err = "bad ordered codebook"; return false; }
            for (std::uint32_t k = 0; k < number; ++k) len[cur + k] = static_cast<std::uint8_t>(curLen);
            cur += number;
            ++curLen;
        }
    }
    if (br.eop) { err = "codebook cut short"; return false; }
    b.lookupType = static_cast<int>(br.Read(4));
    if (b.lookupType > 2) { err = "bad codebook lookup type"; return false; }
    if (b.lookupType != 0) {
        b.minimum = UnpackFloat(br.Read(32));
        b.delta = UnpackFloat(br.Read(32));
        const int valueBits = static_cast<int>(br.Read(4)) + 1;
        b.sequence = br.Read(1) != 0;
        std::uint64_t values = b.lookupType == 1 ? Lookup1Values(b.entries, b.dims)
                                                 : static_cast<std::uint64_t>(b.entries) * static_cast<std::uint64_t>(b.dims);
        if (br.eop || values * static_cast<std::uint64_t>(valueBits) > br.Avail()) { err = "codebook lookup cut short"; return false; }
        b.lookupValues = static_cast<std::uint32_t>(values);
        b.mult.resize(static_cast<std::size_t>(values));
        for (auto& m : b.mult) m = br.Read(valueBits);
        if (b.lookupType == 1 && b.lookupValues == 0 && b.entries > 0) { err = "bad codebook lookup"; return false; }
    }
    if (!BuildHuffman(b, len)) { err = "invalid Huffman tree"; return false; }
    return true;
}

bool ParseFloor1(BitReader& br, Floor1& f, std::size_t nbooks, const std::vector<Codebook>& books, std::string& err) {
    f.partitions = static_cast<int>(br.Read(5));
    int maxClass = -1;
    for (int i = 0; i < f.partitions; ++i) {
        f.partClass[i] = static_cast<int>(br.Read(4));
        maxClass = std::max(maxClass, f.partClass[i]);
    }
    for (int c = 0; c <= maxClass; ++c) {
        f.classDim[c] = static_cast<int>(br.Read(3)) + 1;
        f.classSub[c] = static_cast<int>(br.Read(2));
        f.classMaster[c] = -1;
        if (f.classSub[c]) {
            f.classMaster[c] = static_cast<int>(br.Read(8));
            if (static_cast<std::size_t>(f.classMaster[c]) >= nbooks) { err = "floor refers to a missing codebook"; return false; }
        }
        for (int j = 0; j < (1 << f.classSub[c]); ++j) {
            f.subBook[c][j] = static_cast<int>(br.Read(8)) - 1;
            if (f.subBook[c][j] >= static_cast<int>(nbooks)) { err = "floor refers to a missing codebook"; return false; }
        }
    }
    f.mult = static_cast<int>(br.Read(2)) + 1;
    f.rangeBits = static_cast<int>(br.Read(4));
    f.x[0] = 0;
    f.x[1] = 1 << f.rangeBits;
    f.count = 2;
    for (int i = 0; i < f.partitions; ++i) {
        for (int j = 0; j < f.classDim[f.partClass[i]]; ++j) {
            if (f.count >= 258) { err = "too many floor points"; return false; }
            f.x[f.count++] = static_cast<int>(br.Read(f.rangeBits));
        }
    }
    if (br.eop) { err = "floor cut short"; return false; }
    (void)books;
    for (int i = 0; i < f.count; ++i)
        for (int j = i + 1; j < f.count; ++j)
            if (f.x[i] == f.x[j]) { err = "duplicate floor point"; return false; }
    for (int i = 0; i < f.count; ++i) f.sorted[i] = i;
    std::sort(f.sorted, f.sorted + f.count, [&](int a, int b) { return f.x[a] < f.x[b]; });
    for (int i = 2; i < f.count; ++i) {
        int lo = 0, hi = 1;
        for (int j = 0; j < i; ++j) {
            if (f.x[j] < f.x[i] && f.x[j] > f.x[lo]) lo = j;
            if (f.x[j] > f.x[i] && f.x[j] < f.x[hi]) hi = j;
        }
        f.lowN[i] = lo;
        f.highN[i] = hi;
    }
    return true;
}

bool ParseFloor0(BitReader& br, Floor0& f, const std::vector<Codebook>& books, std::string& err) {
    f.order = static_cast<int>(br.Read(8));
    f.rate = static_cast<int>(br.Read(16));
    f.barkMapSize = static_cast<int>(br.Read(16));
    f.ampBits = static_cast<int>(br.Read(6));
    f.ampOffset = static_cast<int>(br.Read(8));
    f.numBooks = static_cast<int>(br.Read(4)) + 1;
    for (int i = 0; i < f.numBooks; ++i) {
        f.books[i] = static_cast<int>(br.Read(8));
        if (static_cast<std::size_t>(f.books[i]) >= books.size() || books[static_cast<std::size_t>(f.books[i])].lookupType == 0) {
            err = "floor refers to a bad codebook";
            return false;
        }
    }
    if (br.eop || f.order < 1 || f.rate < 1 || f.barkMapSize < 1) { err = "bad floor 0 header"; return false; }
    return true;
}

bool ParseSetup(const unsigned char* p, std::size_t n, const Ident& id, Setup& s, std::string& err) {
    if (!IsHeader(p, n, 5)) { err = "missing Vorbis setup header"; return false; }
    BitReader br(p + 7, n - 7);
    const int nbooks = static_cast<int>(br.Read(8)) + 1;
    s.books.resize(static_cast<std::size_t>(nbooks));
    std::uint32_t totalEntries = 0;
    for (int i = 0; i < nbooks; ++i)
        if (!ParseCodebook(br, s.books[static_cast<std::size_t>(i)], totalEntries, err)) return false;

    const int ntime = static_cast<int>(br.Read(6)) + 1;
    for (int i = 0; i < ntime; ++i)
        if (br.Read(16) != 0) { err = "bad time domain transform"; return false; }

    const int nfloors = static_cast<int>(br.Read(6)) + 1;
    s.floors.resize(static_cast<std::size_t>(nfloors));
    for (int i = 0; i < nfloors; ++i) {
        Floor& fl = s.floors[static_cast<std::size_t>(i)];
        fl.type = static_cast<int>(br.Read(16));
        if (fl.type == 0) { if (!ParseFloor0(br, fl.f0, s.books, err)) return false; }
        else if (fl.type == 1) { if (!ParseFloor1(br, fl.f1, static_cast<std::size_t>(nbooks), s.books, err)) return false; }
        else { err = "bad floor type"; return false; }
    }

    const int nres = static_cast<int>(br.Read(6)) + 1;
    s.residues.resize(static_cast<std::size_t>(nres));
    for (int i = 0; i < nres; ++i) {
        Residue& r = s.residues[static_cast<std::size_t>(i)];
        r.type = static_cast<int>(br.Read(16));
        if (r.type > 2) { err = "bad residue type"; return false; }
        r.begin = br.Read(24);
        r.end = br.Read(24);
        r.partSize = br.Read(24) + 1;
        r.classes = static_cast<int>(br.Read(6)) + 1;
        r.classbook = static_cast<int>(br.Read(8));
        if (br.eop || r.classbook >= nbooks) { err = "bad residue header"; return false; }
        int cascade[64];
        for (int c = 0; c < r.classes; ++c) {
            const int low = static_cast<int>(br.Read(3));
            const int high = br.Read(1) ? static_cast<int>(br.Read(5)) : 0;
            cascade[c] = high * 8 + low;
        }
        for (int c = 0; c < r.classes; ++c)
            for (int j = 0; j < 8; ++j) {
                r.books[c][j] = -1;
                if (cascade[c] & (1 << j)) {
                    const int b = static_cast<int>(br.Read(8));
                    if (b >= nbooks || s.books[static_cast<std::size_t>(b)].lookupType == 0) { err = "residue refers to a bad codebook"; return false; }
                    r.books[c][j] = b;
                }
            }
        if (br.eop) { err = "residue cut short"; return false; }
        for (int c = 0; c < r.classes; ++c)
            for (int j = 0; j < 8; ++j)
                if (r.books[c][j] >= 0) r.stages = std::max(r.stages, j + 1);
    }

    const int nmaps = static_cast<int>(br.Read(6)) + 1;
    s.mappings.resize(static_cast<std::size_t>(nmaps));
    for (int i = 0; i < nmaps; ++i) {
        Mapping& m = s.mappings[static_cast<std::size_t>(i)];
        if (br.Read(16) != 0) { err = "bad mapping type"; return false; }
        m.submaps = br.Read(1) ? static_cast<int>(br.Read(4)) + 1 : 1;
        if (br.Read(1)) {
            m.couplingSteps = static_cast<int>(br.Read(8)) + 1;
            for (int k = 0; k < m.couplingSteps; ++k) {
                m.mag[k] = static_cast<int>(br.Read(Ilog(static_cast<std::uint32_t>(id.channels - 1))));
                m.ang[k] = static_cast<int>(br.Read(Ilog(static_cast<std::uint32_t>(id.channels - 1))));
                if (m.mag[k] == m.ang[k] || m.mag[k] >= id.channels || m.ang[k] >= id.channels) { err = "bad channel coupling"; return false; }
            }
        }
        if (br.Read(2) != 0) { err = "bad mapping"; return false; }
        if (m.submaps > 1)
            for (int c = 0; c < id.channels; ++c) {
                m.mux[c] = static_cast<int>(br.Read(4));
                if (m.mux[c] >= m.submaps) { err = "bad channel multiplexing"; return false; }
            }
        for (int k = 0; k < m.submaps; ++k) {
            br.Read(8);
            m.submapFloor[k] = static_cast<int>(br.Read(8));
            m.submapResidue[k] = static_cast<int>(br.Read(8));
            if (m.submapFloor[k] >= nfloors || m.submapResidue[k] >= nres) { err = "mapping refers to a missing floor or residue"; return false; }
        }
        if (br.eop) { err = "mapping cut short"; return false; }
    }

    const int nmodes = static_cast<int>(br.Read(6)) + 1;
    s.modes.resize(static_cast<std::size_t>(nmodes));
    for (int i = 0; i < nmodes; ++i) {
        Mode& m = s.modes[static_cast<std::size_t>(i)];
        m.blockflag = static_cast<int>(br.Read(1));
        const std::uint32_t wt = br.Read(16), tt = br.Read(16);
        m.mapping = static_cast<int>(br.Read(8));
        if (wt != 0 || tt != 0 || m.mapping >= nmaps) { err = "bad mode"; return false; }
    }
    if (!br.Read(1) || br.eop) { err = "setup header framing error"; return false; }
    return true;
}

// ---- inverse MDCT ------------------------------------------------------------------------------

// N coefficients -> 2N samples:
//   y[k] = sum_i X[i] cos(pi/N (k + 1/2 + N/2)(i + 1/2)).
// Computed as a DCT-IV (through an N/2-point complex FFT) followed by the
// usual unfolding.
struct Mdct {
    int N = 0, H = 0;
    std::vector<float> tw;                // exp(-i pi (n + 1/8) / N), n < H, as (cos, sin)
    std::vector<float> ftw;               // exp(-2 pi i j / H), j < H/2
    std::vector<std::uint32_t> rev;
    std::vector<float> work, dct;

    void Init(int n) {
        N = n;
        H = n / 2;
        const int bits = Ilog(static_cast<std::uint32_t>(H)) - 1;
        rev.resize(static_cast<std::size_t>(H));
        for (int i = 0; i < H; ++i) {
            std::uint32_t r = 0;
            for (int b = 0; b < bits; ++b) if ((i >> b) & 1) r |= 1u << (bits - 1 - b);
            rev[static_cast<std::size_t>(i)] = r;
        }
        tw.resize(static_cast<std::size_t>(H) * 2);
        for (int k = 0; k < H; ++k) {
            const double a = -kPi * (k + 0.125) / N;
            tw[2 * k] = static_cast<float>(std::cos(a));
            tw[2 * k + 1] = static_cast<float>(std::sin(a));
        }
        ftw.resize(static_cast<std::size_t>(std::max(H, 2)));
        for (int j = 0; j < H / 2; ++j) {
            const double a = -2.0 * kPi * j / H;
            ftw[2 * j] = static_cast<float>(std::cos(a));
            ftw[2 * j + 1] = static_cast<float>(std::sin(a));
        }
        work.assign(static_cast<std::size_t>(N), 0.0f);
        dct.assign(static_cast<std::size_t>(N), 0.0f);
    }

    void Inverse(const float* in, float* out, float scale) {
        float* a = work.data();
        for (int n = 0; n < H; ++n) {
            const float re = in[2 * n], im = in[N - 1 - 2 * n];
            const float c = tw[2 * n], s = tw[2 * n + 1];
            const std::size_t r = rev[static_cast<std::size_t>(n)];
            a[2 * r] = re * c - im * s;
            a[2 * r + 1] = re * s + im * c;
        }
        for (int len = 2; len <= H; len <<= 1) {
            const int half = len / 2, step = H / len;
            for (int i = 0; i < H; i += len)
                for (int j = 0; j < half; ++j) {
                    const float wr = ftw[2 * (j * step)], wi = ftw[2 * (j * step) + 1];
                    float* p = a + 2 * (i + j);
                    float* q = a + 2 * (i + j + half);
                    const float vr = q[0] * wr - q[1] * wi, vi = q[0] * wi + q[1] * wr;
                    q[0] = p[0] - vr; q[1] = p[1] - vi;
                    p[0] += vr; p[1] += vi;
                }
        }
        float* y = dct.data();
        for (int k = 0; k < H; ++k) {
            const float c = tw[2 * k], s = tw[2 * k + 1];
            const float zr = a[2 * k], zi = a[2 * k + 1];
            y[2 * k] = zr * c - zi * s;
            y[N - 1 - 2 * k] = -(zr * s + zi * c);
        }
        const int h = N / 2;
        for (int k = 0; k < h; ++k) out[k] = y[h + k] * scale;
        for (int k = h; k < 3 * h; ++k) out[k] = -y[3 * h - 1 - k] * scale;
        for (int k = 3 * h; k < 2 * N; ++k) out[k] = -y[k - 3 * h] * scale;
    }
};

// ---- floor 1 -------------------------------------------------------------------------------------

const float* FloorDbTable() {
    // The 256-entry table of the specification is a geometric progression from
    // 1.0649863e-07 (index 0) to 1.0 (index 255).
    static const float* table = [] {
        static float t[256];
        const double l = std::log(1.0649863e-07) / 255.0;
        for (int i = 0; i < 256; ++i) t[i] = static_cast<float>(std::exp(l * (255 - i)));
        t[255] = 1.0f;
        return t;
    }();
    return table;
}

int RenderPoint(int x0, int y0, int x1, int y1, int x) {
    const int dy = y1 - y0;
    const int adx = x1 - x0;
    const int ady = dy < 0 ? -dy : dy;
    const std::int64_t err = static_cast<std::int64_t>(ady) * (x - x0);
    const int off = static_cast<int>(err / adx);
    return dy < 0 ? y0 - off : y0 + off;
}

void RenderLine(int x0, int y0, int x1, int y1, float* out, int n) {
    const float* db = FloorDbTable();
    const int dy = y1 - y0;
    const int adx = x1 - x0;
    int ady = dy < 0 ? -dy : dy;
    const int base = dy / adx;
    int y = y0;
    int err = 0;
    const int sy = dy < 0 ? base - 1 : base + 1;
    ady -= (base < 0 ? -base : base) * adx;
    auto put = [&](int x, int yy) {
        if (x >= 0 && x < n) out[x] = db[std::min(255, std::max(0, yy))];
    };
    put(x0, y);
    for (int x = x0 + 1; x < x1 && x < n; ++x) {
        err += ady;
        if (err >= adx) { err -= adx; y += sy; } else { y += base; }
        put(x, y);
    }
}

// Reads the floor of one channel. Returns false when the floor is unused (or
// the packet ended inside it), true with `curve` (h values) filled otherwise.
bool DecodeFloor1(const Floor1& f, const std::vector<Codebook>& books, BitReader& br, float* curve, int h) {
    if (!br.Read(1) || br.eop) return false;
    static const int kRange[4] = {256, 128, 86, 64};
    const int range = kRange[f.mult - 1];
    int y[260];
    const int bits = Ilog(static_cast<std::uint32_t>(range - 1));
    y[0] = static_cast<int>(br.Read(bits));
    y[1] = static_cast<int>(br.Read(bits));
    int offset = 2;
    for (int i = 0; i < f.partitions; ++i) {
        const int cls = f.partClass[i];
        const int cdim = f.classDim[cls], cbits = f.classSub[cls];
        const int csub = (1 << cbits) - 1;
        int cval = 0;
        if (cbits > 0) {
            cval = DecodeScalar(books[static_cast<std::size_t>(f.classMaster[cls])], br);
            if (cval < 0) return false;
        }
        for (int j = 0; j < cdim; ++j) {
            const int book = f.subBook[cls][cval & csub];
            cval >>= cbits;
            if (book >= 0) {
                const int v = DecodeScalar(books[static_cast<std::size_t>(book)], br);
                if (v < 0) return false;
                y[offset + j] = v;
            } else {
                y[offset + j] = 0;
            }
        }
        offset += cdim;
    }
    if (br.eop) return false;

    int fy[260];
    bool step2[260];
    auto clampY = [range](int v) { return v < 0 ? 0 : (v >= range ? range - 1 : v); };   // keeps hostile values from overflowing
    fy[0] = clampY(y[0]); fy[1] = clampY(y[1]);
    step2[0] = step2[1] = true;
    for (int i = 2; i < f.count; ++i) {
        const int lo = f.lowN[i], hi = f.highN[i];
        const int predicted = RenderPoint(f.x[lo], fy[lo], f.x[hi], fy[hi], f.x[i]);
        const int val = y[i];
        const int highroom = range - predicted;
        const int lowroom = predicted;
        const int room = (highroom < lowroom ? highroom : lowroom) * 2;
        if (val != 0) {
            step2[lo] = step2[hi] = step2[i] = true;
            if (val >= room) {
                fy[i] = highroom > lowroom ? val - lowroom + predicted : predicted - val + highroom - 1;
            } else {
                fy[i] = (val & 1) ? predicted - ((val + 1) >> 1) : predicted + (val >> 1);
            }
        } else {
            step2[i] = false;
            fy[i] = predicted;
        }
        fy[i] = clampY(fy[i]);
    }

    int hx = 0, hy = 0, lx = 0;
    int ly = fy[f.sorted[0]] * f.mult;
    for (int i = 1; i < f.count; ++i) {
        const int idx = f.sorted[i];
        if (!step2[idx]) continue;
        hy = fy[idx] * f.mult;
        hx = f.x[idx];
        RenderLine(lx, ly, hx, hy, curve, h);
        lx = hx;
        ly = hy;
    }
    if (hx < h) RenderLine(hx, hy, h, hy, curve, h);
    return true;
}

// Floor type 0: line spectral pair coefficients, evaluated on the Bark scale. The
// arithmetic follows the reference decoder's (single precision, the same
// constants) so that the Bark map lands on the same coefficient boundaries.
double ToBark(float x) {
    return 13.1f * std::atan(static_cast<double>(.00074f * x)) + 2.24f * std::atan(static_cast<double>(x * x * 1.85e-8f)) + 1e-4f * x;
}

void BuildBarkMap(Floor0& f, int blockIndex, int n) {
    std::vector<int>& map = f.map[blockIndex];
    map.assign(static_cast<std::size_t>(n) + 1, 0);
    const float scale = static_cast<float>(f.barkMapSize / ToBark(f.rate / 2.f));
    for (int j = 0; j < n; ++j) {
        int val = static_cast<int>(std::floor(ToBark((f.rate / 2.f) / static_cast<float>(n) * static_cast<float>(j)) * scale));
        if (val >= f.barkMapSize) val = f.barkMapSize - 1;
        if (val < 0) val = 0;
        map[static_cast<std::size_t>(j)] = val;
    }
    map[static_cast<std::size_t>(n)] = -1;
}

bool DecodeFloor0(const Floor0& f, const std::vector<Codebook>& books, BitReader& br, int blockIndex, float* curve, int h,
                  std::vector<float>& lsp) {
    if (f.ampBits > 32) return false;
    const std::uint32_t ampRaw = br.Read(f.ampBits);
    if (br.eop || ampRaw == 0) return false;
    const std::uint32_t maxVal = f.ampBits >= 32 ? 0xFFFFFFFFu : (1u << f.ampBits) - 1u;
    const float amp = static_cast<float>(ampRaw) / static_cast<float>(maxVal) * static_cast<float>(f.ampOffset);
    const std::uint32_t bookNum = br.Read(Ilog(static_cast<std::uint32_t>(f.numBooks)));
    if (br.eop || bookNum >= static_cast<std::uint32_t>(f.numBooks)) return false;
    const Codebook& b = books[static_cast<std::size_t>(f.books[bookNum])];
    const std::size_t m = static_cast<std::size_t>(f.order);
    const std::size_t dims = static_cast<std::size_t>(b.dims);
    lsp.assign(m + dims + 1, 0.0f);
    std::vector<float> vec(dims);
    float last = 0.0f;
    for (std::size_t j = 0; j < m; j += dims) {
        const int e = DecodeScalar(b, br);
        if (e < 0) return false;
        VqValues(b, static_cast<std::uint32_t>(e), vec.data(), b.dims);
        for (std::size_t k = 0; k < dims; ++k) lsp[j + k] = vec[k] + last;
        last = lsp[j + dims - 1];
    }
    const std::vector<int>& map = f.map[blockIndex];
    const float wdel = static_cast<float>(kPi / f.barkMapSize);
    for (std::size_t i = 0; i < m; ++i) lsp[i] = static_cast<float>(2.0 * std::cos(static_cast<double>(lsp[i])));
    int i = 0;
    while (i < h) {
        const int k = map[static_cast<std::size_t>(i)];
        float p = .5f, q = .5f;
        const float w = static_cast<float>(2.0 * std::cos(static_cast<double>(wdel * static_cast<float>(k))));
        std::size_t j = 1;
        for (; j < m; j += 2) {
            q *= w - lsp[j - 1];
            p *= w - lsp[j];
        }
        if (j == m) {            // odd order
            q *= w - lsp[j - 1];
            p *= p * (4.f - w * w);
            q *= q;
        } else {                 // even order
            p *= p * (2.f - w);
            q *= q * (2.f + w);
        }
        const float value = static_cast<float>(std::exp(static_cast<double>((amp / std::sqrt(p + q) - static_cast<float>(f.ampOffset)) * .11512925f)));
        curve[i] = value;
        while (++i < h && map[static_cast<std::size_t>(i)] == k) curve[i] = value;
    }
    return true;
}

// ---- residue -------------------------------------------------------------------------------------

// Decodes one residue configuration into `nvec` vectors of `len` floats
// (adding to what they hold). Stops quietly when the packet ends.
void DecodeResidue(const Residue& r, const std::vector<Codebook>& books, BitReader& br, float** vec, int nvec,
                   std::size_t len, std::vector<std::uint8_t>& clsStore, std::vector<float>& tmp,
                   std::vector<std::uint32_t>& entries) {
    const std::size_t lb = std::min<std::size_t>(r.begin, len);
    const std::size_t le = std::min<std::size_t>(r.end, len);
    if (le <= lb) return;
    const std::size_t ps = r.partSize;
    const std::size_t parts = (le - lb) / ps;
    if (parts == 0) return;
    if (r.stages == 0) return;      // no class has a codebook: nothing is read, not even class words
    const Codebook& cb = books[static_cast<std::size_t>(r.classbook)];
    const std::size_t classwords = static_cast<std::size_t>(cb.dims);
    const std::size_t stride = parts;
    // A classification word must name one of classes^classwords combinations;
    // a larger value ends the residue like a cut-off packet.
    std::uint64_t combos = 1;
    for (std::size_t i = 0; i < classwords && r.classes > 1 && combos <= cb.entries; ++i) combos *= static_cast<std::uint64_t>(r.classes);
    clsStore.assign(stride * static_cast<std::size_t>(nvec), 0);
    for (int pass = 0; pass < r.stages; ++pass) {
        std::size_t pc = 0;
        while (pc < parts) {
            if (pass == 0) {
                for (int j = 0; j < nvec; ++j) {
                    const int e = DecodeScalar(cb, br);
                    if (e < 0 || static_cast<std::uint64_t>(e) >= combos) return;
                    std::uint32_t temp = static_cast<std::uint32_t>(e);
                    std::uint8_t* c = clsStore.data() + stride * static_cast<std::size_t>(j) + pc;
                    // Digits from the last partition of the word backwards; once the value is used up
                    // the remaining (earlier) ones are zero, which the zeroed storage already says.
                    for (std::size_t i = classwords; i-- > 0 && temp != 0;) {
                        if (pc + i < parts) c[i] = static_cast<std::uint8_t>(temp % static_cast<std::uint32_t>(r.classes));
                        temp /= static_cast<std::uint32_t>(r.classes);
                    }
                }
            }
            for (std::size_t i = 0; i < classwords && pc < parts; ++i, ++pc) {
                for (int j = 0; j < nvec; ++j) {
                    const int cls = clsStore[stride * static_cast<std::size_t>(j) + pc];
                    const int bookIdx = r.books[cls][pass];
                    if (bookIdx < 0) continue;
                    const Codebook& vb = books[static_cast<std::size_t>(bookIdx)];
                    const std::size_t off = lb + pc * ps;
                    float* out = vec[j];
                    const std::size_t dims = static_cast<std::size_t>(vb.dims);
                    if (tmp.size() < dims) tmp.resize(dims);
                    if (r.type == 0) {
                        // The partition's codewords are all read before any value is added,
                        // so a packet that ends inside it contributes nothing of it.
                        const std::size_t step = ps / dims;
                        entries.resize(step);
                        for (std::size_t k = 0; k < step; ++k) {
                            const int e = DecodeScalar(vb, br);
                            if (e < 0) return;
                            entries[k] = static_cast<std::uint32_t>(e);
                        }
                        for (std::size_t k = 0; k < step; ++k) {
                            VqValues(vb, entries[k], tmp.data(), vb.dims);
                            for (std::size_t m = 0; m < dims; ++m) out[off + k + m * step] += tmp[m];
                        }
                    } else {
                        for (std::size_t k = 0; k < ps; k += dims) {
                            const int e = DecodeScalar(vb, br);
                            if (e < 0) return;
                            const std::size_t m = std::min(dims, ps - k);
                            VqValues(vb, static_cast<std::uint32_t>(e), tmp.data(), static_cast<int>(m));
                            for (std::size_t t = 0; t < m; ++t) out[off + k + t] += tmp[t];
                        }
                    }
                }
            }
        }
    }
}

// ---- the stream decoder ------------------------------------------------------------------------

class StreamDecoder {
public:
    StreamDecoder(const Ident& id, Setup&& setup) : id_(id), setup_(std::move(setup)) {
        for (int b = 0; b < 2; ++b) mdct_[b].Init(id_.blocksize[b] / 2);
        for (Floor& fl : setup_.floors)
            if (fl.type == 0)
                for (int b = 0; b < 2; ++b) BuildBarkMap(fl.f0, b, id_.blocksize[b] / 2);
        BuildWindows();
        const std::size_t half1 = static_cast<std::size_t>(id_.blocksize[1] / 2);
        const std::size_t ch = static_cast<std::size_t>(id_.channels);
        floorCurve_.assign(ch, std::vector<float>(half1, 0.0f));
        residue_.assign(ch, std::vector<float>(half1, 0.0f));
        time_.assign(ch, std::vector<float>(static_cast<std::size_t>(id_.blocksize[1]), 0.0f));
        tail_.assign(ch, std::vector<float>(half1, 0.0f));
    }

    // Decodes one audio packet and appends the finished samples (interleaved)
    // to `out`. Packets that are not audio or are unusable produce nothing.
    // Returns the number of frames appended.
    std::size_t Decode(const unsigned char* data, std::size_t size, std::vector<float>& out) {
        if (size == 0) return 0;
        BitReader br(data, size);
        if (br.Read(1) != 0) return 0;                              // not an audio packet
        const std::uint32_t mode = br.Read(Ilog(static_cast<std::uint32_t>(setup_.modes.size() - 1)));
        if (br.eop || mode >= setup_.modes.size()) return 0;
        const Mode& md = setup_.modes[mode];
        int prevFlag = 0, nextFlag = 0;
        if (md.blockflag) {
            prevFlag = static_cast<int>(br.Read(1));
            nextFlag = static_cast<int>(br.Read(1));
            if (br.eop) return 0;
        }
        const int n = id_.blocksize[md.blockflag];
        const int h = n / 2;
        const Mapping& map = setup_.mappings[static_cast<std::size_t>(md.mapping)];
        const int ch = id_.channels;

        bool used[kMaxChannels];
        bool noResidue[kMaxChannels];
        for (int c = 0; c < ch; ++c) {
            const Floor& f = setup_.floors[static_cast<std::size_t>(map.submapFloor[map.mux[c]])];
            float* curve = floorCurve_[static_cast<std::size_t>(c)].data();
            used[c] = f.type == 0 ? DecodeFloor0(f.f0, setup_.books, br, md.blockflag, curve, h, lsp_)
                                  : DecodeFloor1(f.f1, setup_.books, br, curve, h);
            noResidue[c] = !used[c];
            std::fill(residue_[static_cast<std::size_t>(c)].begin(), residue_[static_cast<std::size_t>(c)].begin() + h, 0.0f);
        }
        for (int k = 0; k < map.couplingSteps; ++k)
            if (!noResidue[map.mag[k]] || !noResidue[map.ang[k]]) noResidue[map.mag[k]] = noResidue[map.ang[k]] = false;

        for (int sm = 0; sm < map.submaps; ++sm) {
            const Residue& r = setup_.residues[static_cast<std::size_t>(map.submapResidue[sm])];
            int chans[kMaxChannels];
            int count = 0;
            bool any = false;
            for (int c = 0; c < ch; ++c)
                if (map.mux[c] == sm) { chans[count++] = c; any = any || !noResidue[c]; }
            if (count == 0) continue;
            if (r.type == 2) {
                if (!any) continue;
                const std::size_t len = static_cast<std::size_t>(h) * static_cast<std::size_t>(count);
                interleaved_.assign(len, 0.0f);
                float* v = interleaved_.data();
                DecodeResidue(r, setup_.books, br, &v, 1, len, cls_, tmp_, entries_);
                for (int i = 0; i < h; ++i)
                    for (int c = 0; c < count; ++c) residue_[static_cast<std::size_t>(chans[c])][static_cast<std::size_t>(i)] = v[static_cast<std::size_t>(i) * count + c];
            } else {
                float* vecs[kMaxChannels];
                int nv = 0;
                for (int c = 0; c < count; ++c)
                    if (!noResidue[chans[c]]) vecs[nv++] = residue_[static_cast<std::size_t>(chans[c])].data();
                if (nv) DecodeResidue(r, setup_.books, br, vecs, nv, static_cast<std::size_t>(h), cls_, tmp_, entries_);
            }
        }

        for (int k = map.couplingSteps - 1; k >= 0; --k) {
            float* m = residue_[static_cast<std::size_t>(map.mag[k])].data();
            float* a = residue_[static_cast<std::size_t>(map.ang[k])].data();
            for (int i = 0; i < h; ++i) {
                const float M = m[i], A = a[i];
                float nm, na;
                if (M > 0) {
                    if (A > 0) { nm = M; na = M - A; } else { na = M; nm = M + A; }
                } else {
                    if (A > 0) { nm = M; na = M + A; } else { na = M; nm = M - A; }
                }
                m[i] = nm;
                a[i] = na;
            }
        }

        const float* window = windows_[md.blockflag ? 1 + prevFlag * 2 + nextFlag : 0].data();
        Mdct& mdct = mdct_[md.blockflag];
        for (int c = 0; c < ch; ++c) {
            float* t = time_[static_cast<std::size_t>(c)].data();
            if (!used[c]) {
                std::fill(t, t + n, 0.0f);
                continue;
            }
            float* res = residue_[static_cast<std::size_t>(c)].data();
            const float* fl = floorCurve_[static_cast<std::size_t>(c)].data();
            for (int i = 0; i < h; ++i) res[i] *= fl[i];
            mdct.Inverse(res, t, kImdctScale);
            for (int i = 0; i < n; ++i) {
                const float x = t[i] * window[i];
                t[i] = std::isfinite(x) ? x : 0.0f;       // hostile values can overflow
            }
        }

        std::size_t produced = 0;
        if (havePrev_) {
            const int pn = prevN_;
            const std::size_t L = static_cast<std::size_t>(pn / 4 + n / 4);
            const std::size_t base = out.size();
            out.resize(base + L * static_cast<std::size_t>(ch));
            for (int c = 0; c < ch; ++c) {
                const float* tl = tail_[static_cast<std::size_t>(c)].data();
                const float* t = time_[static_cast<std::size_t>(c)].data();
                const int shift = n / 4 - pn / 4;
                for (std::size_t k = 0; k < L; ++k) {
                    float v = k < static_cast<std::size_t>(pn / 2) ? tl[k] : 0.0f;
                    const long idx = static_cast<long>(k) + shift;
                    if (idx >= 0) v += t[idx];
                    if (!std::isfinite(v)) v = 0.0f;
                    out[base + k * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] = v;
                }
            }
            produced = L;
        }
        for (int c = 0; c < ch; ++c)
            std::copy(time_[static_cast<std::size_t>(c)].begin() + h, time_[static_cast<std::size_t>(c)].begin() + n, tail_[static_cast<std::size_t>(c)].begin());
        havePrev_ = true;
        prevN_ = n;
        return produced;
    }

    int Channels() const { return id_.channels; }
    int MaxFramesPerPacket() const { return id_.blocksize[1] / 4 * 2; }

private:
    // IMDCT scale so that windowed overlap-add reconstructs unit-amplitude audio.
    static constexpr float kImdctScale = 1.0f;

    void BuildWindows() {
        // Slope tables: sin(pi/2 * sin^2((i + 0.5) / len * pi/2)) for a slope of `len` samples.
        auto slope = [](int len, int i) {
            const double s = std::sin((i + 0.5) / len * kPi / 2.0);
            return std::sin(kPi / 2.0 * s * s);
        };
        const int n0 = id_.blocksize[0], n1 = id_.blocksize[1];
        windows_[0].assign(static_cast<std::size_t>(n0), 0.0f);
        for (int i = 0; i < n0 / 2; ++i) {
            windows_[0][static_cast<std::size_t>(i)] = static_cast<float>(slope(n0 / 2, i));
            windows_[0][static_cast<std::size_t>(n0 - 1 - i)] = static_cast<float>(slope(n0 / 2, i));
        }
        for (int variant = 0; variant < 4; ++variant) {
            const bool prevLong = (variant >> 1) & 1, nextLong = variant & 1;
            std::vector<float>& w = windows_[static_cast<std::size_t>(1 + variant)];
            w.assign(static_cast<std::size_t>(n1), 0.0f);
            const int ls = prevLong ? 0 : n1 / 4 - n0 / 4;
            const int ln = prevLong ? n1 / 2 : n0 / 2;
            const int rs = nextLong ? n1 / 2 : n1 * 3 / 4 - n0 / 4;
            const int rn = nextLong ? n1 / 2 : n0 / 2;
            for (int i = 0; i < n1; ++i) {
                double v;
                if (i < ls) v = 0.0;
                else if (i < ls + ln) v = slope(ln, i - ls);
                else if (i < rs) v = 1.0;
                else if (i < rs + rn) v = slope(rn, rn - 1 - (i - rs));
                else v = 0.0;
                w[static_cast<std::size_t>(i)] = static_cast<float>(v);
            }
        }
    }

    Ident id_;
    Setup setup_;
    Mdct mdct_[2];
    std::vector<float> windows_[5];
    std::vector<std::vector<float>> floorCurve_, residue_, time_, tail_;
    std::vector<float> interleaved_, tmp_, lsp_;
    std::vector<std::uint8_t> cls_;
    std::vector<std::uint32_t> entries_;
    bool havePrev_ = false;
    int prevN_ = 0;
};

enum class StreamResult { Ok, BadHeaders, FormatMismatch };

// Decodes one logical stream, appending to `out`. `first` is true for the
// stream that defines the output format.
StreamResult DecodeStream(const PacketList& pl, bool first, const VorbisOptions& opt,
                          VorbisAudio& out, std::string& err) {
    if (pl.packets.size() < 3) { err = "Vorbis headers cut short"; return StreamResult::BadHeaders; }
    auto pkt = [&](std::size_t i) { return pl.arena.data() + pl.packets[i].off; };
    Ident id;
    if (!ParseIdent(pkt(0), pl.packets[0].len, id, err)) return StreamResult::BadHeaders;
    if (!IsHeader(pkt(1), pl.packets[1].len, 3)) { err = "missing Vorbis comment header"; return StreamResult::BadHeaders; }
    Setup setup;
    if (!ParseSetup(pkt(2), pl.packets[2].len, id, setup, err)) return StreamResult::BadHeaders;
    if (!first && (id.channels != out.channels || id.rate != out.sampleRate)) return StreamResult::FormatMismatch;
    if (first) { out.channels = id.channels; out.sampleRate = id.rate; }

    StreamDecoder dec(id, std::move(setup));
    const std::size_t startSamples = out.samples.size();
    const std::size_t chans = static_cast<std::size_t>(id.channels);
    for (std::size_t i = 3; i < pl.packets.size(); ++i) {
        if (out.samples.size() + static_cast<std::size_t>(dec.MaxFramesPerPacket()) * chans > opt.maxSamples) {
            out.truncated = true;
            break;
        }
        dec.Decode(pkt(i), pl.packets[i].len, out.samples);
    }
    // The last page's granule position is the true length of the stream.
    if (pl.sawEos && pl.eosGranule >= 0) {
        const std::uint64_t frames = (out.samples.size() - startSamples) / chans;
        if (static_cast<std::uint64_t>(pl.eosGranule) < frames)
            out.samples.resize(startSamples + static_cast<std::size_t>(pl.eosGranule) * chans);
    }
    return StreamResult::Ok;
}

} // namespace

bool DecodeVorbis(const unsigned char* data, std::size_t size, VorbisAudio& out, std::string* error,
                  const VorbisOptions* options) {
    out = VorbisAudio();
    std::string err;
    VorbisOptions opt;
    if (options) opt = *options;
    try {
        if (!data || size < 27 || size > 0xFFFFFFF0u) {
            if (error) *error = "not an Ogg file";
            return false;
        }
        std::vector<Page> pages;
        ScanPages(data, size, pages);
        if (pages.empty()) {
            if (error) *error = "not an Ogg file";
            return false;
        }
        std::size_t idx = 0;
        std::string firstErr;
        while (idx < pages.size()) {
            // The next stream that starts with a Vorbis identification header.
            std::size_t start = idx;
            while (start < pages.size()) {
                const Page& p = pages[start];
                if ((p.flags & 2) && p.nseg > 0 && IsHeader(data + p.dataOff, data[p.lacingOff], 1)) break;
                ++start;
            }
            if (start >= pages.size()) break;
            ++out.streamsFound;
            PacketList pl;
            idx = CollectPackets(data, pages, start, pl);
            if (out.streamsDecoded > 0 && !opt.allStreams) continue;   // only counted
            const bool first = out.streamsDecoded == 0;
            const std::size_t before = out.samples.size();
            err.clear();
            const StreamResult r = DecodeStream(pl, first, opt, out, err);
            if (r == StreamResult::Ok && out.samples.size() > before) {
                ++out.streamsDecoded;
                if (!pl.sawEos) out.truncated = true;
            } else if (first) {
                out.channels = out.sampleRate = 0;
                out.samples.clear();
                if (firstErr.empty()) firstErr = err.empty() ? "the Vorbis stream holds no audio" : err;
            }
            if (out.truncated && (!pl.sawEos || out.samples.size() >= opt.maxSamples)) break;
        }
        err = firstErr;
    } catch (...) {
        err = "out of memory while decoding";
        if (out.samples.empty()) { out.channels = out.sampleRate = 0; if (error) *error = err; return false; }
        out.truncated = true;
        return true;
    }
    if (out.streamsDecoded == 0 || out.samples.empty()) {
        out.samples.clear();
        out.channels = out.sampleRate = 0;
        if (error) *error = err.empty() ? "no Ogg Vorbis stream found" : err;
        return false;
    }
    return true;
}

} // namespace okay
