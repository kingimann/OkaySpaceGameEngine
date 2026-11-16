#pragma once
// A random-number seed from a place in the world, for components that should behave differently
// where they stand but the same every run (Spawner, NPCController, BehaviorTreeRunner wander).
//
// Written for issue #888 finding 52: the old seed, (uint32_t)(|x| * 73856093 + |z| * 19349663),
// converted a float of 2^32 or more to an integer once an object stood about 58 units from the
// origin. That is undefined behaviour; x86-64 saturates it, so every spawner and NPC out there got
// the same seed. Here the float's bit pattern is copied into an integer (no conversion, so no
// overflow) and mixed with the SplitMix64 finalizer (xor-shift and multiply steps; the general
// algorithm, constants as published with it), which spreads any one-bit change over the result.
#include <cstdint>
#include <cstring>

namespace okay {

/// A 32-bit seed for the point (x, z) and a `salt` that tells the components apart. Defined for every
/// float (far away, negative, infinite or NaN); -0 and +0 give the same seed; x and -x differ.
inline std::uint32_t PositionSeed(float x, float z, std::uint32_t salt) {
    if (x == 0.0f) x = 0.0f;   // fold -0 into +0
    if (z == 0.0f) z = 0.0f;
    std::uint32_t bx = 0, bz = 0;
    std::memcpy(&bx, &x, sizeof bx);
    std::memcpy(&bz, &z, sizeof bz);
    std::uint64_t k = ((std::uint64_t)bx << 32) | bz;
    k += 0x9E3779B97F4A7C15ull * ((std::uint64_t)salt + 1u);
    k = (k ^ (k >> 30)) * 0xBF58476D1CE4E5B9ull;
    k = (k ^ (k >> 27)) * 0x94D049BB133111EBull;
    k ^= k >> 31;
    return (std::uint32_t)(k ^ (k >> 32));
}

} // namespace okay
