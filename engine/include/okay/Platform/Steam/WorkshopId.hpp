#pragma once
// Steam Workshop item ids in scripts (issue #888 finding 32).
//
// A Workshop id is a 64-bit number, and real ones have ten digits or more. A script number
// is a 32-bit float, which holds whole numbers exactly only up to 2^24 (16777216), so
// 2847193562 became 2847193600 and subscribe, unsubscribe and path then asked Steam about a
// different item, or none. So the script builtins hand ids out as decimal text
// ("2847193562") and take them back as text, exactly. A plain number is still accepted when
// it is a whole number below 2^24, the range where a float cannot have lost digits.
//
// Written from the C++ standard library's documented behaviour (std::from_chars); no other
// project's code.
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <system_error>

namespace okay {

/// The largest whole number every value below which a 32-bit float holds exactly.
constexpr double kWorkshopIdExactFloatLimit = 16777216.0;   // 2^24

/// The id as decimal text: 2147483648123 -> "2147483648123". (0, "no item", -> "0".)
inline std::string WorkshopIdToText(std::uint64_t id) { return std::to_string(id); }

/// Decimal text -> id. Digits only (spaces around them are fine): no sign, point or exponent.
/// False, leaving `out` as it was, for anything else, for 0 and for a value past 2^64 - 1.
inline bool WorkshopIdFromText(const std::string& text, std::uint64_t& out) {
    std::size_t b = 0, e = text.size();
    while (b < e && (text[b] == ' ' || text[b] == '\t')) ++b;
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
    if (b == e) return false;
    for (std::size_t i = b; i < e; ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    std::uint64_t v = 0;
    const char* first = text.data() + b;
    const char* last = text.data() + e;
    const auto r = std::from_chars(first, last, v, 10);
    if (r.ec != std::errc() || r.ptr != last || v == 0) return false;
    out = v;
    return true;
}

/// A script number -> id: a whole number from 1 to 16777215. Past that a float may already
/// have lost digits (the id would name another item), and negative, fractional, NaN and
/// infinite values are no id at all. False, leaving `out` as it was, for those.
inline bool WorkshopIdFromNumber(double v, std::uint64_t& out) {
    if (!(v >= 1.0) || !(v < kWorkshopIdExactFloatLimit) || v != std::floor(v)) return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}

} // namespace okay
