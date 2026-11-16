// The exact decimal-to-double conversion (engine/src/Render/DecimalToDouble.hpp) against the C library's strtod, bit for
// bit, and its 5^q table re-derived with big integers. Standalone: no engine library, so it can run under sanitizers.
//
//   OKAY_DECIMAL_ITERS=N   random inputs to try (default 2,000,000, about ten seconds; the full check is 100,000,000).
//
// Inputs (all plain decimals: [+-] digits [. digits] [e [+-] digits]):
//   - every finite double class: random bit patterns printed with 1 to 25 significant digits and as fixed point;
//   - random digit strings of 1 to 40 digits (19 and 20 digits on purpose), the point anywhere, exponents -360..330;
//   - halfway cases: the exact midpoint between two neighbouring doubles (printed exactly through an 80-bit long double
//     where the platform has one), and the midpoint cut short or nudged by one in its last digit;
//   - subnormals and the edges of the range (2^-1074, half of it, DBL_MAX and the halfway point above it, 1e309, 1e-400);
//   - long mantissas: 50 to 800 digits;
//   - garbage made of digits, '.', 'e', 'E', '+' and '-': the scanner must accept exactly what strtod reads whole.
// A "fallback" is an input the conversion hands to strtod (the method cannot decide); they are counted and must be rare.
#include "test_framework.hpp"
#include "../engine/src/Render/DecimalToDouble.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace okay::detail;

namespace {

// ---- Big integers, just enough to re-derive the table: little-endian 32-bit limbs -------------------------------
using Big = std::vector<std::uint32_t>;
void Trim(Big& a) { while (!a.empty() && a.back() == 0) a.pop_back(); }
Big FromU128(std::uint64_t hi, std::uint64_t lo) {
    Big r = {(std::uint32_t)lo, (std::uint32_t)(lo >> 32), (std::uint32_t)hi, (std::uint32_t)(hi >> 32)};
    Trim(r); return r;
}
Big Mul(const Big& a, const Big& b) {
    Big r(a.size() + b.size() + 1, 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t c = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const std::uint64_t t = (std::uint64_t)a[i] * b[j] + r[i + j] + c;
            r[i + j] = (std::uint32_t)t; c = t >> 32;
        }
        std::size_t k = i + b.size();
        while (c) { const std::uint64_t t = (std::uint64_t)r[k] + c; r[k] = (std::uint32_t)t; c = t >> 32; ++k; }
    }
    Trim(r); return r;
}
Big Shl(const Big& a, int n) {   // a * 2^n
    Big r((std::size_t)(n / 32), 0);
    const int b = n % 32;
    std::uint32_t carry = 0;
    for (std::uint32_t v : a) { r.push_back((v << b) | carry); carry = b ? v >> (32 - b) : 0; }
    r.push_back(carry);
    Trim(r); return r;
}
int Cmp(const Big& a, const Big& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (std::size_t i = a.size(); i-- > 0;) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
Big Pow5(int n) { Big r = {1}; const Big five = {5}; for (int i = 0; i < n; ++i) r = Mul(r, five); return r; }

// Every entry: 2^127 <= T < 2^128 and T * 2^s <= 5^q < (T + 1) * 2^s, with equality exactly for 0 <= q <= 55.
void CheckTable() {
    RUN_SUITE("5^q table re-derived with big integers");
    int bad = 0;
    for (int q = kPow5MinQ; q <= kPow5MaxQ; ++q) {
        const std::uint64_t* e = kPow5[q - kPow5MinQ];
        const int s = kPow5Shift[q - kPow5MinQ];
        const Big T = FromU128(e[0], e[1]);
        Big T1 = T;   // T + 1
        for (std::size_t i = 0; ; ++i) { if (i == T1.size()) { T1.push_back(1); break; } if (++T1[i] != 0) break; }
        const bool top = (e[0] >> 63) != 0;
        // compare T * 2^s with 5^q, both scaled to integers: multiply by 2^-s when s < 0, by 5^-q when q < 0
        Big lo, mid, hi;
        if (q >= 0) {
            const Big p = Pow5(q);
            if (s >= 0) { lo = Shl(T, s); hi = Shl(T1, s); mid = p; }
            else { lo = T; hi = T1; mid = Shl(p, -s); }
        } else {
            const Big d = Pow5(-q);   // 5^q = 1 / d; s < 0 here
            lo = Mul(T, d); hi = Mul(T1, d); mid = Shl(Big{1}, -s);
        }
        const bool ok = top && Cmp(lo, mid) <= 0 && Cmp(mid, hi) < 0 && ((Cmp(lo, mid) == 0) == (q >= 0 && q <= kPow5ExactMaxQ));
        if (!ok && bad++ < 5) std::printf("  table entry for 5^%d is wrong\n", q);
    }
    CHECK(bad == 0);
}

// ---- The comparison ---------------------------------------------------------------------------------------------
struct Stats { long tried = 0, bad = 0, fallback = 0, accepted = 0; bool showFallbacks = false; };

std::uint64_t Bits(double d) { std::uint64_t b; std::memcpy(&b, &d, 8); return b; }

// s must be a plain decimal: the scanner must read all of it and give strtod's double.
void Compare(const std::string& s, Stats& st) {
    ++st.tried;
    const char* b = s.data();
    const char* e = b + s.size();
    char* se = nullptr;
    const double ref = std::strtod(s.c_str(), &se);
    DecimalParts d;
    const char* end = ScanDecimal(b, e, d);
    if (end != e || se != s.c_str() + s.size()) {
        if (st.bad++ < 10) std::printf("  not read whole: \"%.80s\"\n", s.c_str());
        return;
    }
    double v;
    if (!DecimalToDouble(d, v)) { if (st.showFallbacks) std::printf("  falls back to strtod: \"%.80s\"\n", s.c_str()); ++st.fallback; return; }
    if (Bits(v) != Bits(ref) && st.bad++ < 10)
        std::printf("  \"%.80s\" -> %a, strtod %a\n", s.c_str(), v, ref);
}

// Garbage tokens: accepted by the scanner (read whole) exactly when strtod reads them whole, and then the same double.
void CompareGarbage(const std::string& s, Stats& st) {
    ++st.tried;
    char* se = nullptr;
    const double ref = std::strtod(s.c_str(), &se);
    const bool strtodWhole = !s.empty() && se == s.c_str() + s.size();
    DecimalParts d;
    const char* end = ScanDecimal(s.data(), s.data() + s.size(), d);
    const bool oursWhole = end == s.data() + s.size();
    if (oursWhole != strtodWhole) { if (st.bad++ < 10) std::printf("  grammar differs on \"%s\"\n", s.c_str()); return; }
    if (!oursWhole) return;
    ++st.accepted;
    double v;
    if (!DecimalToDouble(d, v)) { ++st.fallback; return; }
    if (Bits(v) != Bits(ref) && st.bad++ < 10) std::printf("  \"%s\" -> %a, strtod %a\n", s.c_str(), v, ref);
}

double RandomFinite(std::mt19937_64& r) {
    for (;;) {
        std::uint64_t b = r();
        const int kind = (int)(r() % 8);
        if (kind == 0) b &= 0x800FFFFFFFFFFFFFull;                                       // subnormal or zero
        else if (kind == 1) b = (b & 0x800FFFFFFFFFFFFFull) | ((std::uint64_t)(r() % 4) << 52);   // near the bottom
        else if (kind == 2) b = (b & 0x800FFFFFFFFFFFFFull) | ((std::uint64_t)(2043 + r() % 4) << 52);  // near the top
        else if (kind == 3) b &= 0x8000000000000000ull | (0x7FFull << 52) | (0xFull << 48);   // short mantissas
        double d; std::memcpy(&d, &b, 8);
        if (std::isfinite(d)) return d;
    }
}

std::string RandomDigits(std::mt19937_64& r, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += (char)('0' + r() % 10);
    return s;
}

// A plain decimal from digits: optional sign, the point at `point` (or none), an exponent (or none).
std::string Compose(std::mt19937_64& r, const std::string& digits, long exp10) {
    std::string s;
    const int sign = (int)(r() % 4);
    if (sign == 1) s += '-'; else if (sign == 2) s += '+';
    const int pt = (int)(r() % (digits.size() + 2)) - 1;   // -1: no point
    if (pt < 0) s += digits;
    else { s += digits.substr(0, (std::size_t)pt); s += '.'; s += digits.substr((std::size_t)pt); }
    if (exp10 != 0 || r() % 4 == 0) {
        s += (r() & 1) ? 'e' : 'E';
        if (exp10 < 0) s += '-'; else if (r() % 3 == 0) s += '+';
        char b[32]; std::snprintf(b, sizeof b, "%0*ld", (int)(1 + r() % 3), exp10 < 0 ? -exp10 : exp10);
        s += b;
    }
    return s;
}

// The exact decimal expansion of the midpoint between d (> 0, finite) and the next double up, through a long double with
// at least 64 bits of mantissa (the midpoint needs 54). Empty when the platform has no such long double.
std::string Midpoint(double d) {
#if (defined(__x86_64__) || defined(__i386__)) && !defined(_WIN32)   // the Windows C runtime prints long double as double
    if (std::numeric_limits<long double>::digits < 64) return {};
    const double up = std::nextafter(d, std::numeric_limits<double>::infinity());
    if (!std::isfinite(up)) return {};
    const long double m = ((long double)d + (long double)up) / 2;   // exact in 64 bits of mantissa
    static char buf[1200];
    std::snprintf(buf, sizeof buf, "%.1100Le", m);   // glibc prints the exact expansion; zeros pad it
    std::string s = buf;
    const std::size_t epos = s.find('e');
    std::string mant = s.substr(0, epos), ex = s.substr(epos);
    while (!mant.empty() && mant.back() == '0') mant.pop_back();
    if (!mant.empty() && mant.back() == '.') mant.pop_back();
    return mant + ex;
#else
    (void)d;
    return {};
#endif
}

// Cuts a mantissa "d.ddd...e±x" to n significant digits, or nudges its last digit by +1 / -1 (staying a valid string).
std::string Cut(const std::string& s, std::size_t n) {
    const std::size_t epos = s.find('e');
    std::string mant = s.substr(0, epos);
    const std::string ex = s.substr(epos);
    if (mant.size() > n + 1) mant.resize(n + 1);
    if (!mant.empty() && mant.back() == '.') mant.pop_back();
    return mant + ex;
}
std::string Nudge(const std::string& s, int dir) {
    const std::size_t epos = s.find('e');
    std::string mant = s.substr(0, epos);
    const std::string ex = s.substr(epos);
    if (dir > 0) mant += (mant.find('.') == std::string::npos ? ".0000001" : "0000001");
    else {   // decrement the last non-zero digit when it is not the leading one
        std::size_t i = mant.size();
        while (i-- > 0) if (mant[i] >= '1' && mant[i] <= '9') break;
        if (i != std::string::npos && i > 0) mant[i] = (char)(mant[i] - 1);
    }
    return mant + ex;
}

void Fixed() {
    RUN_SUITE("chosen inputs");
    static const char* kCases[] = {
        "0", "-0", "+0", "0.0", "-0.0", "0e0", "0e999999", "-0e-999999", "0.000000000000000000000000000000000",
        "1", "-1", "1.0", "0.5", ".5", "5.", "-.5", "1e0", "1E+0", "1e-0",
        "0.1", "0.2", "0.3", "0.30000000000000004", "0.1000000000000000055511151231257827021181583404541015625",
        "3.141592653589793", "3.1415926535897932384626433832795028841971693993751",
        "9007199254740992", "9007199254740993", "9007199254740994", "9007199254740995", "9007199254740993.0000000001",
        "18446744073709551615", "18446744073709551616", "18446744073709551617", "99999999999999999999",
        "1000000000000000000000", "1e22", "1e23", "8.98846567431158e307",
        "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308",
        "1.797693134862315807937289714053034150799341327710419e308",   // just below the halfway point to 2^1024
        "1.797693134862315807937289714053034150799341327710420e308",   // just above it: infinity
        "1e308", "1e309", "-1e309", "1e400", "123456789e300",
        "2.2250738585072014e-308", "2.2250738585072011e-308", "2.2250738585072012e-308",
        "2.225073858507201136057409796709131975934819546351645648e-308",
        "4.9406564584124654e-324", "5e-324", "4e-324", "3e-324", "2.5e-324",
        "2.4703282292062327e-324", "2.4703282292062328e-324",
        "2.470328229206232720882778577e-324",   // half the smallest subnormal, cut short (below it): zero
        "2.4703282292062327208828439643411068618252990130716238221279284125033775364e-324",  // a bit above it
        "1e-320", "1e-323", "1e-324", "1e-342", "1e-343", "1e-400", "1e-10000",
        "7.2057594037927933e16", "7.2057594037927935e16", "9.0071992547409915e15", "4.35679e-10",
        "1.00000000000000011102230246251565404236316680908203125",   // halfway between 1 and the next double: 1
        "1.00000000000000011102230246251565404236316680908203126",   // above: the next double
        "1.00000000000000011102230246251565404236316680908203124",   // below: 1
        "0.000000000000000000000000000000000000000000001e45",
        "123456789012345678901234567890e-30", "00000000000000000000001.5", "1.5000000000000000000000000000",
    };
    Stats st;
    st.showFallbacks = true;
    for (const char* c : kCases) Compare(c, st);
    std::printf("  %ld inputs, %ld differ, %ld fall back\n", st.tried, st.bad, st.fallback);
    CHECK(st.bad == 0);
    // only the exact or nearly exact midpoints with more than 19 digits (the nine above): the first 19 cannot decide them
    CHECK(st.fallback <= 9);
}

void Random(long iters) {
    RUN_SUITE("random inputs against strtod");
    std::mt19937_64 r(20261116);
    Stats st[8];
    const bool haveMid = !Midpoint(1.0).empty();
    for (long it = 0; it < iters; ++it) {
        const int mode = (int)(r() % 8);
        char buf[512];
        switch (mode) {
        case 0: {   // a double printed with 1..25 significant digits
            const double d = RandomFinite(r);
            std::snprintf(buf, sizeof buf, (r() & 1) ? "%.*e" : "%.*g", (int)(r() % 25), d);
            Compare(buf, st[mode]);
            break;
        }
        case 1: {   // a double printed in fixed point (long for large values)
            const double d = RandomFinite(r);
            const int ex = std::ilogb(d);
            if (d != 0 && (ex > 400 || ex < -400)) { std::snprintf(buf, sizeof buf, "%.17e", d); Compare(buf, st[mode]); break; }
            std::snprintf(buf, sizeof buf, "%.*f", (int)(r() % 40), d);
            Compare(buf, st[mode]);
            break;
        }
        case 2: {   // random digits, 1..40 of them, 19 and 20 often
            const int n = (r() % 3 == 0) ? 19 + (int)(r() % 2) : 1 + (int)(r() % 40);
            Compare(Compose(r, RandomDigits(r, n), (long)(r() % 691) - 360), st[mode]);
            break;
        }
        case 3: {   // halfway: the exact midpoint, cut, or nudged
            if (!haveMid) { Compare(Compose(r, RandomDigits(r, 17 + (int)(r() % 4)), (long)(r() % 600) - 300), st[mode]); break; }
            double d = std::fabs(RandomFinite(r));
            if (r() % 4 == 0) d = std::ldexp(1.0 + (double)(r() % 1024) / 1024, (int)(r() % 120) - 60);   // short ones
            const std::string m = Midpoint(d);
            if (m.empty()) break;
            const int how = (int)(r() % 4);
            std::string s = how == 0 ? m : how == 1 ? Cut(m, 17 + r() % 30) : Nudge(m, how == 2 ? 1 : -1);
            if (r() & 1) s = "-" + s;
            Compare(s, st[mode]);
            break;
        }
        case 4: {   // subnormals and the range edges
            const int which = (int)(r() % 3);
            if (which == 0) {
                std::uint64_t b = r() & 0x000FFFFFFFFFFFFFull;
                double d; std::memcpy(&d, &b, 8);
                std::snprintf(buf, sizeof buf, "%.*e", (int)(r() % 25), d);
            } else if (which == 1) {
                std::snprintf(buf, sizeof buf, "%se%d", RandomDigits(r, 1 + (int)(r() % 25)).c_str(), -300 - (int)(r() % 80));
            } else {
                std::snprintf(buf, sizeof buf, "%se%d", RandomDigits(r, 1 + (int)(r() % 25)).c_str(), 280 + (int)(r() % 50));
            }
            Compare(buf, st[mode]);
            break;
        }
        case 5: {   // long mantissas
            const int n = 50 + (int)(r() % 750);
            Compare(Compose(r, RandomDigits(r, n), (long)(r() % 1400) - 700), st[mode]);
            break;
        }
        case 6: {   // garbage over the number alphabet
            static const char kAlpha[] = "0123456789000.eE+-";
            const int n = 1 + (int)(r() % 12);
            std::string s;
            for (int i = 0; i < n; ++i) s += kAlpha[r() % (sizeof kAlpha - 1)];
            CompareGarbage(s, st[mode]);
            break;
        }
        default: {   // what the FBX files hold: Python repr and %g of float32 and float64 values
            const double d = (r() & 1) ? (double)(float)((double)(std::int64_t)(r() >> 20) / 1e6)
                                       : std::ldexp((double)(r() >> 11), (int)(r() % 40) - 63);
            std::snprintf(buf, sizeof buf, (r() & 1) ? "%.17g" : "%.9g", (r() & 1) ? d : -d);
            Compare(buf, st[mode]);
            break;
        }
        }
    }
    static const char* kNames[8] = {"printed doubles", "fixed point", "random digits", "halfway cases",
                                    "subnormals, range edges", "long mantissas", "garbage tokens", "FBX-like"};
    long tried = 0, bad = 0, fallback = 0;
    for (int m = 0; m < 8; ++m) {
        std::printf("  %-24s %10ld inputs, %ld differ, %ld fall back to strtod%s\n", kNames[m], st[m].tried, st[m].bad,
                    st[m].fallback, m == 6 ? "" : "");
        tried += st[m].tried; bad += st[m].bad; fallback += st[m].fallback;
    }
    std::printf("  garbage tokens read whole as numbers: %ld\n", st[6].accepted);
    std::printf("  total: %ld inputs, %ld differ from strtod, %ld (%.2g) fall back to strtod\n", tried, bad, fallback,
                (double)fallback / (double)(tried ? tried : 1));
    if (!haveMid) std::printf("  (no 80-bit long double here: exact midpoints replaced by random 17-20 digit inputs)\n");
    CHECK(bad == 0);
    // Giving up is about speed, not correctness (strtod then gives the value). It happens when a carry from below the
    // 128-bit product could reach the kept bits (about 2^-73 of inputs), and for more than 19 digits when the first 19
    // cannot decide (the true value within 10^-19 of a rounding boundary: up to about 10^-3 of long inputs; nearly
    // always for exact midpoints, which is what the halfway mode makes).
    for (int m : {0, 1, 4, 6, 7}) CHECK(st[m].fallback * 1000 <= st[m].tried + 10);
    CHECK(st[2].fallback * 500 <= st[2].tried + 10);
    CHECK(st[5].fallback * 200 <= st[5].tried + 10);
}

} // namespace

int main() {
    CheckTable();
    Fixed();
    long iters = 2000000;
    if (const char* e = std::getenv("OKAY_DECIMAL_ITERS")) iters = std::max(1L, std::atol(e));
    Random(iters);
    TEST_MAIN_RESULT();
}
