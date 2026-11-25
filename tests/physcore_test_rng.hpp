#pragma once
// The random numbers the physics core tests use: okay::Random (Core/Random.hpp, header only) with the few calls the
// tests make. Range takes floats only, so Range(-1, 1) is a float range.
#include "okay/Core/Random.hpp"
#include <cstdint>

namespace okaytest {
struct CoreRng : okay::Random {
    explicit CoreRng(std::uint64_t seed) : okay::Random(seed) {}
    std::uint32_t Next() { return (std::uint32_t)(NextUInt64() >> 32); }
    float Unit() { return Value(); }
    float Range(float a, float b) { return okay::Random::Range(a, b); }
};
} // namespace okaytest
