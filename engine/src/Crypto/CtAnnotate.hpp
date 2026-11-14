#pragma once
// Constant-time audit hook, internal to okay::crypto. UNREVIEWED (see Crypto.hpp).
//
// tests/crypto_ct_valgrind.cpp runs the primitives under valgrind with every secret marked as
// "undefined memory"; valgrind then reports any branch or memory address that depends on it.
// A few values are SUPPOSED to become public: the accept/reject result of a tag comparison, and
// whether an X25519 result is all zero (a small-order peer point). OKAY_CT_PUBLIC(x) marks exactly
// those, so the audit can tell "intended" from "leak". In a normal build it expands to nothing and
// costs nothing; it is only active when the library is compiled with OKAY_CRYPTO_CTGRIND.
#ifdef OKAY_CRYPTO_CTGRIND
#include <valgrind/memcheck.h>
#define OKAY_CT_PUBLIC(x) VALGRIND_MAKE_MEM_DEFINED(&(x), sizeof(x))
#else
#define OKAY_CT_PUBLIC(x) ((void)0)
#endif

// ---- value barrier ------------------------------------------------------------------------------
// A compiler is allowed to turn `(a & mask) | (b & ~mask)` into a branch on `mask` when it can prove or
// guess something about it, and clang does (found by the valgrind audit, tests/crypto_ct_valgrind.cpp:
// ToBytes in the 16-bit field and the Barrett correction step in Scalar25519.hpp became `test; js` at -O2/-O3).
// Passing a mask through an empty inline-asm statement makes it opaque, so the select stays arithmetic.
// This is a mitigation that the audit checks per compiler and optimisation level, not a guarantee.
#include <cstdint>
namespace okay::crypto::ct {
template <class T> inline T Barrier(T x) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__("" : "+r"(x));
#else
    volatile T v = x;   // MSVC and others: a volatile round trip is the portable way to hide the value
    x = v;
#endif
    return x;
}
} // namespace okay::crypto::ct
