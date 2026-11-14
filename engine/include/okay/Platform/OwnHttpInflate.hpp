#pragma once
// ---------------------------------------------------------------------------
// okay::ownhttp inflate - gzip (RFC 1952) and zlib/"deflate" (RFC 1950) bodies for
// the HTTP client, written here (no zlib). Pure and standalone: no engine, no OS.
// This is compression, not cryptography. Hostile input is the normal case:
//  - the output is capped (`maxOut`), so a zip bomb stops at the cap;
//  - every table is validated (over-subscribed or incomplete codes, bad lengths,
//    distances before the start of the output) and every read is bounds checked;
//  - the gzip CRC-32 and size, or the zlib Adler-32, must match; extra bytes after
//    the stream are refused (no multi-member gzip).
// The whole encoded body is held in memory by the caller (see Limits::maxEncodedBytes).
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>

namespace okay::ownhttp {

namespace inflate_detail {

struct Bits {
    const uint8_t* p; size_t n; size_t pos = 0; uint32_t buf = 0; int cnt = 0;
    bool bad = false;
    Bits(const uint8_t* d, size_t len) : p(d), n(len) {}
    uint32_t Get(int need) {   // need <= 16
        while (cnt < need) {
            if (pos >= n) { bad = true; return 0; }
            buf |= uint32_t(p[pos++]) << cnt; cnt += 8;
        }
        uint32_t v = buf & ((1u << need) - 1);
        buf >>= need; cnt -= need;
        return v;
    }
    void ByteAlign() { buf = 0; cnt = 0; }   // whole bytes are never prefetched beyond `need` (cnt < 8 after a Get)
};

struct Huff {
    uint16_t count[16]; uint16_t symbol[288];
    /// Builds the canonical code. Returns false for over-subscribed codes. `complete` tells if it is complete.
    bool Build(const uint8_t* len, int n, bool& complete) {
        for (int i = 0; i < 16; ++i) count[i] = 0;
        for (int i = 0; i < n; ++i) ++count[len[i]];
        int left = 1;
        for (int l = 1; l < 16; ++l) { left <<= 1; left -= count[l]; if (left < 0) return false; }
        complete = (left == 0);
        uint16_t offs[16]; offs[1] = 0;
        for (int l = 1; l < 15; ++l) offs[l + 1] = uint16_t(offs[l] + count[l]);
        for (int i = 0; i < n; ++i) if (len[i]) symbol[offs[len[i]]++] = uint16_t(i);
        return true;
    }
    int Decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; ++l) {
            code |= int(b.Get(1));
            if (b.bad) return -1;
            int c = count[l];
            if (code - c < first) return symbol[index + (code - first)];
            index += c; first += c; first <<= 1; code <<= 1;
        }
        return -1;
    }
};

inline bool Codes(Bits& b, const Huff& lit, const Huff& dist, std::string& out, size_t maxOut, std::string& why) {
    static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t  lext[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t  dext[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for (;;) {
        int s = lit.Decode(b);
        if (s < 0) { why = b.bad ? "truncated deflate data" : "bad deflate code"; return false; }
        if (s < 256) {
            if (out.size() >= maxOut) { why = "decoded body larger than the limit"; return false; }
            out.push_back(char(s));
        } else if (s == 256) {
            return true;
        } else {
            s -= 257;
            if (s >= 29) { why = "bad deflate length code"; return false; }
            size_t len = lbase[s] + b.Get(lext[s]);
            int ds = dist.Decode(b);
            if (b.bad) { why = "truncated deflate data"; return false; }
            if (ds < 0 || ds >= 30) { why = "bad deflate distance code"; return false; }
            size_t d = dbase[ds] + b.Get(dext[ds]);
            if (b.bad) { why = "truncated deflate data"; return false; }
            if (d > out.size()) { why = "deflate distance too far back"; return false; }
            if (len > maxOut - out.size()) { why = "decoded body larger than the limit"; return false; }
            size_t from = out.size() - d;
            for (size_t i = 0; i < len; ++i) out.push_back(out[from + i]);
        }
    }
}

/// Raw deflate stream. On success `consumed` is the number of input bytes used.
inline bool Raw(const uint8_t* in, size_t n, std::string& out, size_t maxOut, size_t& consumed, std::string& why) {
    Bits b(in, n);
    int last;
    do {
        last = int(b.Get(1));
        int type = int(b.Get(2));
        if (b.bad) { why = "truncated deflate data"; return false; }
        if (type == 0) {
            b.ByteAlign();
            if (b.pos + 4 > n) { why = "truncated deflate data"; return false; }
            uint32_t len = in[b.pos] | (uint32_t(in[b.pos + 1]) << 8);
            uint32_t nlen = in[b.pos + 2] | (uint32_t(in[b.pos + 3]) << 8);
            b.pos += 4;
            if ((len ^ 0xffff) != nlen) { why = "bad stored block length"; return false; }
            if (len > n - b.pos) { why = "truncated deflate data"; return false; }
            if (len > maxOut - out.size()) { why = "decoded body larger than the limit"; return false; }
            out.append(reinterpret_cast<const char*>(in + b.pos), len);
            b.pos += len;
        } else if (type == 1) {
            uint8_t l[288]; int i = 0;
            for (; i < 144; ++i) l[i] = 8;
            for (; i < 256; ++i) l[i] = 9;
            for (; i < 280; ++i) l[i] = 7;
            for (; i < 288; ++i) l[i] = 8;
            Huff lit, dist; bool c;
            lit.Build(l, 288, c);
            uint8_t dl[30]; for (int k = 0; k < 30; ++k) dl[k] = 5;
            dist.Build(dl, 30, c);
            if (!Codes(b, lit, dist, out, maxOut, why)) return false;
        } else if (type == 2) {
            int nlen = int(b.Get(5)) + 257, ndist = int(b.Get(5)) + 1, ncode = int(b.Get(4)) + 4;
            if (b.bad) { why = "truncated deflate data"; return false; }
            if (nlen > 286 || ndist > 30) { why = "bad deflate table sizes"; return false; }
            static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t lengths[320] = {0};
            for (int k = 0; k < ncode; ++k) lengths[order[k]] = uint8_t(b.Get(3));
            if (b.bad) { why = "truncated deflate data"; return false; }
            Huff lencode; bool comp;
            if (!lencode.Build(lengths, 19, comp) || !comp) { why = "bad deflate code lengths"; return false; }
            int idx = 0;
            while (idx < nlen + ndist) {
                int sym = lencode.Decode(b);
                if (sym < 0) { why = b.bad ? "truncated deflate data" : "bad deflate code"; return false; }
                if (sym < 16) lengths[idx++] = uint8_t(sym);
                else {
                    int prev = 0, rep;
                    if (sym == 16) { if (idx == 0) { why = "bad deflate repeat"; return false; } prev = lengths[idx - 1]; rep = 3 + int(b.Get(2)); }
                    else if (sym == 17) rep = 3 + int(b.Get(3));
                    else rep = 11 + int(b.Get(7));
                    if (b.bad) { why = "truncated deflate data"; return false; }
                    if (idx + rep > nlen + ndist) { why = "bad deflate repeat"; return false; }
                    while (rep--) lengths[idx++] = uint8_t(prev);
                }
            }
            if (lengths[256] == 0) { why = "deflate block without end code"; return false; }
            Huff lit, dist; bool c1, c2;
            if (!lit.Build(lengths, nlen, c1)) { why = "bad deflate literal code"; return false; }
            if (!c1 && nlen - lit.count[0] != 1) { why = "incomplete deflate literal code"; return false; }
            if (!dist.Build(lengths + nlen, ndist, c2)) { why = "bad deflate distance code"; return false; }
            if (!c2 && ndist - dist.count[0] > 1) { why = "incomplete deflate distance code"; return false; }
            if (!Codes(b, lit, dist, out, maxOut, why)) return false;
        } else { why = "bad deflate block type"; return false; }
    } while (!last);
    consumed = b.pos;
    return true;
}

inline uint32_t Crc32(const std::string& s) {
    static uint32_t table[256]; static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; }
        init = true;
    }
    uint32_t c = 0xffffffffu;
    for (unsigned char ch : s) c = table[(c ^ ch) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

inline uint32_t Adler32(const std::string& s) {
    uint32_t a = 1, b = 0;
    for (unsigned char ch : s) { a = (a + ch) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

} // namespace inflate_detail

/// Decode a gzip member (`gzip` true) or a zlib stream (`gzip` false). false with `why` set on any problem.
inline bool InflateBody(const std::string& in, bool gzip, size_t maxOut, std::string& out, std::string& why) {
    using namespace inflate_detail;
    out.clear();
    const uint8_t* p = reinterpret_cast<const uint8_t*>(in.data());
    size_t n = in.size(), off = 0;
    if (gzip) {
        if (n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) { why = "not a gzip stream"; return false; }
        unsigned flg = p[3];
        if (flg & 0xe0) { why = "reserved gzip flags"; return false; }
        off = 10;
        if (flg & 4) { if (off + 2 > n) { why = "truncated gzip header"; return false; } size_t x = p[off] | (size_t(p[off + 1]) << 8); off += 2 + x; }
        for (unsigned bit : {8u, 16u}) if (flg & bit) { while (off < n && p[off]) ++off; if (off >= n) { why = "truncated gzip header"; return false; } ++off; }
        if (flg & 2) off += 2;
        if (off > n || n - off < 8) { why = "truncated gzip header"; return false; }
    } else {
        if (n < 6 || (p[0] & 0x0f) != 8 || (p[0] >> 4) > 7 || ((uint32_t(p[0]) << 8) | p[1]) % 31 != 0 || (p[1] & 0x20)) { why = "not a zlib stream"; return false; }
        off = 2;
    }
    size_t used = 0;
    if (!Raw(p + off, n - off, out, maxOut, used, why)) return false;
    off += used;
    if (gzip) {
        if (n - off != 8) { why = "trailing bytes after gzip data"; return false; }
        uint32_t crc = p[off] | (uint32_t(p[off + 1]) << 8) | (uint32_t(p[off + 2]) << 16) | (uint32_t(p[off + 3]) << 24);
        uint32_t isz = p[off + 4] | (uint32_t(p[off + 5]) << 8) | (uint32_t(p[off + 6]) << 16) | (uint32_t(p[off + 7]) << 24);
        if (crc != Crc32(out) || isz != uint32_t(out.size() & 0xffffffffu)) { why = "gzip checksum mismatch"; return false; }
    } else {
        if (n - off != 4) { why = "bad zlib trailer"; return false; }
        uint32_t ad = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) | (uint32_t(p[off + 2]) << 8) | p[off + 3];
        if (ad != Adler32(out)) { why = "zlib checksum mismatch"; return false; }
    }
    return true;
}

} // namespace okay::ownhttp
