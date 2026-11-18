// Miller-Rabin tests. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Known primes and composites; random-number helpers.
#include "check.hpp"
#include "okay/Sign/Prime.hpp"

using namespace okay::sign;

static BigInt Hex(const char* s) { BigInt v; BigInt::FromHex(s, v); return v; }
static BigInt Pow2Minus(unsigned k, std::uint64_t c) {
    BigInt r;
    BigInt::Sub(BigInt::ShiftLeft(BigInt::FromU64(1), k), BigInt::FromU64(c), r);
    return r;
}

int main() {
    // Every prime below 1000 is recognised, every other number below 1000 is not (against a plain sieve).
    bool comp[1000] = {};
    for (unsigned p = 2; p < 1000; ++p)
        if (!comp[p]) for (unsigned k = p * p; k < 1000; k += p) comp[k] = true;
    for (unsigned n = 0; n < 1000; ++n) CHECK(IsProbablePrime(BigInt::FromU64(n)) == (n >= 2 && !comp[n]));
    // Small known values past the trial-division range.
    CHECK(IsProbablePrime(BigInt::FromU64(1000003)));
    CHECK(!IsProbablePrime(BigInt::FromU64(1000001)));   // 101 * 9901
    CHECK(IsProbablePrime(BigInt::FromU64(2147483647ull)));          // 2^31 - 1
    CHECK(IsProbablePrime(BigInt::FromU64(2305843009213693951ull))); // 2^61 - 1
    // Known large primes: Mersenne 2^89-1, 2^127-1, 2^521-1; 2^255-19; the secp256k1 field prime.
    CHECK(IsProbablePrime(Pow2Minus(89, 1)));
    CHECK(IsProbablePrime(Pow2Minus(127, 1)));
    CHECK(IsProbablePrime(Pow2Minus(521, 1), 10));
    CHECK(IsProbablePrime(Pow2Minus(255, 19)));
    CHECK(IsProbablePrime(Hex("fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f")));
    // Known composites.
    CHECK(!IsProbablePrime(Pow2Minus(255, 21)));
    CHECK(!IsProbablePrime(Pow2Minus(256, 188)));                              // even
    CHECK(!IsProbablePrime(BigInt::Mul(Pow2Minus(61, 1), Pow2Minus(89, 1))));  // product of two primes
    CHECK(!IsProbablePrime(BigInt::Mul(Pow2Minus(127, 1), Pow2Minus(127, 1))));
    const std::uint64_t carmichael[] = {561, 1105, 1729, 2465, 2821, 6601, 8911, 41041, 825265, 321197185ull};
    for (std::uint64_t c : carmichael) CHECK(!IsProbablePrime(BigInt::FromU64(c)));   // fool Fermat tests, not Miller-Rabin
    // Strong pseudoprime to bases 2, 3, 5 and 7: 3215031751 = 151 * 751 * 28351. Trial division by < 1000 finds 151, so test
    // the single round directly: it passes the four bases and fails base 11, and the full test still says composite.
    const BigInt sp = BigInt::FromU64(3215031751ull);
    for (unsigned b : {2u, 3u, 5u, 7u}) CHECK(MillerRabinRound(sp, BigInt::FromU64(b)));
    CHECK(!MillerRabinRound(sp, BigInt::FromU64(11)));
    CHECK(!IsProbablePrime(sp));
    // A strong pseudoprime with no factor below 1000 would pass trial division: 2152302898747 = 6763 * 10627 * 29947 (bases 2..11).
    const BigInt sp2 = BigInt::FromU64(2152302898747ull);
    for (unsigned b : {2u, 3u, 5u, 7u, 11u}) CHECK(MillerRabinRound(sp2, BigInt::FromU64(b)));
    CHECK(!IsProbablePrime(sp2));
    // MillerRabinRound on real primes passes for any base.
    for (unsigned b = 2; b < 50; ++b) CHECK(MillerRabinRound(BigInt::FromU64(1000003), BigInt::FromU64(b)));
    // Random helpers.
    BigInt out;
    CHECK(!RandomBelow(BigInt(), out));
    for (int i = 0; i < 200; ++i) {
        CHECK(RandomBelow(BigInt::FromU64(1), out) && out.IsZero());
        CHECK(RandomBelow(BigInt::FromU64(1000), out) && BigInt::Compare(out, BigInt::FromU64(1000)) < 0);
        const BigInt bound = Pow2Minus(130, 7);
        CHECK(RandomBelow(bound, out) && BigInt::Compare(out, bound) < 0);
    }
    unsigned seen[8] = {};   // all of 0..7 show up (bound 8 uses three random bits)
    for (int i = 0; i < 400; ++i) { std::uint64_t v; RandomBelow(BigInt::FromU64(8), out); out.ToU64(v); ++seen[v]; }
    for (unsigned c : seen) CHECK(c > 0);
    for (std::size_t bits : {1u, 2u, 7u, 8u, 9u, 31u, 32u, 33u, 255u, 1024u})
        for (int i = 0; i < 20; ++i) CHECK(RandomBitsTopSet(bits).BitLength() == bits);
    // A random 128-bit prime search finds something that the check agrees on (smoke test of the whole path).
    int found = 0;
    for (int i = 0; i < 4000 && found == 0; ++i) {
        BigInt c = RandomBitsTopSet(128);
        if (!c.IsOdd()) c = BigInt::Add(c, BigInt::FromU64(1));
        if (IsProbablePrime(c)) { ++found; CHECK(IsProbablePrime(c, 10)); }
    }
    CHECK(found == 1);
    FINISH("test_sign_prime");
}
