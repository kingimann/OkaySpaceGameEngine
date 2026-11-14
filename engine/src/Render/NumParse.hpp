#pragma once
// strtod / strtof that read '.' as the decimal point whatever LC_NUMERIC says. The C library's own versions follow the
// process locale: after setlocale(LC_ALL, "") in a German or French locale they stop at the '.' of "1.5" and a model file
// reads as 1 (or fails). Model files always use the point. Internal: not installed.
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <string>

namespace okay {
namespace detail {

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
