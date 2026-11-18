// Miller-Rabin helpers. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. See Prime.hpp. Written from FIPS 186-5 appendix B.
#include "okay/Sign/Prime.hpp"

#include <vector>

#include "okay/Crypto/Crypto.hpp"

namespace okay::sign {

bool RandomBelow(const BigInt& bound, BigInt& out) {
    out = BigInt();
    if (bound.IsZero()) return false;
    const std::size_t bits = bound.BitLength();
    const std::size_t nbytes = (bits + 7) / 8;
    const unsigned topMask = static_cast<unsigned>(0xFFu >> (nbytes * 8 - bits));   // keep only `bits` bits in the first byte
    std::vector<std::uint8_t> buf(nbytes);
    for (;;) {   // rejection sampling: draw `bits` random bits until the value is below the bound
        okay::crypto::RandomBytes(buf.data(), nbytes);
        buf[0] = static_cast<std::uint8_t>(buf[0] & topMask);
        BigInt c = BigInt::FromBytes(buf);
        if (BigInt::Compare(c, bound) < 0) {
            out = c;
            return true;
        }
    }
}

BigInt RandomBitsTopSet(std::size_t bits) {
    if (bits == 0) return BigInt();
    const std::size_t nbytes = (bits + 7) / 8;
    std::vector<std::uint8_t> buf(nbytes);
    okay::crypto::RandomBytes(buf.data(), nbytes);
    const unsigned topBits = static_cast<unsigned>(bits - (nbytes - 1) * 8);   // 1..8 bits live in the first byte
    buf[0] = static_cast<std::uint8_t>(buf[0] & (0xFFu >> (8 - topBits)));
    buf[0] = static_cast<std::uint8_t>(buf[0] | (1u << (topBits - 1)));
    return BigInt::FromBytes(buf);
}

bool MillerRabinRound(const BigInt& n, const BigInt& base) {
    const BigInt one = BigInt::FromU64(1);
    BigInt nm1;
    BigInt::Sub(n, one, nm1);
    std::size_t s = 0;   // n - 1 = d * 2^s with d odd
    while (!nm1.Bit(s)) ++s;
    const BigInt d = BigInt::ShiftRight(nm1, s);
    BigInt x, q, r;
    BigInt::ModExp(base, d, n, x);
    if (x == one || x == nm1) return true;
    for (std::size_t i = 1; i < s; ++i) {
        BigInt::DivMod(BigInt::Mul(x, x), n, q, r);
        x = r;
        if (x == nm1) return true;
        if (x == one) return false;
    }
    return false;
}

bool IsProbablePrime(const BigInt& n, int rounds) {
    if (BigInt::Compare(n, BigInt::FromU64(2)) < 0) return false;
    // Trial division by the primes below 1000 (a sieve, computed once per call: it is tiny).
    bool composite[1000] = {};
    for (unsigned p = 2; p < 1000; ++p) {
        if (composite[p]) continue;
        for (unsigned k = p * p; k < 1000; k += p) composite[k] = true;
        const BigInt bp = BigInt::FromU64(p);
        if (n == bp) return true;
        BigInt q, r;
        BigInt::DivMod(n, bp, q, r);
        if (r.IsZero()) return false;
    }
    if (BigInt::Compare(n, BigInt::FromU64(1000000)) < 0) return true;   // no factor below 1000 and n < 1000^2: prime
    BigInt nm3;
    BigInt::Sub(n, BigInt::FromU64(3), nm3);   // bases are drawn from 2 .. n-2
    for (int i = 0; i < rounds; ++i) {
        BigInt a;
        RandomBelow(nm3, a);
        a = BigInt::Add(a, BigInt::FromU64(2));
        if (!MillerRabinRound(n, a)) return false;
    }
    return true;
}

}  // namespace okay::sign
