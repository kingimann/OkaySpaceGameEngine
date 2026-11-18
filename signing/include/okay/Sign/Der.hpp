#pragma once
// ASN.1 DER writer and reader basics for our own test-key signing (docs/own_signing_plan.md, stage 1).
//
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Written from ITU-T X.690 (BER/CER/DER, sections 8 and 10, 11) and the OID
// encoding of X.690 8.19, in our own words. Only what X.509 / CMS / the APPX signature need: single-byte tags (tag
// numbers below 31), definite lengths, the primitive types below, and constructed SEQUENCE, SET and context tags.
// The reader is strict (DER only): minimal lengths, no indefinite length, BOOLEAN 0x00/0xFF only, minimal INTEGERs.
// No exceptions, no <mutex>/<thread>.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "okay/Sign/BigInt.hpp"

namespace okay::sign::der {

using Bytes = std::vector<std::uint8_t>;

// Universal tags (class bits 00).
constexpr std::uint8_t kTagBoolean = 0x01, kTagInteger = 0x02, kTagBitString = 0x03, kTagOctetString = 0x04, kTagNull = 0x05,
                       kTagOid = 0x06, kTagUtf8String = 0x0C, kTagPrintableString = 0x13, kTagUtcTime = 0x17,
                       kTagGeneralizedTime = 0x18, kTagSequence = 0x30, kTagSet = 0x31;

// ---- writer: every function returns the complete encoding (tag, length, content) ----------------------------------
Bytes EncodeLength(std::size_t n);   // the length octets only
Bytes Tlv(std::uint8_t tag, const Bytes& content);
Bytes Boolean(bool v);
Bytes IntegerU64(std::uint64_t v);
Bytes Integer(const BigInt& v);      // non-negative; adds a leading 0x00 when the top bit would make it negative
Bytes Null();
Bytes OctetString(const Bytes& v);
Bytes BitString(const Bytes& v, unsigned unusedBits = 0);
/// Dotted decimal such as "1.2.840.113549.1.1.11". Returns an EMPTY vector for a malformed or too-short OID.
Bytes Oid(const std::string& dotted);
Bytes Utf8String(const std::string& s);
Bytes PrintableString(const std::string& s);   // the caller must stay inside the PrintableString alphabet
Bytes UtcTime(int year, int month, int day, int hour, int minute, int second);          // YYMMDDHHMMSSZ, years 1950..2049
Bytes GeneralizedTime(int year, int month, int day, int hour, int minute, int second);  // YYYYMMDDHHMMSSZ
Bytes Sequence(const std::vector<Bytes>& parts);   // concatenated in the order given
Bytes Set(const std::vector<Bytes>& parts);        // concatenated as given (use SetOf for a DER "SET OF")
Bytes SetOf(std::vector<Bytes> parts);             // sorted by encoding as X.690 11.6 requires
Bytes ContextExplicit(unsigned number, const Bytes& inner);   // [n] EXPLICIT: tag 0xA0 | n, content = inner encoding
Bytes ContextImplicit(unsigned number, const Bytes& content, bool constructed);   // [n] IMPLICIT

// ---- reader ----------------------------------------------------------------------------------------------------------
struct Tlv_ {
    std::uint8_t tag = 0;
    const std::uint8_t* content = nullptr;
    std::size_t length = 0;
    const std::uint8_t* whole = nullptr;   // start of the tag octet
    std::size_t wholeLength = 0;           // tag + length octets + content
};

/// Reads DER elements one after another from a buffer that must outlive the reader.
class Reader {
public:
    Reader(const std::uint8_t* p, std::size_t n) : p_(p), end_(p + n) {}
    explicit Reader(const Bytes& b) : p_(b.data()), end_(b.data() + b.size()) {}
    bool AtEnd() const { return p_ == end_; }
    /// Next element's tag without consuming it; false at the end.
    bool PeekTag(std::uint8_t& tag) const;
    /// Reads the next element. False (and the reader is unusable) on any DER violation or truncation.
    bool Next(Tlv_& out);
    /// Reads the next element and requires this tag.
    bool Expect(std::uint8_t tag, Tlv_& out);

private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

bool ReadInteger(const Tlv_& t, BigInt& out);    // tag must be INTEGER, non-negative, minimal
bool ReadBoolean(const Tlv_& t, bool& out);
bool ReadOid(const Tlv_& t, std::string& dotted);
bool ReadBitString(const Tlv_& t, Bytes& out, unsigned& unusedBits);
Bytes ContentOf(const Tlv_& t);

}  // namespace okay::sign::der
