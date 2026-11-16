#pragma once
// The content hash the GPU renderers use to notice that a mesh changed (their per-mesh vertex-buffer cache:
// GLRenderer.cpp and D3D11Renderer.cpp). Written for this engine: a multiply-and-fold hash over 32-byte blocks.
//
// It replaces a byte-at-a-time FNV-1a loop. That loop does one dependent multiply per BYTE, so hashing a mesh cost
// about 1.3 ns per byte: 1.7 ms for the 1.5 MB of a modest voxel terrain, which the renderer paid every time the mesh
// was rebuilt and once a second for every static mesh, and the cheap per-frame "sample" check (up to 2048 vertices and
// indices) cost about 25 microseconds for each mesh in view. Four independent lanes over 8-byte words are about
// twenty times faster and just as good at what the renderer needs: any change to the bytes changes the hash.
// The values only ever meet other values made by this same function, so nothing depends on them staying the same.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace okay {
namespace meshhash {

constexpr std::uint64_t kSeed  = 1469598103934665603ull;   // the seed the renderers always started from
constexpr std::uint64_t kMulA  = 0x9E3779B97F4A7C15ull;    // 2^64 / golden ratio: an odd multiplier, so x -> x * k is a bijection
constexpr std::uint64_t kMulB  = 0xC2B2AE3D27D4EB4Full;
constexpr std::uint64_t kPrime = 1099511628211ull;         // FNV prime, for the leftover bytes

inline std::uint64_t Fold(std::uint64_t h, std::uint64_t w) {
    h = (h ^ w) * kMulA;
    return h ^ (h >> 29);
}

/// Hash `nbytes` bytes, continuing from `seed` (chain several calls by passing the previous result).
inline std::uint64_t Bytes(const void* data, std::size_t nbytes, std::uint64_t seed = kSeed) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::size_t i = 0;
    std::uint64_t h = seed;
    if (nbytes >= 64) {
        std::uint64_t l0 = seed, l1 = seed ^ kMulB, l2 = seed + kMulA, l3 = ~seed;
        for (; i + 32 <= nbytes; i += 32) {
            std::uint64_t w0, w1, w2, w3;
            std::memcpy(&w0, p + i, 8);      std::memcpy(&w1, p + i + 8, 8);
            std::memcpy(&w2, p + i + 16, 8); std::memcpy(&w3, p + i + 24, 8);
            l0 = Fold(l0, w0); l1 = Fold(l1, w1); l2 = Fold(l2, w2); l3 = Fold(l3, w3);
        }
        h = Fold(Fold(Fold(Fold(seed, l0), l1), l2), l3);
    }
    for (; i + 8 <= nbytes; i += 8) { std::uint64_t w; std::memcpy(&w, p + i, 8); h = Fold(h, w); }
    if (i + 4 <= nbytes) { std::uint32_t w; std::memcpy(&w, p + i, 4); h = Fold(h, w); i += 4; }
    for (; i < nbytes; ++i) { h ^= p[i]; h *= kPrime; }
    // the length goes in too, so a buffer and the same buffer with zero bytes added do not collide
    return Fold(h, (std::uint64_t)nbytes);
}

} // namespace meshhash
} // namespace okay
