#pragma once
// strtod / strtof that read '.' as the decimal point whatever LC_NUMERIC says. The C library's own versions follow the
// process locale: after setlocale(LC_ALL, "") in a German or French locale they stop at the '.' of "1.5" and a model file
// reads as 1 (or fails). Model files always use the point. Internal: not installed.
#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <charconv>
#include <system_error>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <string>

namespace okay {
namespace detail {

// The exact fast case of decimal to double conversion (W. D. Clinger, "How to read floating point numbers accurately",
// 1990): when the digits, read as an integer, fit in 53 bits and the power of ten is at most 22 either way, both are exact
// doubles and one multiplication or division rounds the true value correctly, so the result is the one strtod gives.
// Reads [p, e) as [-]digits[.digits][(e|E)[+|-]digits], the whole range; returns false for anything else (more digits,
// a larger exponent, a '+' sign, other text) and the caller reads it the slow way. Needs no locale.
inline bool FastDecimal(const char* p, const char* e, double& out) {
    static const double kPow10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
    (void)p; (void)e; (void)out; return false;   // wider intermediate precision (x87) would round twice
#else
    if (p >= e) return false;
#endif
    const bool neg = *p == '-';
    if (neg) ++p;
    // every digit goes into m, leading zeros too (they add nothing); at most 19 digits in all, so m cannot overflow
    unsigned long long m = 0;
    const char* const d0 = p;
    for (unsigned d; p < e && (d = (unsigned)(unsigned char)*p - '0') <= 9; ++p) m = m * 10 + d;
    std::ptrdiff_t digits = p - d0;
    int scale = 0;   // the power of ten of the digits after the point (and the exponent)
    if (p < e && *p == '.') {
        const char* const f0 = ++p;
            for (unsigned d; p < e && (d = (unsigned)(unsigned char)*p - '0') <= 9; ++p) m = m * 10 + d;
        scale = -(int)std::min<std::ptrdiff_t>(p - f0, 1000);
        digits += p - f0;
    }
    if (digits == 0 || digits > 19) return false;
    if (p < e && (*p == 'e' || *p == 'E')) {
        ++p;
        bool eneg = false;
        if (p < e && (*p == '-' || *p == '+')) { eneg = *p == '-'; ++p; }
        if (p >= e) return false;
        int x = 0;
        for (; p < e && *p >= '0' && *p <= '9'; ++p) { x = x * 10 + (*p - '0'); if (x > 1000) return false; }
        scale += eneg ? -x : x;
    }
    if (p != e) return false;
    if (m == 0) { out = neg ? -0.0 : 0.0; return true; }
    if (m > (1ull << 53) || scale < -22 || scale > 22) return false;
    double v = (double)m;
    v = scale < 0 ? v / kPow10[-scale] : v * kPow10[scale];
    out = neg ? -v : v;
    return true;
}

// Four ASCII digits at q (q + 4 must be readable) as their value, or -1 when one of the four bytes is not a digit. Little
// endian only (the first character is the lowest byte); elsewhere it always says -1 and the callers go digit by digit.
// OKAY_NUMPARSE_NO_FOUR_DIGITS forces that other path on any machine: the test reader_numparse_portable is built with it and
// checks that ScanDecimal gives the same answers both ways. OKAY_NUMPARSE_FOUR_DIGITS says which path this build has.
#if !defined(OKAY_NUMPARSE_NO_FOUR_DIGITS) && ((defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || defined(_MSC_VER))
#define OKAY_NUMPARSE_FOUR_DIGITS 1
#else
#define OKAY_NUMPARSE_FOUR_DIGITS 0
#endif
inline int FourDigits(const char* q) {
#if OKAY_NUMPARSE_FOUR_DIGITS
    std::uint32_t x;
    std::memcpy(&x, q, 4);
    // every byte in '0'..'9': its high nibble is 3, and adding 6 does not carry it out of 3 (no byte passes the first test
    // with a value that could carry into the next byte)
    if ((x & 0xF0F0F0F0u) != 0x30303030u || ((x + 0x06060606u) & 0xF0F0F0F0u) != 0x30303030u) return -1;
    x -= 0x30303030u;                              // bytes d0 d1 d2 d3, d0 the first digit
    x = (x * 10u + (x >> 8)) & 0x00FF00FFu;        // d0*10+d1 in byte 0, d2*10+d3 in byte 2
    return (int)((x * 100u + (x >> 16)) & 0xFFFFu);
#else
    (void)q; return -1;
#endif
}

// FastDecimal for the token that starts at `p` and ends at the first white space (' ', 9 to 13) or at `e`, read in one pass:
// returns true with the same value FastDecimal(p, tokenEnd) gives, and moves p to the token end, when FastDecimal would
// accept the token and it has at most `maxLen` characters. False (p unchanged) when FastDecimal would refuse it or the token
// is longer: the caller then reads the token the old way.
inline bool ScanDecimal(const char*& p, const char* e, double& out, std::ptrdiff_t maxLen) {
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
    (void)p; (void)e; (void)out; (void)maxLen; return false;
#else
    static const double kPow10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    auto ws = [](char c) { return c == ' ' || (unsigned char)(c - 9) < 5u; };
    const char* q = p;
    if (q >= e) return false;
    const bool neg = *q == '-';
    if (neg) ++q;
    unsigned long long m = 0;
    // m takes the digits, four at a time while it can (the same value as one at a time: m * 10^4 + v)
    const char* const d0 = q;
    for (int v; e - q >= 4 && (v = FourDigits(q)) >= 0; q += 4) m = m * 10000u + (unsigned)v;
    for (unsigned d; q < e && (d = (unsigned)(unsigned char)*q - '0') <= 9; ++q) m = m * 10 + d;
    std::ptrdiff_t nd = q - d0;
    int scale = 0;
    if (q < e && *q == '.') {
        const char* const f0 = ++q;
        for (int v; e - q >= 4 && (v = FourDigits(q)) >= 0; q += 4) m = m * 10000u + (unsigned)v;
        for (unsigned d; q < e && (d = (unsigned)(unsigned char)*q - '0') <= 9; ++q) m = m * 10 + d;
        scale = -(int)std::min<std::ptrdiff_t>(q - f0, 1000);
        nd += q - f0;
    }
    if (nd == 0 || nd > 19) return false;
    if (q < e && (*q == 'e' || *q == 'E')) {
        ++q;
        bool eneg = false;
        if (q < e && (*q == '-' || *q == '+')) { eneg = *q == '-'; ++q; }
        if (q >= e || ws(*q)) return false;   // FastDecimal: nothing after the 'e' (or its sign) in the token
        int x = 0;
        for (; q < e && *q >= '0' && *q <= '9'; ++q) { x = x * 10 + (*q - '0'); if (x > 1000) return false; }
        scale += eneg ? -x : x;
    }
    if (q < e && !ws(*q)) return false;            // other characters in the token
    if (q - p > maxLen) return false;
    if (m == 0) { out = neg ? -0.0 : 0.0; p = q; return true; }
    if (m > (1ull << 53) || scale < -22 || scale > 22) return false;
    double v = (double)m;
    v = scale < 0 ? v / kPow10[-scale] : v * kPow10[scale];
    out = neg ? -v : v;
    p = q;
    return true;
#endif
}

// A whole token [b, e) of a plain decimal number read without strtod when that gives the same value: FastDecimal, or
// std::from_chars (correctly rounded like strtod, '.' as the point) when the C library also reads '.' as the point.
// False means "not decided here" (a leading '+', hex, inf, out of range, trailing text): the caller goes the old way.
inline bool DecimalWithoutStrtod(const char* b, const char* e, double& v) {
    if (FastDecimal(b, e, v)) return true;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    if (b < e && *b != '+') {
        const char* dp = std::localeconv()->decimal_point;
        if (!dp || (dp[0] == '.' && dp[1] == 0)) {
            double d = 0;
            const auto r = std::from_chars(b, e, d);
            if (r.ec == std::errc() && r.ptr == e) { v = d; return true; }
        }
    }
#endif
    return false;
}

inline double StrToD(const char* s, char** end) {
    const char* dp = std::localeconv()->decimal_point;
    if (!dp || (dp[0] == '.' && dp[1] == 0)) return std::strtod(s, end);   // the usual case: nothing to do
    const char* q = s;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n' || *q == '\f' || *q == '\v') ++q;
    const char* a = q;
    std::size_t point = std::string::npos;
    while (*q && (std::strchr("+-0123456789.eE", *q) != nullptr) && q - a < 63) { if (*q == '.' && point == std::string::npos) point = (std::size_t)(q - a); ++q; }
    if (point == std::string::npos) return std::strtod(s, end);   // no point in the number: the locale does not matter
    std::string buf(a, point);
    buf += dp;
    buf.append(a + point + 1, (std::size_t)(q - a) - point - 1);
    char* e = nullptr;
    const double v = std::strtod(buf.c_str(), &e);
    if (end) {
        std::size_t used = (std::size_t)(e - buf.c_str());
        const std::size_t dpl = std::strlen(dp);
        if (used > point) used -= (used >= point + dpl ? dpl - 1 : used - point);   // back from the locale's point to the file's one
        *end = const_cast<char*>(e == buf.c_str() ? s : a + used);
    }
    return v;
}
inline float StrToF(const char* s, char** end) {
    const char* dp = std::localeconv()->decimal_point;
    if (!dp || (dp[0] == '.' && dp[1] == 0)) return std::strtof(s, end);
    char* e = nullptr;
    const double d = StrToD(s, &e);
    if (end) *end = e;
    return (float)d;   // double rounding: a hair off the correctly rounded float, only under a decimal-comma locale
}

} // namespace detail
} // namespace okay
