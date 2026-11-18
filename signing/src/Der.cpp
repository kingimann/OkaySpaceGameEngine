// ASN.1 DER writer and reader. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. See Der.hpp. Written from ITU-T X.690.
#include "okay/Sign/Der.hpp"

#include <algorithm>
#include <cstdio>

namespace okay::sign::der {

Bytes EncodeLength(std::size_t n) {
    if (n < 128) return Bytes{static_cast<std::uint8_t>(n)};   // short form (X.690 8.1.3.4)
    Bytes digits;
    for (std::size_t v = n; v != 0; v >>= 8) digits.push_back(static_cast<std::uint8_t>(v));
    Bytes out;
    out.push_back(static_cast<std::uint8_t>(0x80 | digits.size()));   // long form: count, then big-endian value
    out.insert(out.end(), digits.rbegin(), digits.rend());
    return out;
}

Bytes Tlv(std::uint8_t tag, const Bytes& content) {
    Bytes out{tag};
    const Bytes len = EncodeLength(content.size());
    out.insert(out.end(), len.begin(), len.end());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

Bytes Boolean(bool v) { return Tlv(kTagBoolean, Bytes{static_cast<std::uint8_t>(v ? 0xFF : 0x00)}); }   // X.690 11.1

Bytes Integer(const BigInt& v) {
    Bytes b = v.ToBytes();
    if ((b[0] & 0x80) != 0) b.insert(b.begin(), 0x00);   // keep it non-negative
    return Tlv(kTagInteger, b);
}

Bytes IntegerU64(std::uint64_t v) { return Integer(BigInt::FromU64(v)); }
Bytes Null() { return Bytes{kTagNull, 0x00}; }
Bytes OctetString(const Bytes& v) { return Tlv(kTagOctetString, v); }

Bytes BitString(const Bytes& v, unsigned unusedBits) {
    Bytes c{static_cast<std::uint8_t>(unusedBits)};
    c.insert(c.end(), v.begin(), v.end());
    return Tlv(kTagBitString, c);
}

Bytes Oid(const std::string& dotted) {
    std::vector<std::uint64_t> arcs;
    std::uint64_t cur = 0;
    bool have = false;
    for (char ch : dotted) {
        if (ch == '.') {
            if (!have) return {};
            arcs.push_back(cur);
            cur = 0;
            have = false;
        } else if (ch >= '0' && ch <= '9') {
            cur = cur * 10 + static_cast<std::uint64_t>(ch - '0');
            have = true;
        } else {
            return {};
        }
    }
    if (!have) return {};
    arcs.push_back(cur);
    if (arcs.size() < 2 || arcs[0] > 2 || (arcs[0] < 2 && arcs[1] > 39)) return {};
    Bytes c;
    auto base128 = [&c](std::uint64_t v) {   // X.690 8.19.2: base 128, high bit set on all but the last octet
        std::uint8_t tmp[10];
        int k = 0;
        do { tmp[k++] = static_cast<std::uint8_t>(v & 0x7F); v >>= 7; } while (v != 0);
        while (k-- > 0) c.push_back(static_cast<std::uint8_t>(tmp[k] | (k > 0 ? 0x80 : 0)));
    };
    base128(arcs[0] * 40 + arcs[1]);
    for (std::size_t i = 2; i < arcs.size(); ++i) base128(arcs[i]);
    return Tlv(kTagOid, c);
}

Bytes Utf8String(const std::string& s) { return Tlv(kTagUtf8String, Bytes(s.begin(), s.end())); }
Bytes PrintableString(const std::string& s) { return Tlv(kTagPrintableString, Bytes(s.begin(), s.end())); }

Bytes UtcTime(int year, int month, int day, int hour, int minute, int second) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d%02d%02d%02d%02d%02dZ", year % 100, month, day, hour, minute, second);
    const std::string s(buf);
    return Tlv(kTagUtcTime, Bytes(s.begin(), s.end()));
}

Bytes GeneralizedTime(int year, int month, int day, int hour, int minute, int second) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d%02d%02d%02dZ", year, month, day, hour, minute, second);
    const std::string s(buf);
    return Tlv(kTagGeneralizedTime, Bytes(s.begin(), s.end()));
}

static Bytes Join(const std::vector<Bytes>& parts) {
    Bytes c;
    for (const Bytes& p : parts) c.insert(c.end(), p.begin(), p.end());
    return c;
}

Bytes Sequence(const std::vector<Bytes>& parts) { return Tlv(kTagSequence, Join(parts)); }
Bytes Set(const std::vector<Bytes>& parts) { return Tlv(kTagSet, Join(parts)); }

Bytes SetOf(std::vector<Bytes> parts) {
    // X.690 11.6: the encodings are ordered as unsigned byte strings, the shorter one first when it is a prefix of the other.
    std::sort(parts.begin(), parts.end());   // lexicographic compare on bytes is exactly that order
    return Tlv(kTagSet, Join(parts));
}

Bytes ContextExplicit(unsigned number, const Bytes& inner) { return Tlv(static_cast<std::uint8_t>(0xA0 | (number & 0x1F)), inner); }

Bytes ContextImplicit(unsigned number, const Bytes& content, bool constructed) {
    return Tlv(static_cast<std::uint8_t>(0x80 | (constructed ? 0x20 : 0) | (number & 0x1F)), content);
}

// ---- reader ------------------------------------------------------------------------------------------------------------

bool Reader::PeekTag(std::uint8_t& tag) const {
    if (p_ == end_) return false;
    tag = *p_;
    return true;
}

bool Reader::Next(Tlv_& out) {
    const std::uint8_t* start = p_;
    if (end_ - p_ < 2) return false;
    const std::uint8_t tag = *p_++;
    if ((tag & 0x1F) == 0x1F) return false;   // multi-byte tag numbers are not supported
    std::size_t len = *p_++;
    if (len >= 0x80) {
        const std::size_t cnt = len & 0x7F;
        if (cnt == 0 || cnt > sizeof(std::size_t) || static_cast<std::size_t>(end_ - p_) < cnt) return false;   // 0x80 = indefinite: not DER
        if (*p_ == 0) return false;   // leading zero length octet: not minimal
        len = 0;
        for (std::size_t i = 0; i < cnt; ++i) len = (len << 8) | *p_++;
        if (len < 128) return false;   // the long form must not be used for a short length
    }
    if (static_cast<std::size_t>(end_ - p_) < len) return false;
    out.tag = tag;
    out.content = p_;
    out.length = len;
    out.whole = start;
    out.wholeLength = static_cast<std::size_t>(p_ - start) + len;
    p_ += len;
    return true;
}

bool Reader::Expect(std::uint8_t tag, Tlv_& out) { return Next(out) && out.tag == tag; }

bool ReadInteger(const Tlv_& t, BigInt& out) {
    out = BigInt();
    if (t.tag != kTagInteger || t.length == 0) return false;
    if ((t.content[0] & 0x80) != 0) return false;   // negative: not supported
    if (t.length > 1 && t.content[0] == 0x00 && (t.content[1] & 0x80) == 0) return false;   // not minimal
    out = BigInt::FromBytes(t.content, t.length);
    return true;
}

bool ReadBoolean(const Tlv_& t, bool& out) {
    if (t.tag != kTagBoolean || t.length != 1 || (t.content[0] != 0x00 && t.content[0] != 0xFF)) return false;
    out = t.content[0] == 0xFF;
    return true;
}

bool ReadOid(const Tlv_& t, std::string& dotted) {
    dotted.clear();
    if (t.tag != kTagOid || t.length == 0) return false;
    std::uint64_t v = 0;
    bool first = true, open = false;
    for (std::size_t i = 0; i < t.length; ++i) {
        const std::uint8_t b = t.content[i];
        if (!open && b == 0x80) return false;   // leading 0x80 octet: not minimal
        if (v >> 57) return false;              // would overflow
        v = (v << 7) | (b & 0x7F);
        open = (b & 0x80) != 0;
        if (open) continue;
        if (first) {
            const std::uint64_t a = v < 80 ? v / 40 : 2;
            dotted = std::to_string(a) + "." + std::to_string(v - a * 40);
            first = false;
        } else {
            dotted += "." + std::to_string(v);
        }
        v = 0;
    }
    return !open;
}

bool ReadBitString(const Tlv_& t, Bytes& out, unsigned& unusedBits) {
    out.clear();
    if (t.tag != kTagBitString || t.length == 0 || t.content[0] > 7) return false;
    unusedBits = t.content[0];
    if (t.length == 1 && unusedBits != 0) return false;
    out.assign(t.content + 1, t.content + t.length);
    return true;
}

Bytes ContentOf(const Tlv_& t) { return Bytes(t.content, t.content + t.length); }

}  // namespace okay::sign::der
