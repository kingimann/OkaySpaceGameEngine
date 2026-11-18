#pragma once
// Miller-Rabin primality helpers for our own test-key signing (docs/own_signing_plan.md, stage 1).
//
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Written from FIPS 186-5 appendix B (probabilistic primality tests) and the
// original Miller-Rabin description, in our own words. Random bases come only from okay::crypto::RandomBytes (the OS
// generator); there is no homemade generator. Not constant time, which is fine for testing candidates that are thrown
// away when composite; it is NOT fine once a prime is a secret, so do not call these on a real key's secret values.
#include <cstddef>

#include "okay/Sign/BigInt.hpp"

namespace okay::sign {

/// A uniformly random number with 0 <= out < bound (rejection sampling, no modulo bias). False when bound is zero.
bool RandomBelow(const BigInt& bound, BigInt& out);
/// A random number of exactly `bits` bits (top bit set), bits >= 1.
BigInt RandomBitsTopSet(std::size_t bits);

/// One Miller-Rabin round with a chosen base 1 < a < n-1 and n odd, n > 3. True means "n passes for this base" (it may
/// still be composite: a strong pseudoprime); false means n is certainly composite.
bool MillerRabinRound(const BigInt& n, const BigInt& base);

/// Trial division by the small primes below 1000, then `rounds` Miller-Rabin rounds with random bases. A composite number
/// passes with probability at most 4^-rounds. Primes below 1000 are recognised exactly.
bool IsProbablePrime(const BigInt& n, int rounds = 40);

}  // namespace okay::sign
