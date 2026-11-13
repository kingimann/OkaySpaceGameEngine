#pragma once
// Arithmetic in GF(2^255 - 19), internal to okay::crypto (X25519 and Ed25519). UNREVIEWED.
// Two interchangeable implementations with the same interface, chosen at compile time:
//   fe64 (Fe25519_64.hpp): five 51-bit limbs and a 128-bit product. Needs __int128 (GCC/Clang, 64-bit).
//   fe16 (Fe25519_16.hpp): sixteen 16-bit limbs in int64_t. Portable (MSVC, WebAssembly, 32-bit).
// Define OKAY_CRYPTO_FE16 to force the portable one (tests/test_crypto_field.cpp compiles both
// regardless and checks them against each other).
#include "Fe25519_16.hpp"
#if defined(__SIZEOF_INT128__) && !defined(OKAY_CRYPTO_FE16)
#include "Fe25519_64.hpp"
namespace okay::crypto { namespace fe = fe64; }
#else
namespace okay::crypto { namespace fe = fe16; }
#endif
