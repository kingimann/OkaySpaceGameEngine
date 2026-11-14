#pragma once
// A bounds-checked little-endian cursor over a byte buffer, shared by the own binary readers of the third batch (MD2,
// MD3, B3D, DirectX .x). Every read is checked against the end of the buffer; a failed read latches `bad`, returns zero
// and moves nothing, so a reader can read a whole record and test `bad` once. Counts read from a file are never trusted:
// `Fits(count, recordSize)` checks that the records are really inside the buffer before anything is allocated.
// Internal: not installed.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace okay {
namespace detail {
namespace bin {

struct Br {
    const unsigned char* d = nullptr;
    std::size_t n = 0, pos = 0;
    bool bad = false;

    Br() = default;
    Br(const std::string& s) : d((const unsigned char*)s.data()), n(s.size()) {}
    Br(const unsigned char* p, std::size_t len) : d(p), n(len) {}

    std::size_t Left() const { return n - pos; }
    bool Has(std::uint64_t k) { if (k > (std::uint64_t)Left()) { bad = true; return false; } return true; }
    // True when `count` records of `rec` bytes each lie in the rest of the buffer (no overflow for any 64-bit count).
    bool Fits(std::uint64_t count, std::uint64_t rec) {
        if (rec != 0 && count > (std::uint64_t)Left() / rec) { bad = true; return false; }
        return true;
    }
    bool Seek(std::uint64_t off) { if (off > (std::uint64_t)n) { bad = true; return false; } pos = (std::size_t)off; return true; }
    void Skip(std::uint64_t k) { if (Has(k)) pos += (std::size_t)k; }

    std::uint8_t U8() { if (!Has(1)) return 0; return d[pos++]; }
    std::int8_t I8() { return (std::int8_t)U8(); }
    std::uint16_t U16() { if (!Has(2)) return 0; const std::uint16_t v = (std::uint16_t)(d[pos] | (d[pos + 1] << 8)); pos += 2; return v; }
    std::int16_t I16() { return (std::int16_t)U16(); }
    std::uint32_t U32() {
        if (!Has(4)) return 0;
        const std::uint32_t v = (std::uint32_t)d[pos] | ((std::uint32_t)d[pos + 1] << 8) | ((std::uint32_t)d[pos + 2] << 16) | ((std::uint32_t)d[pos + 3] << 24);
        pos += 4;
        return v;
    }
    std::int32_t I32() { return (std::int32_t)U32(); }
    std::uint64_t U64() { const std::uint64_t lo = U32(); const std::uint64_t hi = U32(); return lo | (hi << 32); }
    float F32() { const std::uint32_t u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    double F64() { const std::uint64_t u = U64(); double f; std::memcpy(&f, &u, 8); return f; }

    // Fixed-size text field, NUL padded (the text ends at the first NUL or after `len` bytes).
    std::string Str(std::size_t len) {
        if (!Has(len)) return std::string();
        std::size_t e = 0;
        while (e < len && d[pos + e] != 0) ++e;
        std::string s((const char*)d + pos, e);
        pos += len;
        return s;
    }
    // Text up to a NUL byte (the NUL is consumed). Fails when there is none inside `maxLen` bytes.
    std::string CStr(std::size_t maxLen) {
        std::size_t e = 0;
        while (pos + e < n && e < maxLen && d[pos + e] != 0) ++e;
        if (pos + e >= n || e >= maxLen) { bad = true; return std::string(); }
        std::string s((const char*)d + pos, e);
        pos += e + 1;
        return s;
    }
};

inline bool FiniteF(float f) { return std::isfinite(f); }

} // namespace bin
} // namespace detail
} // namespace okay
