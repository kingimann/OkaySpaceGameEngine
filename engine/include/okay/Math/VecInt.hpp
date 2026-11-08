#pragma once
// Integer vectors for grids, tiles, chunks and voxels. Exact == (not approximate),
// lexicographic ordering (so they work in std::map / std::set / std::sort) and
// std::hash specialisations (so they work as unordered_map keys).
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace okay {

namespace vecint_detail {
inline std::uint64_t Mix(std::uint64_t h) {        // splitmix64 finaliser
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}
inline int FloorDiv(int a, int b) { int q = a / b; return ((a % b != 0) && ((a < 0) != (b < 0))) ? q - 1 : q; }
inline int FloorMod(int a, int b) { int r = a % b; return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r; }
} // namespace vecint_detail

struct Vec2i {
    int x = 0, y = 0;
    constexpr Vec2i() = default;
    constexpr Vec2i(int x, int y) : x(x), y(y) {}

    int&       operator[](int i)       { return (&x)[i]; }
    const int& operator[](int i) const { return (&x)[i]; }

    constexpr Vec2i operator+(const Vec2i& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2i operator-(const Vec2i& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2i operator*(const Vec2i& o) const { return {x * o.x, y * o.y}; }
    constexpr Vec2i operator*(int s) const { return {x * s, y * s}; }
    constexpr Vec2i operator-() const { return {-x, -y}; }
    Vec2i& operator+=(const Vec2i& o) { x += o.x; y += o.y; return *this; }
    Vec2i& operator-=(const Vec2i& o) { x -= o.x; y -= o.y; return *this; }
    Vec2i& operator*=(int s) { x *= s; y *= s; return *this; }

    constexpr bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2i& o) const { return !(*this == o); }
    /// Lexicographic (x, then y).
    constexpr bool operator<(const Vec2i& o) const { return x != o.x ? x < o.x : y < o.y; }
    constexpr bool operator>(const Vec2i& o) const { return o < *this; }
    constexpr bool operator<=(const Vec2i& o) const { return !(o < *this); }
    constexpr bool operator>=(const Vec2i& o) const { return !(*this < o); }

    std::int64_t SqrMagnitude() const { return (std::int64_t)x * x + (std::int64_t)y * y; }
    float Magnitude() const { return Mathf::Sqrt((float)SqrMagnitude()); }
    /// |dx| + |dy| (4-neighbour grid distance).
    int ManhattanLength() const { return (x < 0 ? -x : x) + (y < 0 ? -y : y); }
    /// max(|dx|, |dy|) (8-neighbour grid distance).
    int ChebyshevLength() const { int a = x < 0 ? -x : x, b = y < 0 ? -y : y; return a > b ? a : b; }
    static int Manhattan(const Vec2i& a, const Vec2i& b) { return (a - b).ManhattanLength(); }
    static int Chebyshev(const Vec2i& a, const Vec2i& b) { return (a - b).ChebyshevLength(); }
    static std::int64_t Dot(const Vec2i& a, const Vec2i& b) { return (std::int64_t)a.x * b.x + (std::int64_t)a.y * b.y; }
    static Vec2i Min(const Vec2i& a, const Vec2i& b) { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y}; }
    static Vec2i Max(const Vec2i& a, const Vec2i& b) { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y}; }
    static Vec2i Abs(const Vec2i& a) { return {a.x < 0 ? -a.x : a.x, a.y < 0 ? -a.y : a.y}; }

    /// Per-axis floor division / modulo (rounds toward -infinity: chunk coords of
    /// negative tiles come out right). `d` components must be non-zero.
    Vec2i FloorDiv(const Vec2i& d) const { return {vecint_detail::FloorDiv(x, d.x), vecint_detail::FloorDiv(y, d.y)}; }
    Vec2i FloorMod(const Vec2i& d) const { return {vecint_detail::FloorMod(x, d.x), vecint_detail::FloorMod(y, d.y)}; }

    Vec2 ToVec2() const { return {(float)x, (float)y}; }
    static Vec2i FloorToInt(const Vec2& v) { return {(int)Mathf::Floor(v.x), (int)Mathf::Floor(v.y)}; }
    static Vec2i CeilToInt(const Vec2& v)  { return {(int)Mathf::Ceil(v.x),  (int)Mathf::Ceil(v.y)}; }
    static Vec2i RoundToInt(const Vec2& v) { return {(int)Mathf::Round(v.x), (int)Mathf::Round(v.y)}; }

    std::size_t Hash() const {
        return (std::size_t)vecint_detail::Mix(((std::uint64_t)(std::uint32_t)x << 32) | (std::uint32_t)y);
    }

    static constexpr Vec2i Zero() { return {0, 0}; }
    static constexpr Vec2i One() { return {1, 1}; }
    static constexpr Vec2i Up() { return {0, 1}; }
    static constexpr Vec2i Down() { return {0, -1}; }
    static constexpr Vec2i Left() { return {-1, 0}; }
    static constexpr Vec2i Right() { return {1, 0}; }
};
inline constexpr Vec2i operator*(int s, const Vec2i& v) { return v * s; }

struct Vec3i {
    int x = 0, y = 0, z = 0;
    constexpr Vec3i() = default;
    constexpr Vec3i(int x, int y, int z) : x(x), y(y), z(z) {}
    constexpr Vec3i(const Vec2i& v, int z = 0) : x(v.x), y(v.y), z(z) {}

    int&       operator[](int i)       { return (&x)[i]; }
    const int& operator[](int i) const { return (&x)[i]; }
    Vec2i xy() const { return {x, y}; }

    constexpr Vec3i operator+(const Vec3i& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3i operator-(const Vec3i& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3i operator*(const Vec3i& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3i operator*(int s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3i operator-() const { return {-x, -y, -z}; }
    Vec3i& operator+=(const Vec3i& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3i& operator-=(const Vec3i& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3i& operator*=(int s) { x *= s; y *= s; z *= s; return *this; }

    constexpr bool operator==(const Vec3i& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3i& o) const { return !(*this == o); }
    /// Lexicographic (x, then y, then z).
    constexpr bool operator<(const Vec3i& o) const {
        return x != o.x ? x < o.x : (y != o.y ? y < o.y : z < o.z);
    }
    constexpr bool operator>(const Vec3i& o) const { return o < *this; }
    constexpr bool operator<=(const Vec3i& o) const { return !(o < *this); }
    constexpr bool operator>=(const Vec3i& o) const { return !(*this < o); }

    std::int64_t SqrMagnitude() const { return (std::int64_t)x * x + (std::int64_t)y * y + (std::int64_t)z * z; }
    float Magnitude() const { return Mathf::Sqrt((float)SqrMagnitude()); }
    int ManhattanLength() const { return (x < 0 ? -x : x) + (y < 0 ? -y : y) + (z < 0 ? -z : z); }
    int ChebyshevLength() const {
        int a = x < 0 ? -x : x, b = y < 0 ? -y : y, c = z < 0 ? -z : z;
        int m = a > b ? a : b; return m > c ? m : c;
    }
    static int Manhattan(const Vec3i& a, const Vec3i& b) { return (a - b).ManhattanLength(); }
    static int Chebyshev(const Vec3i& a, const Vec3i& b) { return (a - b).ChebyshevLength(); }
    static std::int64_t Dot(const Vec3i& a, const Vec3i& b) {
        return (std::int64_t)a.x * b.x + (std::int64_t)a.y * b.y + (std::int64_t)a.z * b.z;
    }
    static Vec3i Cross(const Vec3i& a, const Vec3i& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    static Vec3i Min(const Vec3i& a, const Vec3i& b) {
        return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
    }
    static Vec3i Max(const Vec3i& a, const Vec3i& b) {
        return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
    }
    static Vec3i Abs(const Vec3i& a) { return {a.x < 0 ? -a.x : a.x, a.y < 0 ? -a.y : a.y, a.z < 0 ? -a.z : a.z}; }

    Vec3i FloorDiv(const Vec3i& d) const {
        return {vecint_detail::FloorDiv(x, d.x), vecint_detail::FloorDiv(y, d.y), vecint_detail::FloorDiv(z, d.z)};
    }
    Vec3i FloorMod(const Vec3i& d) const {
        return {vecint_detail::FloorMod(x, d.x), vecint_detail::FloorMod(y, d.y), vecint_detail::FloorMod(z, d.z)};
    }

    Vec3 ToVec3() const { return {(float)x, (float)y, (float)z}; }
    static Vec3i FloorToInt(const Vec3& v) { return {(int)Mathf::Floor(v.x), (int)Mathf::Floor(v.y), (int)Mathf::Floor(v.z)}; }
    static Vec3i CeilToInt(const Vec3& v)  { return {(int)Mathf::Ceil(v.x),  (int)Mathf::Ceil(v.y),  (int)Mathf::Ceil(v.z)}; }
    static Vec3i RoundToInt(const Vec3& v) { return {(int)Mathf::Round(v.x), (int)Mathf::Round(v.y), (int)Mathf::Round(v.z)}; }

    std::size_t Hash() const {
        std::uint64_t h = vecint_detail::Mix(((std::uint64_t)(std::uint32_t)x << 32) | (std::uint32_t)y);
        return (std::size_t)vecint_detail::Mix(h ^ ((std::uint64_t)(std::uint32_t)z + 0x9E3779B97F4A7C15ull));
    }

    static constexpr Vec3i Zero() { return {0, 0, 0}; }
    static constexpr Vec3i One() { return {1, 1, 1}; }
    static constexpr Vec3i Up() { return {0, 1, 0}; }
    static constexpr Vec3i Down() { return {0, -1, 0}; }
    static constexpr Vec3i Left() { return {-1, 0, 0}; }
    static constexpr Vec3i Right() { return {1, 0, 0}; }
    static constexpr Vec3i Forward() { return {0, 0, 1}; }
    static constexpr Vec3i Back() { return {0, 0, -1}; }
};
inline constexpr Vec3i operator*(int s, const Vec3i& v) { return v * s; }

} // namespace okay

namespace std {
template <> struct hash<okay::Vec2i> { size_t operator()(const okay::Vec2i& v) const noexcept { return v.Hash(); } };
template <> struct hash<okay::Vec3i> { size_t operator()(const okay::Vec3i& v) const noexcept { return v.Hash(); } };
} // namespace std
