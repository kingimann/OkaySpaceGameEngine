#pragma once
// Test-only WebP encoders, so the decoder tests need no downloaded files: a lossless (VP8L) encoder with every
// transform, the colour cache, LZ77 references and meta prefix codes; a lossy (VP8 key frame) encoder with every
// intra mode, segments, token partitions, skipped macroblocks and probability updates, which also returns its own
// (unfiltered) reconstruction; and writers for ALPH, VP8X and animation chunks.
// Written from RFC 9649 and RFC 6386 like the decoder, but as a separate model: the encoder computes its predictions
// pixel by pixel from the formulas, so a decoder bug does not hide behind a shared helper. Not optimised and not a
// good compressor: it only has to produce valid streams that use the features.
#include "ImageDecodeWebpTables.hpp"   // the RFC 6386 probability and quantiser tables (data)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace webptest {

using Bytes = std::vector<std::uint8_t>;

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return std::uint32_t(s >> 11); }
    int Below(int n) { return n > 0 ? int(Next() % std::uint32_t(n)) : 0; }
};

inline void PutLe(Bytes& b, std::uint64_t v, int n) { for (int i = 0; i < n; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
inline void AddChunk(Bytes& b, const char* tag, const Bytes& payload) {
    b.insert(b.end(), tag, tag + 4);
    PutLe(b, std::uint32_t(payload.size()), 4);
    b.insert(b.end(), payload.begin(), payload.end());
    if (payload.size() & 1) b.push_back(0);
}
inline Bytes Riff(const Bytes& chunks) {
    Bytes b = {'R', 'I', 'F', 'F'};
    PutLe(b, std::uint32_t(chunks.size() + 4), 4);
    b.insert(b.end(), {'W', 'E', 'B', 'P'});
    b.insert(b.end(), chunks.begin(), chunks.end());
    return b;
}

// =====================================================================================================
// Lossless
// =====================================================================================================

struct LsbWriter {   // least significant bit first
    Bytes out;
    std::uint64_t acc = 0;
    int n = 0;
    void Put(std::uint32_t v, int bits) {
        for (int i = 0; i < bits; ++i) {
            acc |= std::uint64_t((v >> i) & 1u) << n;
            if (++n == 8) { out.push_back(std::uint8_t(acc)); acc = 0; n = 0; }
        }
    }
    Bytes Finish() { if (n) { out.push_back(std::uint8_t(acc)); acc = 0; n = 0; } return out; }
};

// A complete prefix code for the symbols that occur. Lengths are near-uniform (every used symbol gets floor or ceil
// of log2(count)), assigned canonically. One used symbol: a zero-bit code.
struct LLCode {
    std::vector<std::uint8_t> len;
    std::vector<std::uint32_t> bits;   // code bits, most significant first
    int single = -1;
    void Write(LsbWriter& w, int sym) const {
        if (single >= 0) return;
        for (int i = len[std::size_t(sym)] - 1; i >= 0; --i) w.Put((bits[std::size_t(sym)] >> i) & 1u, 1);
    }
};

inline LLCode MakePrefixCode(const std::vector<std::uint32_t>& hist) {
    LLCode c;
    const int n = int(hist.size());
    c.len.assign(std::size_t(n), 0);
    c.bits.assign(std::size_t(n), 0);
    std::vector<int> used;
    for (int s = 0; s < n; ++s) if (hist[std::size_t(s)]) used.push_back(s);
    if (used.empty()) used.push_back(0);
    if (used.size() == 1) { c.single = used[0]; c.len[std::size_t(used[0])] = 1; return c; }
    int L = 0;
    while ((1u << L) < used.size()) ++L;
    const int shortOnes = int((1u << L) - used.size());   // that many get L - 1 bits, keeping the code complete
    // Give the shorter codes to the most frequent symbols.
    std::vector<int> byFreq = used;
    // (std::sort with a tie-break, not stable_sort: test_image_hostile replaces operator new, and stable_sort's
    // temporary buffer comes from the nothrow form it does not replace)
    std::sort(byFreq.begin(), byFreq.end(), [&](int a, int b) {
        return hist[std::size_t(a)] != hist[std::size_t(b)] ? hist[std::size_t(a)] > hist[std::size_t(b)] : a < b;
    });
    for (std::size_t i = 0; i < byFreq.size(); ++i) c.len[std::size_t(byFreq[i])] = std::uint8_t(int(i) < shortOnes ? L - 1 : L);
    std::uint32_t code = 0;
    std::uint32_t next[16] = {};
    int count[16] = {};
    for (int s = 0; s < n; ++s) ++count[c.len[std::size_t(s)]];
    count[0] = 0;
    for (int l = 1; l < 16; ++l) { code = (code + std::uint32_t(count[l - 1])) << 1; next[l] = code; }
    for (int s = 0; s < n; ++s) if (c.len[std::size_t(s)]) c.bits[std::size_t(s)] = next[c.len[std::size_t(s)]]++;
    return c;
}

constexpr std::uint8_t kLLClOrder[19] = {17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

struct LLOptions {
    bool subtractGreen = false, predictor = false, crossColor = false, palette = false;
    int predBits = 3, colorBits = 3;
    int cacheBits = 0;        // 0 = no colour cache
    bool lz77 = false;
    int metaBits = 0;         // > 0: meta prefix codes with blocks of 1 << metaBits
    int groups = 3;
    bool maxSymbol = false;   // write the optional max_symbol field
    bool simpleCodes = true;  // use the simple code length code where it applies
    std::uint64_t seed = 1;
};

// Writes one prefix code for `hist` and returns it.
inline LLCode WriteCode(LsbWriter& w, const std::vector<std::uint32_t>& hist, const LLOptions& o) {
    std::vector<int> used;
    for (std::size_t s = 0; s < hist.size(); ++s) if (hist[s]) used.push_back(int(s));
    if (o.simpleCodes && used.size() <= 2 && (used.empty() || used.back() < 256)) {
        LLCode c;
        c.len.assign(hist.size(), 0);
        c.bits.assign(hist.size(), 0);
        w.Put(1, 1);
        if (used.size() <= 1) {
            const int s0 = used.empty() ? 0 : used[0];
            w.Put(0, 1);
            const int first8 = s0 > 1;
            w.Put(std::uint32_t(first8), 1);
            w.Put(std::uint32_t(s0), first8 ? 8 : 1);
            c.single = s0;
            return c;
        }
        w.Put(1, 1);
        const int first8 = used[0] > 1 || (o.seed & 1);   // sometimes 8 bits even when 1 would do
        w.Put(std::uint32_t(first8), 1);
        w.Put(std::uint32_t(used[0]), first8 ? 8 : 1);
        w.Put(std::uint32_t(used[1]), 8);
        c.len[std::size_t(used[0])] = c.len[std::size_t(used[1])] = 1;
        c.bits[std::size_t(used[0])] = 0;
        c.bits[std::size_t(used[1])] = 1;
        return c;
    }
    LLCode c = MakePrefixCode(hist);
    w.Put(0, 1);
    // Code lengths as code-length symbols: 17 / 18 for zero runs, 16 to repeat the previous non-zero length.
    struct Tok { int sym, extra, ebits; };
    std::vector<Tok> toks;
    int n = int(c.len.size());
    while (n > 0 && c.len[std::size_t(n - 1)] == 0) --n;   // trailing zeros only when max_symbol is written
    if (!o.maxSymbol) n = int(c.len.size());
    int prevNz = 8;
    for (int i = 0; i < n;) {
        const int v = c.len[std::size_t(i)];
        int run = 1;
        while (i + run < n && c.len[std::size_t(i + run)] == v) ++run;
        if (v == 0 && run >= 3) {
            const int take = std::min(run, 138);
            if (take >= 11) toks.push_back({18, take - 11, 7});
            else toks.push_back({17, take - 3, 3});
            i += take;
            continue;
        }
        if (v != 0 && v == prevNz && run >= 3) {
            const int take = std::min(run, 6);
            toks.push_back({16, take - 3, 2});
            i += take;
            continue;
        }
        toks.push_back({v, 0, 0});
        if (v) prevNz = v;
        ++i;
    }
    const bool writeMax = o.maxSymbol && toks.size() >= 2;
    if (!writeMax) {
        // the full alphabet is read: spell out any trimmed trailing zeros
        for (int i = n; i < int(c.len.size());) {
            const int take = std::min(int(c.len.size()) - i, 138);
            if (take >= 11) toks.push_back({18, take - 11, 7});
            else if (take >= 3) toks.push_back({17, take - 3, 3});
            else toks.push_back({0, 0, 0});
            i += take >= 3 ? take : 1;
        }
    }
    std::vector<std::uint32_t> clHist(19, 0);
    for (const Tok& t : toks) ++clHist[std::size_t(t.sym)];
    LLCode cl = MakePrefixCode(clHist);
    int numCl = 19;
    while (numCl > 4 && cl.len[kLLClOrder[numCl - 1]] == 0) --numCl;
    w.Put(std::uint32_t(numCl - 4), 4);
    for (int i = 0; i < numCl; ++i) w.Put(cl.len[kLLClOrder[i]], 3);
    w.Put(writeMax ? 1u : 0u, 1);
    if (writeMax) {
        const std::uint32_t m = std::uint32_t(toks.size() - 2);
        int k = 0;
        while (k < 7 && (m >> (2 + 2 * k)) != 0) ++k;
        w.Put(std::uint32_t(k), 3);
        w.Put(m, 2 + 2 * k);
    }
    for (const Tok& t : toks) { cl.Write(w, t.sym); if (t.ebits) w.Put(std::uint32_t(t.extra), t.ebits); }
    return c;
}

// The value -> (prefix code, extra bits) split of RFC 9649 3.6.2.2.
inline void ValueToPrefix(std::uint32_t value, int& code, int& extraBits, std::uint32_t& extra) {
    if (value <= 4) { code = int(value) - 1; extraBits = 0; extra = 0; return; }
    const std::uint32_t v = value - 1;
    int hb = 31;
    while (!((v >> hb) & 1u)) --hb;
    const int second = int((v >> (hb - 1)) & 1u);
    extraBits = hb - 1;
    extra = v & ((1u << extraBits) - 1u);
    code = 2 * hb + second;
}

inline int Ch(std::uint32_t v, int sh) { return int((v >> sh) & 0xFF); }
inline std::uint32_t SubPx(std::uint32_t a, std::uint32_t b) {
    std::uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) r |= std::uint32_t((Ch(a, sh) - Ch(b, sh)) & 0xFF) << sh;
    return r;
}
inline std::uint32_t AvgPx(std::uint32_t a, std::uint32_t b) {
    std::uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) r |= std::uint32_t((Ch(a, sh) + Ch(b, sh)) / 2) << sh;
    return r;
}
inline int Clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline std::uint32_t PredictModel(int mode, std::uint32_t L, std::uint32_t T, std::uint32_t TR, std::uint32_t TL) {
    std::uint32_t r = 0;
    switch (mode) {
        case 0: return 0xFF000000u;
        case 1: return L;
        case 2: return T;
        case 3: return TR;
        case 4: return TL;
        case 5: return AvgPx(AvgPx(L, TR), T);
        case 6: return AvgPx(L, TL);
        case 7: return AvgPx(L, T);
        case 8: return AvgPx(TL, T);
        case 9: return AvgPx(T, TR);
        case 10: return AvgPx(AvgPx(L, TL), AvgPx(T, TR));
        case 11: {
            int dl = 0, dt = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                const int p = Ch(L, sh) + Ch(T, sh) - Ch(TL, sh);
                dl += std::abs(p - Ch(L, sh));
                dt += std::abs(p - Ch(T, sh));
            }
            return dl < dt ? L : T;
        }
        case 12:
            for (int sh = 0; sh < 32; sh += 8) r |= std::uint32_t(Clamp8(Ch(L, sh) + Ch(T, sh) - Ch(TL, sh))) << sh;
            return r;
        default: {
            const std::uint32_t a = AvgPx(L, T);
            for (int sh = 0; sh < 32; sh += 8) r |= std::uint32_t(Clamp8(Ch(a, sh) + (Ch(a, sh) - Ch(TL, sh)) / 2)) << sh;
            return r;
        }
    }
}
inline int CtDeltaModel(int t, int c) { return (int(std::int8_t(std::uint8_t(t))) * int(std::int8_t(std::uint8_t(c)))) >> 5; }

// Writes one entropy-coded image (colour cache info, optional meta codes when `top`, the codes, the pixels).
inline void WriteImageData(LsbWriter& w, const std::vector<std::uint32_t>& px, int width, int height, bool top, const LLOptions& o,
                           Rng& rng) {
    const int cacheBits = o.cacheBits;
    w.Put(cacheBits ? 1u : 0u, 1);
    if (cacheBits) w.Put(std::uint32_t(cacheBits), 4);
    int metaBits = 0, metaW = 0, groups = 1;
    std::vector<int> groupOf;
    if (top) {
        w.Put(o.metaBits ? 1u : 0u, 1);
        if (o.metaBits) {
            metaBits = o.metaBits;
            groups = std::max(1, o.groups);
            w.Put(std::uint32_t(metaBits - 2), 3);
            metaW = (width + (1 << metaBits) - 1) >> metaBits;
            const int metaH = (height + (1 << metaBits) - 1) >> metaBits;
            std::vector<std::uint32_t> meta(std::size_t(metaW) * std::size_t(metaH));
            groupOf.resize(meta.size());
            for (std::size_t i = 0; i < meta.size(); ++i) {
                groupOf[i] = rng.Below(groups);
                meta[i] = std::uint32_t(groupOf[i]) << 8;
            }
            groupOf[0] = groups - 1;   // the largest index is used, so the decoder sees `groups` groups
            meta[0] = std::uint32_t(groups - 1) << 8;
            LLOptions sub = o;
            sub.cacheBits = 0;
            sub.lz77 = false;
            WriteImageData(w, meta, metaW, metaH, false, sub, rng);
        }
    }
    // Tokens: literal, cache index or backward reference.
    struct Tok { int kind; std::uint32_t px; int cache; std::uint32_t len, dcode; int group; };
    std::vector<Tok> toks;
    const std::size_t total = px.size();
    std::vector<std::uint32_t> cache(std::size_t(1) << (cacheBits ? cacheBits : 0), 0);
    auto hashOf = [&](std::uint32_t p) { return int((0x1e35a7bdu * p) >> (32 - cacheBits)); };
    std::map<long long, int> nearCode;   // dist -> short distance code
    const auto& kNearMap = okay::imgdec::vp8tab::kLosslessDistanceMap;   // the 120 short distance codes (RFC 9649)
    for (int i = 0; i < 120; ++i) {
        long long d = kNearMap[i][0] + (long long)kNearMap[i][1] * width;
        if (d < 1) d = 1;
        if (!nearCode.count(d)) nearCode[d] = i + 1;
    }
    std::size_t pos = 0;
    auto groupAt = [&](std::size_t p) {
        if (!metaBits) return 0;
        const int x = int(p % std::size_t(width)), y = int(p / std::size_t(width));
        return groupOf[std::size_t(y >> metaBits) * std::size_t(metaW) + std::size_t(x >> metaBits)];
    };
    while (pos < total) {
        const int g = groupAt(pos);
        if (o.lz77) {
            std::uint32_t bestLen = 0;
            long long bestDist = 0;
            const long long cands[] = {1, 2, width, (long long)width - 1, (long long)width + 1, 2LL * width, 3, 7, 200, 5000};
            for (long long d : cands) {
                if (d < 1 || d > (long long)pos) continue;
                std::uint32_t l = 0;
                while (pos + l < total && l < 4096 && px[pos + l] == px[pos + l - std::size_t(d)]) ++l;
                if (l > bestLen) { bestLen = l; bestDist = d; }
            }
            if (bestLen >= 3) {
                if (rng.Below(4) == 0 && bestLen > 3) bestLen = 3 + std::uint32_t(rng.Below(int(bestLen - 3)));
                auto it = nearCode.find(bestDist);
                std::uint32_t dcode = (it != nearCode.end() && rng.Below(3)) ? std::uint32_t(it->second) : std::uint32_t(bestDist + 120);
                toks.push_back({2, 0, 0, bestLen, dcode, g});
                for (std::uint32_t k = 0; k < bestLen; ++k) if (cacheBits) cache[std::size_t(hashOf(px[pos + k]))] = px[pos + k];
                pos += bestLen;
                continue;
            }
        }
        const std::uint32_t p = px[pos];
        if (cacheBits && cache[std::size_t(hashOf(p))] == p) {
            toks.push_back({1, p, hashOf(p), 0, 0, g});
        } else {
            toks.push_back({0, p, 0, 0, 0, g});
            if (cacheBits) cache[std::size_t(hashOf(p))] = p;
        }
        ++pos;
    }
    // Histograms per group and code: green+length+cache, red, blue, alpha, distance.
    const int cacheSize = cacheBits ? (1 << cacheBits) : 0;
    std::vector<std::vector<std::uint32_t>> hist(std::size_t(groups) * 5);
    for (int gi = 0; gi < groups; ++gi) {
        hist[std::size_t(gi) * 5 + 0].assign(std::size_t(256 + 24 + cacheSize), 0);
        for (int k = 1; k < 4; ++k) hist[std::size_t(gi) * 5 + std::size_t(k)].assign(256, 0);
        hist[std::size_t(gi) * 5 + 4].assign(40, 0);
    }
    for (const Tok& t : toks) {
        auto* h = &hist[std::size_t(t.group) * 5];
        if (t.kind == 0) { ++h[0][std::size_t(Ch(t.px, 8))]; ++h[1][std::size_t(Ch(t.px, 16))]; ++h[2][std::size_t(Ch(t.px, 0))]; ++h[3][std::size_t(Ch(t.px, 24))]; }
        else if (t.kind == 1) ++h[0][std::size_t(256 + 24 + t.cache)];
        else {
            int c, eb; std::uint32_t ex;
            ValueToPrefix(t.len, c, eb, ex); ++h[0][std::size_t(256 + c)];
            ValueToPrefix(t.dcode, c, eb, ex); ++h[4][std::size_t(c)];
        }
    }
    std::vector<LLCode> codes(hist.size());
    for (std::size_t i = 0; i < hist.size(); ++i) codes[i] = WriteCode(w, hist[i], o);
    for (const Tok& t : toks) {
        const LLCode* c = &codes[std::size_t(t.group) * 5];
        if (t.kind == 0) { c[0].Write(w, Ch(t.px, 8)); c[1].Write(w, Ch(t.px, 16)); c[2].Write(w, Ch(t.px, 0)); c[3].Write(w, Ch(t.px, 24)); }
        else if (t.kind == 1) c[0].Write(w, 256 + 24 + t.cache);
        else {
            int pc, eb; std::uint32_t ex;
            ValueToPrefix(t.len, pc, eb, ex); c[0].Write(w, 256 + pc); w.Put(ex, eb);
            ValueToPrefix(t.dcode, pc, eb, ex); c[4].Write(w, pc); w.Put(ex, eb);
        }
    }
}

// Transforms and image data, no header (the body of a VP8L chunk after its 5 header bytes, or of a compressed ALPH).
inline void WriteImageStream(LsbWriter& w, std::vector<std::uint32_t> px, int width, int height, const LLOptions& o) {
    Rng rng(o.seed);
    LLOptions sub = o;
    sub.metaBits = 0;
    sub.lz77 = o.lz77;
    int xs = width;
    if (o.palette) {
        std::vector<std::uint32_t> pal;
        for (std::uint32_t p : px) if (std::find(pal.begin(), pal.end(), p) == pal.end() && pal.size() <= 256) pal.push_back(p);
        if (pal.size() <= 256) {
            const int n = int(pal.size());
            const int wb = n <= 2 ? 3 : (n <= 4 ? 2 : (n <= 16 ? 1 : 0));
            w.Put(1, 1); w.Put(3, 2); w.Put(std::uint32_t(n - 1), 8);
            std::vector<std::uint32_t> delta(pal.size());
            for (std::size_t i = 0; i < pal.size(); ++i) delta[i] = i ? SubPx(pal[i], pal[i - 1]) : pal[0];
            WriteImageData(w, delta, n, 1, false, sub, rng);
            const int nx = (xs + (1 << wb) - 1) >> wb, per = 8 >> wb;
            std::vector<std::uint32_t> packed(std::size_t(nx) * std::size_t(height), 0xFF000000u);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < xs; ++x) {
                    const int idx = int(std::find(pal.begin(), pal.end(), px[std::size_t(y) * std::size_t(xs) + std::size_t(x)]) - pal.begin());
                    packed[std::size_t(y) * std::size_t(nx) + std::size_t(x >> wb)] |= std::uint32_t(idx << ((x & ((1 << wb) - 1)) * per)) << 8;
                }
            px.swap(packed);
            xs = nx;
        }
    }
    if (o.subtractGreen) {
        w.Put(1, 1); w.Put(2, 2);
        for (std::uint32_t& p : px) {
            const int g = Ch(p, 8);
            p = (p & 0xFF00FF00u) | (std::uint32_t((Ch(p, 16) - g) & 0xFF) << 16) | std::uint32_t((Ch(p, 0) - g) & 0xFF);
        }
    }
    if (o.predictor) {
        const int bits = o.predBits, bw = (xs + (1 << bits) - 1) >> bits, bh = (height + (1 << bits) - 1) >> bits;
        std::vector<std::uint32_t> modes(std::size_t(bw) * std::size_t(bh));
        for (std::size_t i = 0; i < modes.size(); ++i) modes[i] = 0xFF000000u | (std::uint32_t(i % 14) << 8);   // every mode in turn
        w.Put(1, 1); w.Put(0, 2); w.Put(std::uint32_t(bits - 2), 3);
        WriteImageData(w, modes, bw, bh, false, sub, rng);
        std::vector<std::uint32_t> res(px.size());
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < xs; ++x) {
                const std::size_t i = std::size_t(y) * std::size_t(xs) + std::size_t(x);
                std::uint32_t pred;
                if (y == 0) pred = x == 0 ? 0xFF000000u : px[i - 1];
                else if (x == 0) pred = px[i - std::size_t(xs)];
                else {
                    const int mode = Ch(modes[std::size_t(y >> bits) * std::size_t(bw) + std::size_t(x >> bits)], 8);
                    const std::uint32_t TR = x == xs - 1 ? px[std::size_t(y) * std::size_t(xs)] : px[i - std::size_t(xs) + 1];
                    pred = PredictModel(mode, px[i - 1], px[i - std::size_t(xs)], TR, px[i - std::size_t(xs) - 1]);
                }
                res[i] = SubPx(px[i], pred);
            }
        px.swap(res);
    }
    if (o.crossColor) {
        const int bits = o.colorBits, bw = (xs + (1 << bits) - 1) >> bits, bh = (height + (1 << bits) - 1) >> bits;
        std::vector<std::uint32_t> el(std::size_t(bw) * std::size_t(bh));
        for (std::uint32_t& e : el) e = 0xFF000000u | (rng.Next() & 0xFFFFFFu);
        w.Put(1, 1); w.Put(1, 2); w.Put(std::uint32_t(bits - 2), 3);
        WriteImageData(w, el, bw, bh, false, sub, rng);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < xs; ++x) {
                const std::uint32_t e = el[std::size_t(y >> bits) * std::size_t(bw) + std::size_t(x >> bits)];
                std::uint32_t& p = px[std::size_t(y) * std::size_t(xs) + std::size_t(x)];
                const int g = Ch(p, 8), r = Ch(p, 16), b = Ch(p, 0);
                const int nr = (r - CtDeltaModel(Ch(e, 0), g)) & 0xFF;
                const int nb = (b - CtDeltaModel(Ch(e, 8), g) - CtDeltaModel(Ch(e, 16), r)) & 0xFF;
                p = (p & 0xFF00FF00u) | (std::uint32_t(nr) << 16) | std::uint32_t(nb);
            }
    }
    w.Put(0, 1);   // no more transforms
    WriteImageData(w, px, xs, height, true, o, rng);
}

// A whole VP8L chunk payload. `argb` holds 0xAARRGGBB pixels.
inline Bytes EncodeVp8l(const std::vector<std::uint32_t>& argb, int width, int height, const LLOptions& o) {
    LsbWriter w;
    w.Put(0x2F, 8);
    w.Put(std::uint32_t(width - 1), 14);
    w.Put(std::uint32_t(height - 1), 14);
    w.Put(1, 1);   // alpha_is_used (a hint)
    w.Put(0, 3);
    WriteImageStream(w, argb, width, height, o);
    return w.Finish();
}

// ALPH chunk payload: `filter` 0..3, `compressed`: VP8L green channel, else raw bytes.
inline Bytes EncodeAlph(const std::vector<std::uint8_t>& alpha, int width, int height, int filter, bool compressed, const LLOptions& o) {
    std::vector<std::uint8_t> res(alpha.size());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(width) + std::size_t(x);
            int pred = 0;
            if (y == 0 && x == 0) pred = 0;
            else if (y == 0) pred = alpha[i - 1];
            else if (x == 0) pred = alpha[i - std::size_t(width)];
            else if (filter == 1) pred = alpha[i - 1];
            else if (filter == 2) pred = alpha[i - std::size_t(width)];
            else if (filter == 3) pred = Clamp8(alpha[i - 1] + alpha[i - std::size_t(width)] - alpha[i - std::size_t(width) - 1]);
            if (filter == 0) pred = 0;
            res[i] = std::uint8_t(alpha[i] - pred);
        }
    Bytes out;
    out.push_back(std::uint8_t((filter << 2) | (compressed ? 1 : 0)));
    if (!compressed) { out.insert(out.end(), res.begin(), res.end()); return out; }
    std::vector<std::uint32_t> g(res.size());
    for (std::size_t i = 0; i < res.size(); ++i) g[i] = 0xFF000000u | (std::uint32_t(res[i]) << 8);
    LsbWriter w;
    WriteImageStream(w, g, width, height, o);
    const Bytes body = w.Finish();
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// =====================================================================================================
// Lossy (VP8 key frame)
// =====================================================================================================

// Boolean entropy encoder (RFC 6386 section 7). `low` keeps the bits of the code value not yet written, `pending` of
// them below the byte that lines up with `range`; a carry out of `low` is added into the bytes already written.
struct BoolEncoder {
    Bytes out;
    std::uint32_t range = 255;
    std::uint64_t low = 0;
    int pending = 0;
    void Carry() {
        for (std::size_t i = out.size(); i-- > 0;) { if (++out[i] != 0) return; }
    }
    void Put(int bit, int prob) {
        const std::uint32_t split = 1 + (((range - 1) * std::uint32_t(prob)) >> 8);
        if (bit) { low += split; range -= split; } else range = split;
        if (low >> (8 + pending)) { Carry(); low &= (std::uint64_t(1) << (8 + pending)) - 1; }
        while (range < 128) {
            range <<= 1;
            low <<= 1;
            if (low >> (8 + pending + 1)) { Carry(); low &= (std::uint64_t(1) << (8 + pending + 1)) - 1; }
            if (++pending == 8) { out.push_back(std::uint8_t(low >> 8)); low &= 0xFF; pending = 0; }
        }
    }
    void Literal(std::uint32_t v, int n) { for (int i = n - 1; i >= 0; --i) Put(int((v >> i) & 1u), 128); }
    void Signed(int v, int n) { Literal(std::uint32_t(std::abs(v)), n); Put(v < 0, 128); }
    void MaybeSigned(int v, int n) { Put(v != 0, 128); if (v) Signed(v, n); }
    // Writes `leaf` of `tree` (entries <= 0 are leaves) starting at index `start`.
    void Tree(const std::int8_t* tree, int treeLen, const std::uint8_t* probs, int leaf, int start = 0) {
        std::vector<std::pair<int, int>> path;   // (node index, bit)
        auto find = [&](auto& self, int i) -> bool {
            for (int b = 0; b < 2; ++b) {
                const int t = tree[i + b];
                path.push_back({i, b});
                if (t <= 0 ? -t == leaf : (t < treeLen && self(self, t))) return true;
                path.pop_back();
            }
            return false;
        };
        find(find, start);
        for (const auto& pb : path) Put(pb.second, probs[pb.first >> 1]);
    }
    Bytes Finish() {
        // the remaining 8 + pending bits, left-aligned into whole bytes
        const int bits = 8 + pending;
        std::uint64_t v = low << (16 - bits);
        out.push_back(std::uint8_t(v >> 8));
        out.push_back(std::uint8_t(v));
        return out;
    }
};

constexpr std::int8_t kTSegment[6] = {2, 4, -0, -1, -2, -3};
constexpr std::int8_t kTYmode[8] = {-4, 2, 4, 6, -0, -1, -2, -3};
constexpr std::int8_t kTUv[6] = {-0, 2, -1, 4, -2, -3};
constexpr std::int8_t kTBmode[18] = {-0, 2, -1, 4, -2, 6, 8, 12, -3, 10, -5, -6, -4, 14, -7, 16, -8, -9};
constexpr std::int8_t kTCoeff[22] = {-11, 2, -0, 4, -1, 6, 8, 12, -2, 10, -3, -4, 14, 16, -5, -6, 18, 20, -7, -8, -9, -10};
constexpr std::uint8_t kPYmode[4] = {145, 156, 163, 128};
constexpr std::uint8_t kPUv[3] = {142, 114, 183};
constexpr std::uint8_t kBands[16] = {0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7};
constexpr std::uint8_t kZz[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};
constexpr std::uint8_t kCatP[6][11] = {{159}, {165, 145}, {173, 148, 140}, {176, 155, 140, 135}, {180, 157, 141, 134, 130},
                                       {254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129}};
constexpr int kCatBits[6] = {1, 2, 3, 4, 5, 11};
constexpr int kCatStart[6] = {5, 7, 11, 19, 35, 67};

struct LossyOptions {
    int q = 40;
    int dYdc = 0, dY2dc = 0, dY2ac = 0, dUVdc = 0, dUVac = 0;
    bool simpleFilter = false;
    int level = 0, sharpness = 0;
    bool lfDeltas = false;
    int refDelta0 = 0, modeDeltaB = 0;
    int log2Parts = 0;
    bool segments = false, segAbs = false, segUpdateMap = true;
    int segQ[4] = {0, 0, 0, 0}, segLf[4] = {0, 0, 0, 0};
    std::uint8_t segProbs[3] = {128, 128, 128};
    bool skip = true;
    bool probUpdates = false;
    int modes = 0;   // 0: every mode in turn; 1: B_PRED only; 2: 16x16 modes only
    std::uint64_t seed = 3;
};

struct Planes { int w = 0, h = 0; Bytes y, u, v; };

// Edge-padded copy of a plane to whole macroblocks.
inline Bytes PadPlane(const Bytes& p, int w, int h, int pw, int ph) {
    Bytes out(std::size_t(pw) * std::size_t(ph));
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x) out[std::size_t(y) * std::size_t(pw) + std::size_t(x)] = p[std::size_t(std::min(y, h - 1)) * std::size_t(w) + std::size_t(std::min(x, w - 1))];
    return out;
}

inline std::uint8_t Sat(int v) { return std::uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// The inverse transforms as the RFC defines them (needed to stay in step with the decoder).
inline void ModelIdct(const int in[16], int out[16]) {
    const int c1 = 20091, s1 = 35468;
    int t[16];
    for (int i = 0; i < 4; ++i) {
        const int i0 = in[i], i1 = in[4 + i], i2 = in[8 + i], i3 = in[12 + i];
        const int a = i0 + i2, b = i0 - i2;
        const int c = ((i1 * s1) >> 16) - (i3 + ((i3 * c1) >> 16));
        const int d = (i1 + ((i1 * c1) >> 16)) + ((i3 * s1) >> 16);
        t[i] = a + d; t[4 + i] = b + c; t[8 + i] = b - c; t[12 + i] = a - d;
    }
    for (int r = 0; r < 4; ++r) {
        const int i0 = t[4 * r], i1 = t[4 * r + 1], i2 = t[4 * r + 2], i3 = t[4 * r + 3];
        const int a = i0 + i2, b = i0 - i2;
        const int c = ((i1 * s1) >> 16) - (i3 + ((i3 * c1) >> 16));
        const int d = (i1 + ((i1 * c1) >> 16)) + ((i3 * s1) >> 16);
        out[4 * r] = (a + d + 4) >> 3; out[4 * r + 1] = (b + c + 4) >> 3; out[4 * r + 2] = (b - c + 4) >> 3; out[4 * r + 3] = (a - d + 4) >> 3;
    }
}
inline void ModelIwht(const int in[16], int out[16]) {
    int t[16];
    for (int i = 0; i < 4; ++i) {
        const int i0 = in[i], i1 = in[4 + i], i2 = in[8 + i], i3 = in[12 + i];
        t[i] = i0 + i1 + i2 + i3; t[4 + i] = i0 + i1 - i2 - i3; t[8 + i] = i0 - i1 - i2 + i3; t[12 + i] = i0 - i1 + i2 - i3;
    }
    for (int r = 0; r < 4; ++r) {
        const int i0 = t[4 * r], i1 = t[4 * r + 1], i2 = t[4 * r + 2], i3 = t[4 * r + 3];
        out[4 * r] = (i0 + i1 + i2 + i3 + 3) >> 3; out[4 * r + 1] = (i0 + i1 - i2 - i3 + 3) >> 3;
        out[4 * r + 2] = (i0 - i1 - i2 + i3 + 3) >> 3; out[4 * r + 3] = (i0 - i1 + i2 - i3 + 3) >> 3;
    }
}
// Forward transforms: the inverses of the linear parts above (rows of the inverse DCT are orthogonal with squared
// norm 4, so its inverse is its transpose / 4), in integers so the test files are the same on every machine:
// sqrt(2) cos(pi/8) and sqrt(2) sin(pi/8) in 1/4096 units.
inline int RoundDiv(long long num, long long den) { return int(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den)); }
inline void ForwardDct(const int res[16], int out[16]) {
    const long long k = 5352, s = 2217, one = 4096;
    const long long M[4][4] = {{one, k, one, s}, {one, s, -one, -k}, {one, -s, -one, k}, {one, -k, one, -s}};   // pixel i = sum_j M[i][j] coef j
    for (int u = 0; u < 4; ++u)
        for (int v = 0; v < 4; ++v) {
            long long acc = 0;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) acc += M[i][u] * res[4 * i + j] * M[j][v];
            out[4 * u + v] = RoundDiv(acc, 2 * one * one);
        }
}
inline void ForwardWht(const int dc[16], int out[16]) {
    const int Hm[4][4] = {{1, 1, 1, 1}, {1, 1, -1, -1}, {1, -1, -1, 1}, {1, -1, 1, -1}};
    for (int u = 0; u < 4; ++u)
        for (int v = 0; v < 4; ++v) {
            long long acc = 0;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) acc += Hm[i][u] * dc[4 * i + j] * Hm[j][v];
            out[4 * u + v] = RoundDiv(acc, 2);
        }
}

// Intra predictions written out from the formulas of RFC 6386 section 12 (edges: `above[-1..]`, `left[0..]`).
inline void ModelPredict16(int mode, const int* above, const int* left, int corner, bool haveAbove, bool haveLeft, int n, int* out) {
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            int v;
            if (mode == 1) v = above[c];
            else if (mode == 2) v = left[r];
            else if (mode == 3) v = Sat(left[r] + above[c] - corner);
            else {
                int sum = 0, cnt = 0;
                if (haveAbove) for (int i = 0; i < n; ++i) { sum += above[i]; ++cnt; }
                if (haveLeft) for (int i = 0; i < n; ++i) { sum += left[i]; ++cnt; }
                v = cnt ? (sum + cnt / 2) / cnt : 128;
            }
            out[r * n + c] = v;
        }
}
inline void ModelPredict4(int mode, const int* A /* 8 */, const int* L /* 4 */, int P, int* out) {
    auto at = [&](int i) { return i < 0 ? P : A[i]; };            // the row above, P at -1
    auto lf = [&](int i) { return i < 0 ? P : (i > 3 ? L[3] : L[i]); };
    auto avg3 = [](int a, int b, int c) { return (a + 2 * b + c + 2) >> 2; };
    auto avg2 = [](int a, int b) { return (a + b + 1) >> 1; };
    // edge pixel k of the line running from the bottom-left (k = 0 is L[3]) through P (k = 4) to A[3] (k = 8)
    auto e = [&](int k) { return k < 4 ? L[3 - k] : (k == 4 ? P : A[k - 5]); };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            int v = 0;
            switch (mode) {
                case 0: { int s = 4; for (int i = 0; i < 4; ++i) s += A[i] + L[i]; v = s >> 3; break; }
                case 1: v = Sat(L[r] + A[c] - P); break;
                case 2: v = avg3(at(c - 1), A[c], A[c + 1]); break;
                case 3: v = r < 3 ? avg3(lf(r - 1), L[r], L[r + 1]) : avg3(L[2], L[3], L[3]); break;
                case 4: { const int k = r + c; v = k < 6 ? avg3(A[k], A[k + 1], A[k + 2]) : avg3(A[6], A[7], A[7]); break; }
                case 5: { const int k = 4 - r + c; v = avg3(e(k - 1), e(k), e(k + 1)); break; }
                case 6: {   // vertical right: lines of slope 2, z = 2c - r
                    const int z = 2 * c - r;
                    if (z >= 0 && !(z & 1)) v = avg2(e(4 + z / 2), e(5 + z / 2));
                    else if (z >= -1) { const int m = 4 + (z + 1) / 2; v = avg3(e(m - 1), e(m), e(m + 1)); }
                    else { const int m = 5 + z; v = avg3(e(m - 1), e(m), e(m + 1)); }
                    break;
                }
                case 7: {   // vertical left
                    const int k = c + (r >> 1);
                    if (r == 2 && c == 3) v = avg3(A[4], A[5], A[6]);
                    else if (r == 3 && c == 3) v = avg3(A[5], A[6], A[7]);
                    else v = (r & 1) ? avg3(A[k], A[k + 1], A[k + 2]) : avg2(A[k], A[k + 1]);
                    break;
                }
                case 8: {   // horizontal down: lines of slope 1/2, z = 2r - c
                    const int z = 2 * r - c;
                    if (z >= 0 && !(z & 1)) v = avg2(e(3 - z / 2), e(4 - z / 2));
                    else if (z >= -1) { const int m = 4 - (z + 1) / 2; v = avg3(e(m - 1), e(m), e(m + 1)); }
                    else { const int m = 3 - z; v = avg3(e(m - 1), e(m), e(m + 1)); }
                    break;
                }
                default: {   // horizontal up: z = 2r + c
                    const int z = 2 * r + c;
                    if (z > 5) v = L[3];
                    else if (z == 5) v = avg3(L[2], L[3], L[3]);
                    else if (!(z & 1)) v = avg2(L[z / 2], L[z / 2 + 1]);
                    else v = avg3(L[z / 2], L[z / 2 + 1], lf(z / 2 + 2));
                    break;
                }
            }
            out[r * 4 + c] = v;
        }
}


// Encodes planes as one VP8 key frame. `recon` receives the encoder's reconstruction (before the loop filter),
// cropped to the image.
inline Bytes EncodeVp8(const Planes& src, const LossyOptions& o, Planes* recon = nullptr) {
    namespace T = okay::imgdec::vp8tab;
    const int W = src.w, H = src.h, mbw = (W + 15) / 16, mbh = (H + 15) / 16;
    const int cw = (W + 1) / 2, ch = (H + 1) / 2;
    const Bytes sy = PadPlane(src.y, W, H, mbw * 16, mbh * 16);
    const Bytes su = PadPlane(src.u, cw, ch, mbw * 8, mbh * 8), sv = PadPlane(src.v, cw, ch, mbw * 8, mbh * 8);
    Bytes ry(sy.size(), 0), ru(su.size(), 0), rv(sv.size(), 0);
    const int sty = mbw * 16, stc = mbw * 8;
    Rng rng(o.seed);
    // probabilities, with optional updates
    std::uint8_t probs[4][8][3][11];
    std::memcpy(probs, T::kDefaultCoeffProbs, sizeof(probs));
    BoolEncoder hdr;
    hdr.Literal(0, 1); hdr.Literal(0, 1);
    hdr.Put(o.segments, 128);
    if (o.segments) {
        hdr.Put(o.segUpdateMap, 128);
        hdr.Put(1, 128);
        hdr.Put(o.segAbs, 128);
        for (int i = 0; i < 4; ++i) hdr.MaybeSigned(o.segQ[i], 7);
        for (int i = 0; i < 4; ++i) hdr.MaybeSigned(o.segLf[i], 6);
        if (o.segUpdateMap) for (int i = 0; i < 3; ++i) { hdr.Put(1, 128); hdr.Literal(o.segProbs[i], 8); }
    }
    hdr.Put(o.simpleFilter, 128);
    hdr.Literal(std::uint32_t(o.level), 6);
    hdr.Literal(std::uint32_t(o.sharpness), 3);
    hdr.Put(o.lfDeltas, 128);
    if (o.lfDeltas) {
        hdr.Put(1, 128);
        for (int i = 0; i < 4; ++i) hdr.MaybeSigned(i == 0 ? o.refDelta0 : 0, 6);
        for (int i = 0; i < 4; ++i) hdr.MaybeSigned(i == 0 ? o.modeDeltaB : 0, 6);
    }
    hdr.Literal(std::uint32_t(o.log2Parts), 2);
    hdr.Literal(std::uint32_t(o.q), 7);
    hdr.MaybeSigned(o.dYdc, 4); hdr.MaybeSigned(o.dY2dc, 4); hdr.MaybeSigned(o.dY2ac, 4);
    hdr.MaybeSigned(o.dUVdc, 4); hdr.MaybeSigned(o.dUVac, 4);
    hdr.Literal(0, 1);   // refresh_entropy_probs
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 3; ++k)
                for (int t = 0; t < 11; ++t) {
                    const bool up = o.probUpdates && ((i * 7 + j * 5 + k * 3 + t) % 4 == 0);
                    hdr.Put(up, T::kCoeffUpdateProbs[i][j][k][t]);
                    if (up) { probs[i][j][k][t] = std::uint8_t(1 + rng.Below(255)); hdr.Literal(probs[i][j][k][t], 8); }
                }
    hdr.Put(o.skip, 128);
    const int skipProb = 100;
    if (o.skip) hdr.Literal(skipProb, 8);

    struct Dq { int f[6]; int* y = f; int* y2 = f + 2; int* uv = f + 4; } dq[4];
    for (int s = 0; s < 4; ++s) {
        int q = o.q;
        if (o.segments) q = o.segAbs ? o.segQ[s] : o.q + o.segQ[s];
        T::DequantFactors(q, o.dYdc, o.dY2dc, o.dY2ac, o.dUVdc, o.dUVac, dq[s].f);
    }
    const int nParts = 1 << o.log2Parts;
    std::vector<BoolEncoder> parts(static_cast<std::size_t>(nParts));
    std::vector<int> aboveB(std::size_t(mbw) * 4, 0);
    std::vector<int> aboveNz(std::size_t(mbw) * 9, 0);
    auto quant = [](int c, int q) { return std::max(-2048, std::min(2048, RoundDiv(c, q))); };
    for (int my = 0; my < mbh; ++my) {
        int leftB[4] = {0, 0, 0, 0};
        int leftNz[9] = {};
        BoolEncoder& tb = parts[std::size_t(my % nParts)];
        for (int mx = 0; mx < mbw; ++mx) {
            const int seg = o.segments && o.segUpdateMap ? (mx + 2 * my) % 4 : 0;
            int ymode;
            if (o.modes == 1) ymode = 4;
            else if (o.modes == 2) ymode = rng.Below(4);
            else ymode = (mx + my * 3) % 5;
            const int uvmode = rng.Below(4);
            int bmodes[16];
            for (int b = 0; b < 16; ++b) bmodes[b] = ymode == 4 ? (b + mx * 3 + my * 7) % 10 : (ymode == 1 ? 2 : (ymode == 2 ? 3 : (ymode == 3 ? 1 : 0)));
            const Dq& q = dq[seg];
            const bool hasY2 = ymode != 4;
            // residual levels per block (quantised), 0..15 Y, 16..19 U, 20..23 V, 24 Y2
            int lev[25][16] = {};
            const int ox = mx * 16, oy = my * 16;
            auto Yat = [&](int x, int y) { return int(ry[std::size_t(y) * std::size_t(sty) + std::size_t(x)]); };
            // luma edges
            int above[20], left[16], corner;
            corner = my == 0 ? 127 : (mx == 0 ? 129 : Yat(ox - 1, oy - 1));
            for (int i = 0; i < 20; ++i) above[i] = my == 0 ? 127 : Yat(i < 16 || mx < mbw - 1 ? ox + i : ox + 15, oy - 1);
            for (int r = 0; r < 16; ++r) left[r] = mx == 0 ? 129 : Yat(ox - 1, oy + r);
            if (!hasY2) {
                for (int b = 0; b < 16; ++b) {
                    const int bx = b & 3, by = b >> 2, px0 = ox + bx * 4, py0 = oy + by * 4;
                    int A[8], L[4], P;
                    for (int i = 0; i < 8; ++i) {
                        if (by == 0) A[i] = above[bx * 4 + i];
                        else if (bx * 4 + i < 16 && (i < 4 || bx < 3)) A[i] = Yat(px0 + i, py0 - 1);
                        else A[i] = above[16 + (i - 4)];
                    }
                    for (int i = 0; i < 4; ++i) L[i] = bx == 0 ? left[by * 4 + i] : Yat(px0 - 1, py0 + i);
                    P = by == 0 ? (bx == 0 ? corner : above[bx * 4 - 1]) : (bx == 0 ? left[by * 4 - 1] : Yat(px0 - 1, py0 - 1));
                    int pred[16];
                    ModelPredict4(bmodes[b], A, L, P, pred);
                    int res[16];
                    for (int i = 0; i < 16; ++i) res[i] = int(sy[std::size_t(py0 + i / 4) * std::size_t(sty) + std::size_t(px0 + i % 4)]) - pred[i];
                    int co[16];
                    ForwardDct(res, co);
                    int deq[16];
                    for (int i = 0; i < 16; ++i) { lev[b][i] = quant(co[i], i ? q.y[1] : q.y[0]); deq[i] = lev[b][i] * (i ? q.y[1] : q.y[0]); }
                    int out[16];
                    ModelIdct(deq, out);
                    for (int i = 0; i < 16; ++i) ry[std::size_t(py0 + i / 4) * std::size_t(sty) + std::size_t(px0 + i % 4)] = Sat(pred[i] + out[i]);
                }
            } else {
                int pred[256];
                ModelPredict16(ymode == 0 ? 0 : ymode, above, left, corner, my > 0, mx > 0, 16, pred);
                int dcs[16], co[16][16];
                for (int b = 0; b < 16; ++b) {
                    const int bx = b & 3, by = b >> 2;
                    int res[16];
                    for (int i = 0; i < 16; ++i) {
                        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                        res[i] = int(sy[std::size_t(oy + y) * std::size_t(sty) + std::size_t(ox + x)]) - pred[y * 16 + x];
                    }
                    ForwardDct(res, co[b]);
                    dcs[b] = co[b][0];
                    for (int i = 1; i < 16; ++i) lev[b][i] = quant(co[b][i], q.y[1]);
                }
                int y2[16];
                ForwardWht(dcs, y2);
                int y2deq[16], dcOut[16];
                for (int i = 0; i < 16; ++i) { lev[24][i] = quant(y2[i], i ? q.y2[1] : q.y2[0]); y2deq[i] = lev[24][i] * (i ? q.y2[1] : q.y2[0]); }
                ModelIwht(y2deq, dcOut);
                for (int b = 0; b < 16; ++b) {
                    const int bx = b & 3, by = b >> 2;
                    int deq[16];
                    deq[0] = dcOut[b];
                    for (int i = 1; i < 16; ++i) deq[i] = lev[b][i] * q.y[1];
                    int out[16];
                    ModelIdct(deq, out);
                    for (int i = 0; i < 16; ++i) {
                        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                        ry[std::size_t(oy + y) * std::size_t(sty) + std::size_t(ox + x)] = Sat(pred[y * 16 + x] + out[i]);
                    }
                }
            }
            for (int pl = 0; pl < 2; ++pl) {
                const Bytes& S = pl ? sv : su;
                Bytes& R = pl ? rv : ru;
                const int cx = mx * 8, cy = my * 8;
                auto Rat = [&](int x, int y) { return int(R[std::size_t(y) * std::size_t(stc) + std::size_t(x)]); };
                int ab[8], lf[8];
                const int cc = my == 0 ? 127 : (mx == 0 ? 129 : Rat(cx - 1, cy - 1));
                for (int i = 0; i < 8; ++i) { ab[i] = my == 0 ? 127 : Rat(cx + i, cy - 1); lf[i] = mx == 0 ? 129 : Rat(cx - 1, cy + i); }
                int pred[64];
                ModelPredict16(uvmode, ab, lf, cc, my > 0, mx > 0, 8, pred);
                for (int b = 0; b < 4; ++b) {
                    const int bx = b & 1, by = b >> 1;
                    int res[16];
                    for (int i = 0; i < 16; ++i) {
                        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                        res[i] = int(S[std::size_t(cy + y) * std::size_t(stc) + std::size_t(cx + x)]) - pred[y * 8 + x];
                    }
                    int co[16];
                    ForwardDct(res, co);
                    int* L = lev[16 + pl * 4 + b];
                    int deq[16];
                    for (int i = 0; i < 16; ++i) { L[i] = quant(co[i], i ? q.uv[1] : q.uv[0]); deq[i] = L[i] * (i ? q.uv[1] : q.uv[0]); }
                    int out[16];
                    ModelIdct(deq, out);
                    for (int i = 0; i < 16; ++i) {
                        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
                        R[std::size_t(cy + y) * std::size_t(stc) + std::size_t(cx + x)] = Sat(pred[y * 8 + x] + out[i]);
                    }
                }
            }
            // Did anything survive quantisation? (Y blocks after a Y2 start at coefficient 1.)
            bool anyNz = false;
            for (int b = 0; b < 25; ++b) {
                if (b == 24 && !hasY2) continue;
                for (int i = (b < 16 && hasY2) ? 1 : 0; i < 16; ++i) anyNz |= lev[b][i] != 0;
            }
            // With every level zero the reconstruction above already is the bare prediction.
            const bool skipMb = o.skip && !anyNz;
            // ---- header in the first partition
            if (o.segments && o.segUpdateMap) hdr.Tree(kTSegment, 6, o.segProbs, seg);
            if (o.skip) hdr.Put(skipMb, skipProb);
            hdr.Tree(kTYmode, 8, kPYmode, ymode);
            int* aB = &aboveB[std::size_t(mx) * 4];
            if (ymode == 4) {
                for (int b = 0; b < 16; ++b) {
                    const int A = b < 4 ? aB[b] : bmodes[b - 4];
                    const int L = (b & 3) ? bmodes[b - 1] : leftB[b >> 2];
                    hdr.Tree(kTBmode, 18, T::kBmodeProbs[A][L], bmodes[b]);
                }
            }
            for (int i = 0; i < 4; ++i) { aB[i] = bmodes[12 + i]; leftB[i] = bmodes[i * 4 + 3]; }
            hdr.Tree(kTUv, 6, kPUv, uvmode);
            // ---- tokens
            int* aNz = &aboveNz[std::size_t(mx) * 9];
            if (skipMb) {
                for (int i = 0; i < 8; ++i) aNz[i] = leftNz[i] = 0;
                if (hasY2) aNz[8] = leftNz[8] = 0;
                continue;
            }
            auto block = [&](int type, int ctx, const int* L) -> bool {
                const int first = type == 0 ? 1 : 0;
                int last = -1;
                for (int i = first; i < 16; ++i) if (L[kZz[i]] != 0) last = i;
                bool prevZero = false;
                for (int i = first; i < 16; ++i) {
                    const std::uint8_t* p = probs[type][kBands[i]][ctx];
                    if (i > last) { tb.Tree(kTCoeff, 22, p, 11, 0); break; }
                    const int v = L[kZz[i]], a = std::abs(v);
                    int tok;
                    if (a <= 4) tok = a;
                    else { tok = 5; while (tok < 10 && a >= kCatStart[tok - 5 + 1]) ++tok; }
                    tb.Tree(kTCoeff, 22, p, tok, prevZero ? 2 : 0);
                    if (tok >= 5) {
                        const int c = tok - 5, e = a - kCatStart[c];
                        for (int k = kCatBits[c] - 1; k >= 0; --k) tb.Put((e >> k) & 1, kCatP[c][kCatBits[c] - 1 - k]);
                    }
                    if (a) tb.Put(v < 0, 128);
                    prevZero = a == 0;
                    ctx = a == 0 ? 0 : (a == 1 ? 1 : 2);
                }
                return last >= 0;
            };
            if (hasY2) { const bool nz = block(1, aNz[8] + leftNz[8], lev[24]); aNz[8] = leftNz[8] = nz; }
            // contexts: aNz / leftNz hold Y columns / rows 0..3, then U 4..5, V 6..7, Y2 8
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col) {
                    const bool has = block(hasY2 ? 0 : 3, aNz[col] + leftNz[row], lev[row * 4 + col]);
                    aNz[col] = has;
                    leftNz[row] = has;
                }
            for (int pl = 0; pl < 2; ++pl)
                for (int row = 0; row < 2; ++row)
                    for (int col = 0; col < 2; ++col) {
                        int& up = aNz[4 + 2 * pl + col];
                        int& lf = leftNz[4 + 2 * pl + row];
                        const bool has = block(2, up + lf, lev[16 + pl * 4 + row * 2 + col]);
                        up = has;
                        lf = has;
                    }
        }
    }
    const Bytes first = hdr.Finish();
    Bytes out;
    const std::uint32_t tag = 0u | (0u << 1) | (1u << 4) | (std::uint32_t(first.size()) << 5);
    PutLe(out, tag, 3);
    out.insert(out.end(), {0x9D, 0x01, 0x2A});
    PutLe(out, std::uint32_t(W), 2);
    PutLe(out, std::uint32_t(H), 2);
    out.insert(out.end(), first.begin(), first.end());
    std::vector<Bytes> pb;
    for (BoolEncoder& p : parts) pb.push_back(p.Finish());
    for (int i = 0; i + 1 < nParts; ++i) PutLe(out, std::uint32_t(pb[std::size_t(i)].size()), 3);
    for (const Bytes& p : pb) out.insert(out.end(), p.begin(), p.end());
    if (recon) {
        recon->w = W; recon->h = H;
        recon->y.resize(std::size_t(W) * std::size_t(H));
        recon->u.resize(std::size_t(cw) * std::size_t(ch));
        recon->v.resize(recon->u.size());
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) recon->y[std::size_t(y) * std::size_t(W) + std::size_t(x)] = ry[std::size_t(y) * std::size_t(sty) + std::size_t(x)];
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) {
                recon->u[std::size_t(y) * std::size_t(cw) + std::size_t(x)] = ru[std::size_t(y) * std::size_t(stc) + std::size_t(x)];
                recon->v[std::size_t(y) * std::size_t(cw) + std::size_t(x)] = rv[std::size_t(y) * std::size_t(stc) + std::size_t(x)];
            }
    }
    return out;
}

// A test picture: gradients, a few hard edges and some noise.
inline Planes TestPlanes(int w, int h, std::uint64_t seed) {
    Planes p;
    p.w = w; p.h = h;
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    p.y.resize(std::size_t(w) * std::size_t(h));
    p.u.resize(std::size_t(cw) * std::size_t(ch));
    p.v.resize(p.u.size());
    Rng r(seed);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v = 40 + (x * 150) / std::max(1, w - 1) + std::abs((y * 9) % 60 - 30);
            if ((x / 9 + y / 7) % 5 == 0) v = 230;
            v += r.Below(9) - 4;
            p.y[std::size_t(y) * std::size_t(w) + std::size_t(x)] = Sat(v);
        }
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            p.u[std::size_t(y) * std::size_t(cw) + std::size_t(x)] = Sat(90 + x * 3 + r.Below(5));
            p.v[std::size_t(y) * std::size_t(cw) + std::size_t(x)] = Sat(170 - y * 2 + ((x / 4) % 2) * 30);
        }
    return p;
}

} // namespace webptest
