// DER tests. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Known byte strings (X.690 examples and well-known OIDs), round
// trips and strictness checks of the reader.
#include <string>

#include "check.hpp"
#include "okay/Sign/Der.hpp"

using namespace okay::sign;
using namespace okay::sign::der;

static Bytes B(std::initializer_list<int> l) { Bytes b; for (int x : l) b.push_back(static_cast<std::uint8_t>(x)); return b; }

static bool ReadAll(const Bytes& in, Tlv_& t) { Reader r(in); return r.Next(t) && r.AtEnd(); }

static void Lengths() {
    CHECK(EncodeLength(0) == B({0x00}));
    CHECK(EncodeLength(127) == B({0x7F}));
    CHECK(EncodeLength(128) == B({0x81, 0x80}));
    CHECK(EncodeLength(255) == B({0x81, 0xFF}));
    CHECK(EncodeLength(256) == B({0x82, 0x01, 0x00}));
    CHECK(EncodeLength(65535) == B({0x82, 0xFF, 0xFF}));
    CHECK(EncodeLength(65536) == B({0x83, 0x01, 0x00, 0x00}));
    Bytes big(300, 0xAB);
    const Bytes e = OctetString(big);
    CHECK(e[0] == 0x04 && e[1] == 0x82 && e[2] == 0x01 && e[3] == 0x2C && e.size() == 304);
}

static void KnownBytes() {
    CHECK(Boolean(true) == B({0x01, 0x01, 0xFF}));
    CHECK(Boolean(false) == B({0x01, 0x01, 0x00}));
    CHECK(Null() == B({0x05, 0x00}));
    CHECK(IntegerU64(0) == B({0x02, 0x01, 0x00}));
    CHECK(IntegerU64(127) == B({0x02, 0x01, 0x7F}));
    CHECK(IntegerU64(128) == B({0x02, 0x02, 0x00, 0x80}));    // leading zero keeps it positive
    CHECK(IntegerU64(255) == B({0x02, 0x02, 0x00, 0xFF}));
    CHECK(IntegerU64(256) == B({0x02, 0x02, 0x01, 0x00}));
    CHECK(IntegerU64(65537) == B({0x02, 0x03, 0x01, 0x00, 0x01}));
    CHECK(OctetString(B({1, 2, 3})) == B({0x04, 0x03, 1, 2, 3}));
    CHECK(OctetString(Bytes()) == B({0x04, 0x00}));
    CHECK(BitString(B({0xA0}), 5) == B({0x03, 0x02, 0x05, 0xA0}));
    CHECK(Utf8String("abc") == B({0x0C, 0x03, 'a', 'b', 'c'}));
    CHECK(PrintableString("US") == B({0x13, 0x02, 'U', 'S'}));
    // OIDs: X.690 8.19.5 example {2 100 3} is 81 34 03; rsaEncryption, sha256WithRSAEncryption, sha-256, codeSigning EKU, SPC_INDIRECT_DATA.
    CHECK(Oid("2.100.3") == B({0x06, 0x03, 0x81, 0x34, 0x03}));
    CHECK(Oid("1.2.840.113549.1.1.1") == B({0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01}));
    CHECK(Oid("1.2.840.113549.1.1.11") == B({0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B}));
    CHECK(Oid("2.16.840.1.101.3.4.2.1") == B({0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01}));
    CHECK(Oid("1.3.6.1.5.5.7.3.3") == B({0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x03}));
    CHECK(Oid("1.3.6.1.4.1.311.2.1.4") == B({0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x04}));
    CHECK(Oid("2.5.4.3") == B({0x06, 0x03, 0x55, 0x04, 0x03}));   // commonName
    CHECK(Oid("").empty() && Oid("1").empty() && Oid("1.").empty() && Oid("3.1").empty() && Oid("1.40").empty() && Oid("1.a").empty());
    CHECK(UtcTime(2026, 11, 18, 9, 5, 7) == B({0x17, 0x0D, '2','6','1','1','1','8','0','9','0','5','0','7','Z'}));
    CHECK(GeneralizedTime(2051, 1, 2, 3, 4, 5).size() == 17);
    CHECK(Sequence({IntegerU64(1), Null()}) == B({0x30, 0x05, 0x02, 0x01, 0x01, 0x05, 0x00}));
    CHECK(Set({Null()}) == B({0x31, 0x02, 0x05, 0x00}));
    CHECK(ContextExplicit(0, IntegerU64(2)) == B({0xA0, 0x03, 0x02, 0x01, 0x02}));
    CHECK(ContextImplicit(0, B({1}), false) == B({0x80, 0x01, 0x01}));
    CHECK(ContextImplicit(3, B({}), true) == B({0xA3, 0x00}));
    // SET OF is sorted by encoding.
    CHECK(SetOf({IntegerU64(5), IntegerU64(1), IntegerU64(3)}) == B({0x31, 0x09, 2,1,1, 2,1,3, 2,1,5}));
    CHECK(SetOf({OctetString(B({1, 1})), OctetString(B({1}))}) == B({0x31, 0x07, 4,1,1, 4,2,1,1}));   // shorter first: prefix rule
    // A full AlgorithmIdentifier for sha256WithRSAEncryption (NULL parameters), as it appears in every X.509 certificate.
    CHECK(Sequence({Oid("1.2.840.113549.1.1.11"), Null()}) ==
          B({0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B, 0x05, 0x00}));
    // BigInt as INTEGER.
    BigInt v; BigInt::FromHex("80000000000000000000", v);
    CHECK(Integer(v) == B({0x02, 0x0B, 0x00, 0x80, 0,0,0,0,0,0,0,0,0}));
}

static void RoundTrips() {
    for (std::uint64_t v : {0ull, 1ull, 127ull, 128ull, 255ull, 256ull, 32767ull, 32768ull, 0xFFFFFFFFull, 0x100000000ull, ~0ull}) {
        Tlv_ t; BigInt out;
        const Bytes e = IntegerU64(v);
        CHECK(ReadAll(e, t) && ReadInteger(t, out));
        std::uint64_t back = 1; CHECK(out.ToU64(back) && back == v);
    }
    for (const char* oid : {"1.2.840.113549.1.1.11", "2.5.4.3", "2.100.3", "1.3.6.1.4.1.311.2.1.30", "2.999.1", "0.9.2342.19200300.100.1.1"}) {
        Tlv_ t; std::string s;
        CHECK(ReadAll(Oid(oid), t) && ReadOid(t, s) && s == oid);
    }
    for (int len : {0, 1, 126, 127, 128, 129, 255, 256, 1000, 70000}) {
        Bytes payload(static_cast<std::size_t>(len), 0x5A);
        Tlv_ t;
        CHECK(ReadAll(OctetString(payload), t) && t.tag == 0x04 && t.length == payload.size() && ContentOf(t) == payload);
    }
    { Tlv_ t; bool b = false; CHECK(ReadAll(Boolean(true), t) && ReadBoolean(t, b) && b); CHECK(ReadAll(Boolean(false), t) && ReadBoolean(t, b) && !b); }
    { Tlv_ t; Bytes d; unsigned u = 9; CHECK(ReadAll(BitString(B({0xDE, 0xA0}), 5), t) && ReadBitString(t, d, u) && u == 5 && d == B({0xDE, 0xA0})); }
    // Nested structure: SEQUENCE { INTEGER, SEQUENCE { OID, NULL }, [0] { OCTET STRING } }
    const Bytes doc = Sequence({IntegerU64(2), Sequence({Oid("1.2.840.113549.1.1.11"), Null()}), ContextExplicit(0, OctetString(B({9, 8, 7})))});
    Reader top(doc);
    Tlv_ seq;
    CHECK(top.Expect(kTagSequence, seq) && top.AtEnd());
    Reader in(seq.content, seq.length);
    Tlv_ a, alg, ctx, x;
    BigInt n; std::string oid;
    CHECK(in.Expect(kTagInteger, a) && ReadInteger(a, n) && n == BigInt::FromU64(2));
    CHECK(in.Expect(kTagSequence, alg));
    Reader ar(alg.content, alg.length);
    CHECK(ar.Expect(kTagOid, x) && ReadOid(x, oid) && oid == "1.2.840.113549.1.1.11" && ar.Expect(kTagNull, x) && x.length == 0 && ar.AtEnd());
    CHECK(in.Expect(0xA0, ctx) && in.AtEnd());
    Reader cr(ctx.content, ctx.length);
    CHECK(cr.Expect(kTagOctetString, x) && ContentOf(x) == B({9, 8, 7}));
    std::uint8_t tag = 0; Reader empty(nullptr, 0); CHECK(!empty.PeekTag(tag));
    CHECK(Reader(doc).PeekTag(tag) && tag == 0x30);
    CHECK(top.Expect(kTagSet, x) == false);   // already at end
}

static void Strictness() {   // the reader must refuse everything that is not DER
    Tlv_ t;
    CHECK(!ReadAll(B({0x04, 0x03, 1, 2}), t));              // truncated
    CHECK(!ReadAll(B({0x04}), t));                          // no length
    CHECK(!ReadAll(B({0x04, 0x80}), t));                    // indefinite length
    CHECK(!ReadAll(B({0x04, 0x81, 0x05, 1,2,3,4,5}), t));   // long form for a short length
    CHECK(!ReadAll(B({0x04, 0x82, 0x00, 0x80}), t));        // leading zero length octet
    CHECK(!ReadAll(B({0x1F, 0x01, 0x00}), t));              // high tag numbers unsupported
    CHECK(!ReadAll(B({0x04, 0xFF, 1, 2}), t));              // absurd length count
    CHECK(!ReadAll(B({0x04, 0x01, 0x00, 0x00}), t));        // trailing byte after the element
    BigInt n; bool b; std::string s; Bytes d; unsigned u;
    CHECK(ReadAll(B({0x02, 0x02, 0x00, 0x01}), t) && !ReadInteger(t, n));   // 00 01 is not minimal
    CHECK(ReadAll(B({0x02, 0x01, 0x80}), t) && !ReadInteger(t, n));         // negative
    CHECK(ReadAll(B({0x02, 0x00}), t) && !ReadInteger(t, n));               // empty
    CHECK(ReadAll(B({0x04, 0x01, 0x00}), t) && !ReadInteger(t, n));         // wrong tag
    CHECK(ReadAll(B({0x01, 0x01, 0x01}), t) && !ReadBoolean(t, b));         // BER true, not DER
    CHECK(ReadAll(B({0x01, 0x02, 0xFF, 0xFF}), t) && !ReadBoolean(t, b));
    CHECK(ReadAll(B({0x06, 0x02, 0x2A, 0x80}), t) && !ReadOid(t, s));       // unterminated arc
    CHECK(ReadAll(B({0x06, 0x03, 0x2A, 0x80, 0x01}), t) && !ReadOid(t, s)); // leading 0x80 in an arc
    CHECK(ReadAll(B({0x06, 0x00}), t) && !ReadOid(t, s));
    CHECK(ReadAll(B({0x03, 0x01, 0x08}), t) && !ReadBitString(t, d, u));    // more than 7 unused bits
    CHECK(ReadAll(B({0x03, 0x01, 0x03}), t) && !ReadBitString(t, d, u));    // unused bits but no data
    CHECK(ReadAll(B({0x03, 0x01, 0x00}), t) && ReadBitString(t, d, u) && d.empty());
    // A tiny fuzz: random bytes never crash the reader, and anything accepted re-reads from the same bytes.
    unsigned seed = 7;
    for (int i = 0; i < 20000; ++i) {
        Bytes junk(static_cast<std::size_t>(i % 40));
        for (auto& x : junk) { seed = seed * 1103515245u + 12345u; x = static_cast<std::uint8_t>(seed >> 16); }
        Reader r(junk); Tlv_ e;
        while (r.Next(e)) { CHECK(e.wholeLength >= 2 && e.whole >= junk.data()); }
    }
}

int main() {
    Lengths();
    KnownBytes();
    RoundTrips();
    Strictness();
    FINISH("test_sign_der");
}
