#pragma once
// okay::sign::BigInt: unsigned arbitrary-size integers for our own test-key signing (docs/own_signing_plan.md, stage 1).
//
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Not constant time (the operations branch and index on the values), so it
// must never be used for anything that protects a real secret. It exists so that our own APPX packer can sign a
// Developer Mode test package with a throw-away self-signed key. OFF by default (CMake OKAY_OWN_SIGNING); releases refuse it.
//
// Written from Knuth, TAOCP volume 2, section 4.3.1 (classical algorithms A, S, M and D, in our own words and with our
// own structure), and the extended Euclidean algorithm. Nothing was copied or ported from another project.
//
// Representation: little-endian 32-bit limbs in a std::vector, no leading zero limbs, zero = no limbs. 64-bit
// intermediates. Functions never throw on bad arithmetic input: operations that can fail (subtract below zero, divide
// by zero, no inverse) return false and leave the output zero. No <mutex>/<thread> (MinGW rule).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::sign {

class BigInt {
public:
    BigInt() = default;
    static BigInt FromU64(std::uint64_t v);
    /// Big-endian bytes, any length (leading zero bytes allowed).
    static BigInt FromBytes(const std::uint8_t* p, std::size_t n);
    static BigInt FromBytes(const std::vector<std::uint8_t>& v) { return FromBytes(v.data(), v.size()); }
    /// Hex digits (upper or lower case, no prefix). Returns false on any other character; the result is then zero.
    static bool FromHex(const std::string& hex, BigInt& out);

    /// Big-endian bytes. fixedLen == 0: the shortest form (one 0x00 byte for zero). fixedLen > 0: left padded with zeros to
    /// exactly that length; returns an EMPTY vector if the value does not fit.
    std::vector<std::uint8_t> ToBytes(std::size_t fixedLen = 0) const;
    std::string ToHex() const;   // lower case, no leading zeros, "0" for zero
    bool ToU64(std::uint64_t& out) const;   // false if it does not fit

    bool IsZero() const { return limbs_.empty(); }
    bool IsOdd() const { return !limbs_.empty() && (limbs_[0] & 1u) != 0; }
    std::size_t BitLength() const;   // 0 for zero
    bool Bit(std::size_t i) const;
    std::size_t LimbCount() const { return limbs_.size(); }

    /// -1, 0 or 1.
    static int Compare(const BigInt& a, const BigInt& b);
    friend bool operator==(const BigInt& a, const BigInt& b) { return a.limbs_ == b.limbs_; }
    friend bool operator!=(const BigInt& a, const BigInt& b) { return a.limbs_ != b.limbs_; }

    static BigInt Add(const BigInt& a, const BigInt& b);
    /// out = a - b; false (out = 0) when a < b.
    static bool Sub(const BigInt& a, const BigInt& b, BigInt& out);
    static BigInt Mul(const BigInt& a, const BigInt& b);
    /// q = a / b, r = a % b; false when b is zero. q and r may not alias a or b.
    static bool DivMod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r);
    static BigInt ShiftLeft(const BigInt& a, std::size_t bits);
    static BigInt ShiftRight(const BigInt& a, std::size_t bits);
    static BigInt Gcd(const BigInt& a, const BigInt& b);
    /// out = base^exp mod m; false when m is zero. m == 1 gives 0. Plain square-and-multiply, not constant time.
    static bool ModExp(const BigInt& base, const BigInt& exp, const BigInt& m, BigInt& out);
    /// out = a^-1 mod m (0 < out < m); false when m < 2 or gcd(a, m) != 1.
    static bool ModInverse(const BigInt& a, const BigInt& m, BigInt& out);

private:
    std::vector<std::uint32_t> limbs_;
    void Trim();
};

}  // namespace okay::sign
