#include "okay/Core/ImageWrite.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace okay {
namespace ImageWrite {

namespace {

// ---- checksums ----------------------------------------------------------------------------------
const std::uint32_t* CrcTable() {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    return table;
}

// ---- bit writer (DEFLATE packs bits LSB first) ----------------------------------------------------
struct BitWriter {
    std::vector<std::uint8_t>& out;
    std::uint32_t acc = 0;
    int nbits = 0;
    explicit BitWriter(std::vector<std::uint8_t>& o) : out(o) {}
    void Put(std::uint32_t value, int bits) {
        acc |= value << nbits;
        nbits += bits;
        while (nbits >= 8) { out.push_back(static_cast<std::uint8_t>(acc & 0xFF)); acc >>= 8; nbits -= 8; }
    }
    // Huffman codes are packed MSB first, so reverse before putting.
    void PutCode(std::uint32_t code, int len) {
        std::uint32_t r = 0;
        for (int i = 0; i < len; ++i) { r = (r << 1) | (code & 1u); code >>= 1; }
        Put(r, len);
    }
    void Flush() { if (nbits > 0) { out.push_back(static_cast<std::uint8_t>(acc & 0xFF)); acc = 0; nbits = 0; } }
};

// ---- length / distance code tables ---------------------------------------------------------------
const int kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const int kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const int kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const int kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

int LenCode(int len) { int c = 28; while (kLenBase[c] > len) --c; return c; }
int DistCode(int dist) { int c = 29; while (kDistBase[c] > dist) --c; return c; }

// ---- Huffman code construction (length limited) ---------------------------------------------------
// Fills lens[0..n) with code lengths <= maxBits for the given frequencies. Unused symbols get 0.
void BuildLengths(const std::uint32_t* freqIn, int n, int maxBits, std::uint8_t* lens) {
    std::vector<std::uint32_t> freq(freqIn, freqIn + n);
    for (;;) {
        struct Node { std::uint64_t w; int left, right; };
        std::vector<Node> nodes;
        std::vector<int> heap;
        std::vector<int> leaf(static_cast<std::size_t>(n), -1);
        for (int i = 0; i < n; ++i) {
            lens[i] = 0;
            if (freq[static_cast<std::size_t>(i)]) { leaf[static_cast<std::size_t>(i)] = static_cast<int>(nodes.size()); nodes.push_back({freq[static_cast<std::size_t>(i)], -1, -1}); }
        }
        if (nodes.empty()) return;
        if (nodes.size() == 1) {
            for (int i = 0; i < n; ++i) if (leaf[static_cast<std::size_t>(i)] >= 0) lens[i] = 1;
            return;
        }
        auto cmp = [&](int a, int b) { return nodes[static_cast<std::size_t>(a)].w > nodes[static_cast<std::size_t>(b)].w; };
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) heap.push_back(i);
        std::make_heap(heap.begin(), heap.end(), cmp);
        while (heap.size() > 1) {
            std::pop_heap(heap.begin(), heap.end(), cmp); int a = heap.back(); heap.pop_back();
            std::pop_heap(heap.begin(), heap.end(), cmp); int b = heap.back(); heap.pop_back();
            nodes.push_back({nodes[static_cast<std::size_t>(a)].w + nodes[static_cast<std::size_t>(b)].w, a, b});
            heap.push_back(static_cast<int>(nodes.size()) - 1);
            std::push_heap(heap.begin(), heap.end(), cmp);
        }
        // Depths, walking down from the root (the last node).
        std::vector<int> depth(nodes.size(), 0);
        for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
            const Node& nd = nodes[static_cast<std::size_t>(i)];
            if (nd.left >= 0) { depth[static_cast<std::size_t>(nd.left)] = depth[static_cast<std::size_t>(i)] + 1; depth[static_cast<std::size_t>(nd.right)] = depth[static_cast<std::size_t>(i)] + 1; }
        }
        int maxD = 0;
        for (int i = 0; i < n; ++i) if (leaf[static_cast<std::size_t>(i)] >= 0) {
            lens[i] = static_cast<std::uint8_t>(std::min(depth[static_cast<std::size_t>(leaf[static_cast<std::size_t>(i)])], 255));
            maxD = std::max(maxD, depth[static_cast<std::size_t>(leaf[static_cast<std::size_t>(i)])]);
        }
        if (maxD <= maxBits) return;
        // Too deep: flatten the distribution and retry.
        for (auto& f : freq) if (f) f = (f + 1) / 2;
    }
}

// Canonical codes from lengths.
void BuildCodes(const std::uint8_t* lens, int n, std::uint16_t* codes) {
    int count[16] = {0};
    for (int i = 0; i < n; ++i) ++count[lens[i]];
    count[0] = 0;
    std::uint32_t next[16] = {0};
    std::uint32_t code = 0;
    for (int b = 1; b < 16; ++b) { code = (code + static_cast<std::uint32_t>(count[b - 1])) << 1; next[b] = code; }
    for (int i = 0; i < n; ++i) codes[i] = lens[i] ? static_cast<std::uint16_t>(next[lens[i]]++) : 0;
}

// ---- LZ77 tokens -----------------------------------------------------------------------------------
struct Token { std::uint16_t lenOrLit; std::uint16_t dist; };   // dist == 0 -> literal

void WriteBlock(BitWriter& bw, const std::vector<Token>& toks, bool last) {
    std::uint32_t lf[286] = {0}, df[30] = {0};
    for (const Token& t : toks) {
        if (t.dist == 0) ++lf[t.lenOrLit];
        else { ++lf[257 + LenCode(t.lenOrLit)]; ++df[DistCode(t.dist)]; }
    }
    lf[256] = 1;
    // Decoders are happiest with at least two codes in each tree.
    { int used = 0; for (int i = 0; i < 30; ++i) used += df[i] != 0; if (used < 2) { if (!df[0]) df[0] = 1; else df[1] = 1; } }

    std::uint8_t ll[286], dl[30];
    BuildLengths(lf, 286, 15, ll);
    BuildLengths(df, 30, 15, dl);
    std::uint16_t lc[286], dc[30];
    BuildCodes(ll, 286, lc);
    BuildCodes(dl, 30, dc);

    int hlit = 286; while (hlit > 257 && ll[hlit - 1] == 0) --hlit;
    int hdist = 30; while (hdist > 1 && dl[hdist - 1] == 0) --hdist;

    // Run-length encode the code lengths with symbols 16/17/18.
    std::vector<std::uint8_t> seq(ll, ll + hlit);
    seq.insert(seq.end(), dl, dl + hdist);
    struct Rle { std::uint8_t sym, extra; };
    std::vector<Rle> rle;
    for (std::size_t i = 0; i < seq.size();) {
        std::size_t run = 1;
        while (i + run < seq.size() && seq[i + run] == seq[i]) ++run;
        std::uint8_t v = seq[i];
        std::size_t left = run;
        if (v == 0) {
            while (left >= 3) {
                if (left >= 11) { std::size_t k = std::min<std::size_t>(left, 138); rle.push_back({18, static_cast<std::uint8_t>(k - 11)}); left -= k; }
                else { rle.push_back({17, static_cast<std::uint8_t>(left - 3)}); left = 0; }
            }
        } else {
            rle.push_back({v, 0}); --left;
            while (left >= 3) { std::size_t k = std::min<std::size_t>(left, 6); rle.push_back({16, static_cast<std::uint8_t>(k - 3)}); left -= k; }
        }
        while (left > 0) { rle.push_back({v, 0}); --left; }
        i += run;
    }
    std::uint32_t cf[19] = {0};
    for (const Rle& r : rle) ++cf[r.sym];
    { int used = 0; for (int i = 0; i < 19; ++i) used += cf[i] != 0; for (int i = 0; used < 2 && i < 19; ++i) if (!cf[i]) { cf[i] = 1; ++used; } }
    std::uint8_t cl[19];
    BuildLengths(cf, 19, 7, cl);
    std::uint16_t cc[19];
    BuildCodes(cl, 19, cc);
    static const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int hclen = 19; while (hclen > 4 && cl[order[hclen - 1]] == 0) --hclen;

    bw.Put(last ? 1u : 0u, 1);
    bw.Put(2u, 2);   // dynamic Huffman
    bw.Put(static_cast<std::uint32_t>(hlit - 257), 5);
    bw.Put(static_cast<std::uint32_t>(hdist - 1), 5);
    bw.Put(static_cast<std::uint32_t>(hclen - 4), 4);
    for (int i = 0; i < hclen; ++i) bw.Put(cl[order[i]], 3);
    for (const Rle& r : rle) {
        bw.PutCode(cc[r.sym], cl[r.sym]);
        if (r.sym == 16) bw.Put(r.extra, 2);
        else if (r.sym == 17) bw.Put(r.extra, 3);
        else if (r.sym == 18) bw.Put(r.extra, 7);
    }
    for (const Token& t : toks) {
        if (t.dist == 0) { bw.PutCode(lc[t.lenOrLit], ll[t.lenOrLit]); continue; }
        int c = LenCode(t.lenOrLit);
        bw.PutCode(lc[257 + c], ll[257 + c]);
        if (kLenExtra[c]) bw.Put(static_cast<std::uint32_t>(t.lenOrLit - kLenBase[c]), kLenExtra[c]);
        int d = DistCode(t.dist);
        bw.PutCode(dc[d], dl[d]);
        if (kDistExtra[d]) bw.Put(static_cast<std::uint32_t>(t.dist - kDistBase[d]), kDistExtra[d]);
    }
    bw.PutCode(lc[256], ll[256]);
}

void WriteStored(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t size) {
    std::size_t pos = 0;
    do {
        std::size_t n = std::min<std::size_t>(size - pos, 65535);
        const bool last = pos + n >= size;
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(n & 0xFF)); out.push_back(static_cast<std::uint8_t>(n >> 8));
        out.push_back(static_cast<std::uint8_t>(~n & 0xFF)); out.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        out.insert(out.end(), data + pos, data + pos + n);
        pos += n;
    } while (pos < size);
}

void PutBE32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24)); v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8)); v.push_back(static_cast<std::uint8_t>(x));
}

void PutChunk(std::vector<std::uint8_t>& out, const char type[4], const std::uint8_t* data, std::size_t size) {
    PutBE32(out, static_cast<std::uint32_t>(size));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (size) out.insert(out.end(), data, data + size);
    PutBE32(out, Crc32(out.data() + start, out.size() - start));
}

} // namespace

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    const std::uint32_t* t = CrcTable();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = t[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::uint32_t Adler32(const std::uint8_t* data, std::size_t size, std::uint32_t adler) {
    std::uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (size > 0) {
        const std::size_t n = std::min<std::size_t>(size, 5552);   // largest run before the modulo can overflow
        for (std::size_t i = 0; i < n; ++i) { a += data[i]; b += a; }
        a %= 65521u; b %= 65521u;
        data += n; size -= n;
    }
    return (b << 16) | a;
}

std::vector<std::uint8_t> Deflate(const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> out;
    if (size == 0) { WriteStored(out, data, 0); return out; }

    constexpr int kWindow = 32768, kHashBits = 15, kMaxChain = 48, kMinMatch = 3, kMaxMatch = 258;
    constexpr std::size_t kBlockTokens = 1u << 16;
    std::vector<std::int32_t> head(static_cast<std::size_t>(1) << kHashBits, -1), prev(size, -1);
    auto hashAt = [&](std::size_t p) {
        return ((static_cast<std::uint32_t>(data[p]) << 10) ^ (static_cast<std::uint32_t>(data[p + 1]) << 5) ^ data[p + 2]) * 2654435761u >> (32 - kHashBits);
    };
    auto insert = [&](std::size_t p) {
        if (p + kMinMatch > size) return;
        const std::uint32_t h = hashAt(p);
        prev[p] = head[h]; head[h] = static_cast<std::int32_t>(p);
    };

    BitWriter bw(out);
    std::vector<Token> toks;
    toks.reserve(kBlockTokens);
    std::size_t blockStart = 0, pos = 0;
    auto flushBlock = [&](bool last) {
        const std::size_t before = out.size();
        const std::uint32_t savedAcc = bw.acc; const int savedBits = bw.nbits;
        WriteBlock(bw, toks, last);
        // Incompressible block: a stored copy is smaller. Roll the bit writer back and emit it instead.
        const std::size_t rawLen = pos - blockStart;
        if (out.size() - before > rawLen + 5 * (rawLen / 65535 + 1)) {
            out.resize(before); bw.acc = savedAcc; bw.nbits = savedBits;
            std::size_t p = blockStart;
            do {
                const std::size_t n = std::min<std::size_t>(pos - p, 65535);
                const bool lastPiece = p + n >= pos;
                bw.Put((last && lastPiece) ? 1u : 0u, 1); bw.Put(0u, 2); bw.Flush();
                out.push_back(static_cast<std::uint8_t>(n & 0xFF)); out.push_back(static_cast<std::uint8_t>(n >> 8));
                out.push_back(static_cast<std::uint8_t>(~n & 0xFF)); out.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
                out.insert(out.end(), data + p, data + p + n);
                p += n;
            } while (p < pos);
        }
        toks.clear();
        blockStart = pos;
    };

    while (pos < size) {
        int bestLen = 0, bestDist = 0;
        if (pos + kMinMatch <= size) {
            const std::size_t maxLen = std::min<std::size_t>(kMaxMatch, size - pos);
            std::int32_t cand = head[hashAt(pos)];
            int chain = kMaxChain;
            while (cand >= 0 && chain-- > 0) {
                const std::size_t dist = pos - static_cast<std::size_t>(cand);
                if (dist > static_cast<std::size_t>(kWindow)) break;
                std::size_t l = 0;
                while (l < maxLen && data[static_cast<std::size_t>(cand) + l] == data[pos + l]) ++l;
                if (static_cast<int>(l) > bestLen) { bestLen = static_cast<int>(l); bestDist = static_cast<int>(dist); if (l == maxLen) break; }
                cand = prev[static_cast<std::size_t>(cand)];
            }
        }
        if (bestLen >= kMinMatch) {
            toks.push_back({static_cast<std::uint16_t>(bestLen), static_cast<std::uint16_t>(bestDist)});
            for (int i = 0; i < bestLen; ++i) insert(pos + static_cast<std::size_t>(i));
            pos += static_cast<std::size_t>(bestLen);
        } else {
            toks.push_back({data[pos], 0});
            insert(pos);
            ++pos;
        }
        if (toks.size() >= kBlockTokens && pos < size) flushBlock(false);
    }
    flushBlock(true);
    bw.Flush();
    return out;
}

std::vector<std::uint8_t> ZlibCompress(const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> out;
    out.push_back(0x78); out.push_back(0x9C);   // CM=8, 32K window, default level; (0x789C % 31 == 0)
    std::vector<std::uint8_t> body = Deflate(data, size);
    out.insert(out.end(), body.begin(), body.end());
    PutBE32(out, Adler32(data, size));
    return out;
}

std::vector<std::uint8_t> EncodePNG(int width, int height, int channels, const std::uint8_t* pixels) {
    if (width <= 0 || height <= 0 || channels < 1 || channels > 4 || !pixels) return {};
    const std::size_t w = static_cast<std::size_t>(width), h = static_cast<std::size_t>(height), bpp = static_cast<std::size_t>(channels);
    if (w > 0x7FFFFFFFu / (bpp + 1) || h > 0x7FFFFFFFu) return {};
    const std::size_t stride = w * bpp;
    if (h > (std::size_t(1) << 40) / (stride + 1)) return {};

    // Filter each row with whichever of the five PNG filters gives the smallest sum of
    // absolute (signed) residuals, the usual heuristic.
    std::vector<std::uint8_t> raw(h * (stride + 1));
    std::vector<std::uint8_t> cand[5];
    for (auto& c : cand) c.resize(stride);
    const std::vector<std::uint8_t> zeroRow(stride, 0);
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t* cur = pixels + y * stride;
        const std::uint8_t* up = y ? pixels + (y - 1) * stride : zeroRow.data();
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0, b = up[i], c = i >= bpp ? up[i - bpp] : 0;
            int p = a + b - c;
            const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            const int paeth = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            cand[0][i] = cur[i];
            cand[1][i] = static_cast<std::uint8_t>(cur[i] - a);
            cand[2][i] = static_cast<std::uint8_t>(cur[i] - b);
            cand[3][i] = static_cast<std::uint8_t>(cur[i] - ((a + b) >> 1));
            cand[4][i] = static_cast<std::uint8_t>(cur[i] - paeth);
        }
        int best = 0; std::uint64_t bestScore = ~std::uint64_t(0);
        for (int f = 0; f < 5; ++f) {
            std::uint64_t s = 0;
            for (std::size_t i = 0; i < stride; ++i) { const int v = static_cast<std::int8_t>(cand[f][i]); s += static_cast<std::uint64_t>(v < 0 ? -v : v); }
            if (s < bestScore) { bestScore = s; best = f; }
        }
        std::uint8_t* dst = raw.data() + y * (stride + 1);
        dst[0] = static_cast<std::uint8_t>(best);
        std::memcpy(dst + 1, cand[best].data(), stride);
    }

    static const std::uint8_t colorType[5] = {0, 0, 4, 2, 6};
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    PutBE32(ihdr, static_cast<std::uint32_t>(width)); PutBE32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8); ihdr.push_back(colorType[channels]); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    PutChunk(out, "IHDR", ihdr.data(), ihdr.size());
    const std::vector<std::uint8_t> z = ZlibCompress(raw.data(), raw.size());
    PutChunk(out, "IDAT", z.data(), z.size());
    PutChunk(out, "IEND", nullptr, 0);
    return out;
}

bool WritePNG(const std::string& path, int width, int height, int channels, const std::uint8_t* pixels) {
    const std::vector<std::uint8_t> png = EncodePNG(width, height, channels, pixels);
    if (png.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace ImageWrite
} // namespace okay
