#pragma once
// The builtins every OkayScript program has, shared by the engine's OkayScriptVM and the standalone okayscript CLI so
// both answer the same. They are raw builtins (Runtime.hpp): `b` is BasicRuntime<V>::builtins or the engine's table of
// the same type. The rules of the ones moved from the engine (OkayScriptVM_Math.cpp, _Network.cpp, _Input.cpp) are
// unchanged: missing arguments read as 0 (or the usual default), never an error, and `len(x, y)` is the length of the
// vector (x, y), not a count (use count(), .Length or .Count for that).
//   InstallMathBuiltins        abs sin cos tan sqrt floor ceil round trunc sign pow exp log log2 log10 cbrt atan atan2
//                              asin acos min max clamp clamp01 lerp inverse_lerp smoothstep wrap ping_pong len hypot
//                              pi deg2rad rad2deg fract frac mod snap remap approach move_toward math_repeat
//                              delta_angle lerp_angle angle_to approximately is_nan is_finite avg min3 max3
//   InstallConversionBuiltins  to_num/num to_str/str long/int64 typeof is_num is_int is_str is_bool is_array is_map
//                              parse_int hex bit_and bit_or bit_xor bit_not shl shr bit_set bit_clear bit_toggle bit_test
//   InstallTextBuiltins        format (with {0:.2f} specs) split join replace trim trim_start trim_end upper lower
//                              starts_with ends_with str_find str_contains str_len substr char_at str_reverse
//                              capitalize title_case repeat str_repeat pad_left pad_right ord chr utf8_len utf8_chars
//                              base64_encode base64_decode hex_encode hex_decode url_encode url_decode
//   InstallCollectionBuiltins  array count push pop contains index_of remove_at insert_at first last clear slice range
//                              reverse sort sort_num sort_str sum min_of max_of zip enumerate flatten map map_get map_set
//                              map_has map_remove map_keys map_values map_count map_clear map_merge set_of set_add
//                              set_has set_remove set_union set_intersect set_diff, and with a callback (a lambda or a
//                              function's name): map_fn filter_fn reduce_fn for_each find_fn any_fn all_fn count_fn
//   InstallJsonBuiltins        to_json/json_stringify (optional indent) from_json/json_parse
//   InstallRandomBuiltins      rand randi chance choose shuffle rand_seed (the random source is the embedder's)
//   InstallTimeBuiltins        now now_ms clock date_text (sleep is the CLI's only: Stdlib.cpp)
// Written for this project; the new ones from their usual definitions (see each), with no other library's code.
#include "okayscript/detail/Support.hpp"
#include "okayscript/detail/Text.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace okayscript {
namespace detail {
namespace m {   // the float maths the engine's Mathf does, with the same results
inline float Abs(float v) { return std::fabs(v); }
inline float Sqrt(float v) { return std::sqrt(v); }
inline float Sin(float r) { return std::sin(r); }
inline float Cos(float r) { return std::cos(r); }
inline float Floor(float v) { return std::floor(v); }
inline float Ceil(float v) { return std::ceil(v); }
inline float Round(float v) { return std::round(v); }
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }
constexpr float kPi = 3.14159265358979323846f;   // the engine's Mathf::PI
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;
} // namespace m
} // namespace detail

/// abs, sin, cos, sqrt, floor, min, max, tan, pow, round, ceil, sign, atan2, clamp, lerp, len, hypot, atan, asin,
/// acos, log, exp, wrap, ping_pong, smoothstep, clamp01, inverse_lerp.
template <class V, class Table> void InstallMathBuiltins(Table& b) {
    namespace m = detail::m;
    b["abs"]   = [](std::vector<V>& a) { return V{m::Abs(a.empty() ? 0 : a[0].AsFloat())}; };
    b["sin"]   = [](std::vector<V>& a) { return V{m::Sin(a.empty() ? 0 : a[0].AsFloat())}; };
    b["cos"]   = [](std::vector<V>& a) { return V{m::Cos(a.empty() ? 0 : a[0].AsFloat())}; };
    b["sqrt"]  = [](std::vector<V>& a) { return V{m::Sqrt(a.empty() ? 0 : a[0].AsFloat())}; };
    b["floor"] = [](std::vector<V>& a) { return V{m::Floor(a.empty() ? 0 : a[0].AsFloat())}; };
    b["min"]   = [](std::vector<V>& a) { return V{m::Min(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
    b["max"]   = [](std::vector<V>& a) { return V{m::Max(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
    b["tan"]   = [](std::vector<V>& a) { return V{std::tan(a.empty() ? 0 : a[0].AsFloat())}; };
    b["pow"]   = [](std::vector<V>& a) { return V{std::pow(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 1)}; };
    b["round"] = [](std::vector<V>& a) { return V{m::Round(a.empty() ? 0 : a[0].AsFloat())}; };
    b["ceil"]  = [](std::vector<V>& a) { return V{m::Ceil(a.empty() ? 0 : a[0].AsFloat())}; };
    b["sign"]  = [](std::vector<V>& a) { float v = a.empty() ? 0 : a[0].AsFloat(); return V{v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f}; };
    b["atan2"] = [](std::vector<V>& a) { return V{std::atan2(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
    b["clamp"] = [](std::vector<V>& a) {
        float v = a.size() > 0 ? a[0].AsFloat() : 0, lo = a.size() > 1 ? a[1].AsFloat() : 0, hi = a.size() > 2 ? a[2].AsFloat() : 1;
        return V{m::Clamp(v, lo, hi)};
    };
    b["lerp"]  = [](std::vector<V>& a) {
        float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0, t = a.size() > 2 ? a[2].AsFloat() : 0;
        return V{m::Lerp(x, y, t)};
    };
    b["len"]   = [](std::vector<V>& a) { float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0; return V{m::Sqrt(x * x + y * y)}; };
    b["hypot"] = [](std::vector<V>& a) { float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0; return V{m::Sqrt(x * x + y * y)}; };
    b["atan"]  = [](std::vector<V>& a) { return V{std::atan(a.empty() ? 0 : a[0].AsFloat())}; };
    b["asin"]  = [](std::vector<V>& a) { return V{std::asin(a.empty() ? 0 : a[0].AsFloat())}; };
    b["acos"]  = [](std::vector<V>& a) { return V{std::acos(a.empty() ? 0 : a[0].AsFloat())}; };
    // log(x) is the natural log; log(x, base) the log in that base.
    b["log"]   = [](std::vector<V>& a) {
        const float x = a.empty() ? 1 : a[0].AsFloat();
        if (a.size() > 1) return V{std::log(x) / std::log(a[1].AsFloat())};
        return V{std::log(x)};
    };
    b["log2"]  = [](std::vector<V>& a) { return V{std::log2(a.empty() ? 1 : a[0].AsFloat())}; };
    b["log10"] = [](std::vector<V>& a) { return V{std::log10(a.empty() ? 1 : a[0].AsFloat())}; };
    b["cbrt"]  = [](std::vector<V>& a) { return V{std::cbrt(a.empty() ? 0 : a[0].AsFloat())}; };
    b["trunc"] = [](std::vector<V>& a) {   // drop the fraction toward zero; a whole number stays exact
        if (!a.empty() && detail::IntOf(a[0])) return a[0];
        return V{std::trunc(a.empty() ? 0 : a[0].AsFloat())};
    };
    b["exp"]   = [](std::vector<V>& a) { return V{std::exp(a.empty() ? 0 : a[0].AsFloat())}; };
    // Wrap a value into [lo, hi) — angles, looping indices, scrolling.
    b["wrap"]  = [](std::vector<V>& a) {
        float v = a.size() > 0 ? a[0].AsFloat() : 0, lo = a.size() > 1 ? a[1].AsFloat() : 0, hi = a.size() > 2 ? a[2].AsFloat() : 1;
        float span = hi - lo;
        if (span <= 0.0f) return V{lo};
        float r = std::fmod(v - lo, span);
        if (r < 0) r += span;
        return V{lo + r};
    };
    // Triangle wave 0..len..0 — patrols, breathing scale, bobbing.
    b["ping_pong"] = [](std::vector<V>& a) {
        float t = a.size() > 0 ? a[0].AsFloat() : 0, len = a.size() > 1 ? a[1].AsFloat() : 1;
        if (len <= 0.0f) return V{0.0f};
        float m = std::fmod(t, 2.0f * len);
        if (m < 0) m += 2.0f * len;
        return V{m <= len ? m : 2.0f * len - m};
    };
    // Smooth (ease in/out) interpolation between a and b.
    b["smoothstep"] = [](std::vector<V>& a) {
        float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 1, t = a.size() > 2 ? a[2].AsFloat() : 0;
        t = m::Clamp01(t);
        t = t * t * (3.0f - 2.0f * t);
        return V{m::Lerp(x, y, t)};
    };
    // Clamp to [0,1] (Mathf.Clamp01).
    b["clamp01"] = [](std::vector<V>& a) {
        return V{m::Clamp01(a.empty() ? 0.0f : a[0].AsFloat())};
    };
    // Where v lies between a and b, as 0..1 (Mathf.InverseLerp).
    b["inverse_lerp"] = [](std::vector<V>& a) {
        float lo = a.size() > 0 ? a[0].AsFloat() : 0, hi = a.size() > 1 ? a[1].AsFloat() : 1,
              v  = a.size() > 2 ? a[2].AsFloat() : 0;
        if (hi - lo == 0.0f) return V{0.0f};
        return V{m::Clamp01((v - lo) / (hi - lo))};
    };
    // ---- Moved from the engine (OkayScriptVM_Math.cpp), rules unchanged ----
    b["pi"]      = [](std::vector<V>&) { return V{m::kPi}; };
    b["deg2rad"] = [](std::vector<V>& a) { return V{(a.empty() ? 0 : a[0].AsFloat()) * m::kDeg2Rad}; };
    b["rad2deg"] = [](std::vector<V>& a) { return V{(a.empty() ? 0 : a[0].AsFloat()) * m::kRad2Deg}; };
    // fract(x): the fractional part of x (x - floor(x)).
    b["fract"] = [](std::vector<V>& a) { float x = a.empty() ? 0.0f : a[0].AsFloat(); return V{x - m::Floor(x)}; };
    // Loop t into [0,length) (Mathf.Repeat).
    b["math_repeat"] = [](std::vector<V>& a) {
        float t = a.size() > 0 ? a[0].AsFloat() : 0, len = a.size() > 1 ? a[1].AsFloat() : 1;
        if (len <= 0.0f) return V{0.0f};
        float r = std::fmod(t, len); if (r < 0) r += len;
        return V{r};
    };
    // Shortest signed difference between two angles, in degrees (Mathf.DeltaAngle).
    b["delta_angle"] = [](std::vector<V>& a) {
        float from = a.size() > 0 ? a[0].AsFloat() : 0, to = a.size() > 1 ? a[1].AsFloat() : 0;
        float d = std::fmod(to - from, 360.0f);
        if (d < -180.0f) d += 360.0f; else if (d > 180.0f) d -= 360.0f;
        return V{d};
    };
    // Nearly equal (Mathf.Approximately).
    b["approximately"] = [](std::vector<V>& a) {
        float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0;
        return V{std::fabs(x - y) < 1e-4f};
    };
    // Angle in degrees from (x1,y1) toward (x2,y2).
    b["angle_to"] = [](std::vector<V>& a) {
        if (a.size() < 4) return V{0.0f};
        float dx = a[2].AsFloat() - a[0].AsFloat(), dy = a[3].AsFloat() - a[1].AsFloat();
        return V{std::atan2(dy, dx) * 57.2957795f};
    };
    b["move_toward"] = [](std::vector<V>& a) {
        float cur = a.size() > 0 ? a[0].AsFloat() : 0, tgt = a.size() > 1 ? a[1].AsFloat() : 0;
        float maxD = a.size() > 2 ? m::Abs(a[2].AsFloat()) : 0;
        if (m::Abs(tgt - cur) <= maxD) return V{tgt};
        return V{cur + (tgt > cur ? maxD : -maxD)};
    };
    auto N = [](std::vector<V>& a, std::size_t i, float d = 0.0f) { return i < a.size() ? a[i].AsFloat() : d; };
    // approach(cur, target, step) -> step toward target without overshooting.
    b["approach"] = [N](std::vector<V>& a) {
        float c = N(a, 0), t = N(a, 1), s = std::fabs(N(a, 2));
        if (c < t) return V{std::min(c + s, t)};
        if (c > t) return V{std::max(c - s, t)};
        return V{t};
    };
    // remap(v, inLo, inHi, outLo, outHi) -> rescale a value between ranges.
    b["remap"] = [N](std::vector<V>& a) {
        float v = N(a, 0), il = N(a, 1), ih = N(a, 2), ol = N(a, 3), oh = N(a, 4, 1.0f);
        if (ih == il) return V{ol};
        return V{ol + (v - il) / (ih - il) * (oh - ol)};
    };
    b["frac"]   = [N](std::vector<V>& a) { float v = N(a, 0); return V{v - std::floor(v)}; };
    b["mod"]    = [N](std::vector<V>& a) { float b2 = N(a, 1, 1.0f); float r = b2 != 0 ? std::fmod(N(a, 0), b2) : 0; if (r < 0) r += std::fabs(b2); return V{r}; };
    b["snap"]   = [N](std::vector<V>& a) { float s = N(a, 1, 1.0f); return V{s != 0 ? std::round(N(a, 0) / s) * s : N(a, 0)}; };
    b["is_nan"] = [N](std::vector<V>& a) { float v = N(a, 0); return V{std::isnan(v) ? 1.0f : 0.0f}; };
    b["is_finite"] = [N](std::vector<V>& a) { float v = N(a, 0); return V{std::isfinite(v) ? 1.0f : 0.0f}; };
    b["avg"]    = [](std::vector<V>& a) { if (a.empty()) return V{0.0f}; float s = 0; for (auto& v : a) s += v.AsFloat(); return V{s / a.size()}; };
    b["min3"]   = [N](std::vector<V>& a) { return V{std::min(N(a, 0), std::min(N(a, 1), N(a, 2)))}; };
    b["max3"]   = [N](std::vector<V>& a) { return V{std::max(N(a, 0), std::max(N(a, 1), N(a, 2)))}; };
    b["lerp_angle"] = [N](std::vector<V>& a) {
        float fr = N(a, 0), to = N(a, 1), t = N(a, 2);
        float d = std::fmod(to - fr + 540.0f, 360.0f) - 180.0f;   // shortest path
        return V{fr + d * t};
    };
}

/// to_num / num (text to a number, 0 when it is not one: "12abc" reads 12) and to_str / str.
template <class V, class Table> void InstallConversionBuiltins(Table& b) {
    b["to_num"] = [](std::vector<V>& a) {
        if (a.empty()) return V{0.0f};
        try { return V{std::stof(a[0].AsString())}; } catch (...) { return V{0.0f}; }
    };
    b["to_str"] = [](std::vector<V>& a) { return V{a.empty() ? std::string{} : a[0].AsString()}; };
    b["num"] = b["to_num"];
    b["str"] = b["to_str"];
    // long(x) / int64(x): a 64-bit whole number (where the value type has them; else the number without its fraction).
    // Text is read exactly ("9007199254740993"); 0 when it is not a whole number.
    b["long"] = [](std::vector<V>& a) {
        if (a.empty()) return detail::MakeInt<V>(0);
        if (detail::IntOf(a[0])) return a[0];
        if (a[0].IsString()) {
            const std::string t = a[0].AsString();
            char* end = nullptr;
            const long long n = std::strtoll(t.c_str(), &end, 10);
            return detail::MakeInt<V>(end == t.c_str() ? 0 : (std::int64_t)n);
        }
        return detail::MakeInt<V>(detail::SafeInt64(a[0].AsFloat()));
    };
    b["int64"] = b["long"];
    // ---- Moved from the engine (OkayScriptVM_Math.cpp), rules unchanged; is_int is new ----
    // Type introspection: what kind of value is this?
    b["typeof"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string("null")};
        const V& v = a[0];
        if (v.IsArray())  return V{std::string("array")};
        if (v.IsMap())    return V{std::string("map")};
        if (v.IsString()) return V{std::string("string")};
        if (v.IsBool())   return V{std::string("bool")};
        if (v.IsVec3())   return V{std::string("vec3")};
        if (detail::IntOf(v)) return V{std::string("int")};
        return V{std::string("number")};
    };
    b["is_array"] = [](std::vector<V>& a) { return V{!a.empty() && a[0].IsArray()}; };
    b["is_map"]   = [](std::vector<V>& a) { return V{!a.empty() && a[0].IsMap()}; };
    b["is_str"]   = [](std::vector<V>& a) { return V{!a.empty() && a[0].IsString()}; };
    b["is_bool"]  = [](std::vector<V>& a) { return V{!a.empty() && a[0].IsBool()}; };
    b["is_num"]   = [](std::vector<V>& a) { return V{!a.empty() && a[0].IsNumber()}; };
    b["is_int"]   = [](std::vector<V>& a) { return V{!a.empty() && detail::IntOf(a[0]) != nullptr}; };
    // ---- Bitwise / integer helpers (flags, masks, tile ids, packing): 32-bit integers ----
    auto I = [](std::vector<V>& a, std::size_t i) -> std::int32_t { return i < a.size() ? (std::int32_t)a[i].AsFloat() : 0; };
    b["bit_and"] = [I](std::vector<V>& a) { return V{(float)(I(a,0) & I(a,1))}; };
    b["bit_or"]  = [I](std::vector<V>& a) { return V{(float)(I(a,0) | I(a,1))}; };
    b["bit_xor"] = [I](std::vector<V>& a) { return V{(float)(I(a,0) ^ I(a,1))}; };
    b["bit_not"] = [I](std::vector<V>& a) { return V{(float)(~I(a,0))}; };
    b["shl"]     = [I](std::vector<V>& a) { return V{(float)(I(a,0) << (I(a,1) & 31))}; };
    b["shr"]     = [I](std::vector<V>& a) { return V{(float)(I(a,0) >> (I(a,1) & 31))}; };
    b["bit_set"]    = [I](std::vector<V>& a) { return V{(float)(I(a,0) |  (1 << (I(a,1) & 31)))}; };
    b["bit_clear"]  = [I](std::vector<V>& a) { return V{(float)(I(a,0) & ~(1 << (I(a,1) & 31)))}; };
    b["bit_toggle"] = [I](std::vector<V>& a) { return V{(float)(I(a,0) ^  (1 << (I(a,1) & 31)))}; };
    b["bit_test"]   = [I](std::vector<V>& a) { return V{(I(a,0) & (1 << (I(a,1) & 31))) != 0}; };
    // hex(n) -> "0x1f" ; parse_int("ff", 16) -> 255 (base defaults to 10).
    b["hex"] = [I](std::vector<V>& a) {
        char buf[24]; std::snprintf(buf, sizeof(buf), "0x%x", (unsigned)I(a, 0));
        return V{std::string(buf)};
    };
    b["parse_int"] = [](std::vector<V>& a) {
        std::string s = a.empty() ? "" : a[0].AsString();
        int base = a.size() > 1 ? detail::SafeInt(a[1].AsFloat()) : 10;
        return V{(float)std::strtol(s.c_str(), nullptr, base)};
    };
}

/// Strings. Moved from the engine (OkayScriptVM_Math.cpp) with their rules unchanged, apart from format, which also
/// takes format specs now ({0:.2f}, {:>8}, {:x}; detail::FormatTemplate). The encoding helpers are new.
template <class V, class Table> void InstallTextBuiltins(Table& b) {
    using detail::SafeInt;
    b["str_len"] = [](std::vector<V>& a) { return V{a.empty() ? 0.0f : (float)a[0].AsString().size()}; };
    b["upper"] = [](std::vector<V>& a) {
        std::string s = a.empty() ? "" : a[0].AsString();
        for (auto& c : s) c = (char)std::toupper((unsigned char)c);
        return V{s};
    };
    b["lower"] = [](std::vector<V>& a) {
        std::string s = a.empty() ? "" : a[0].AsString();
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return V{s};
    };
    b["substr"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        std::string s = a[0].AsString();
        int start = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0;
        int n = a.size() > 2 ? SafeInt(a[2].AsFloat()) : (int)s.size();
        if (start < 0) start = 0;
        if (start > (int)s.size()) return V{std::string{}};
        if (n < 0) n = 0;
        return V{s.substr(start, n)};
    };
    b["char_at"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        std::string s = a[0].AsString();
        int i = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0;
        if (i < 0 || i >= (int)s.size()) return V{std::string{}};
        return V{std::string(1, s[i])};
    };
    b["str_find"] = [](std::vector<V>& a) {
        if (a.size() < 2) return V{-1.0f};
        auto pos = a[0].AsString().find(a[1].AsString());
        return V{pos == std::string::npos ? -1.0f : (float)pos};
    };
    b["str_contains"] = [](std::vector<V>& a) {
        if (a.size() < 2) return V{false};
        return V{a[0].AsString().find(a[1].AsString()) != std::string::npos};
    };
    b["starts_with"] = [](std::vector<V>& a) {
        if (a.size() < 2) return V{false};
        const std::string s = a[0].AsString(), p = a[1].AsString();
        return V{s.size() >= p.size() && s.compare(0, p.size(), p) == 0};
    };
    b["ends_with"] = [](std::vector<V>& a) {
        if (a.size() < 2) return V{false};
        const std::string s = a[0].AsString(), p = a[1].AsString();
        return V{s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0};
    };
    // Replace every occurrence of `find` with `repl`.
    b["replace"] = [](std::vector<V>& a) {
        if (a.size() < 3) return V{a.empty() ? std::string{} : a[0].AsString()};
        std::string s = a[0].AsString(), find = a[1].AsString(), repl = a[2].AsString();
        if (find.empty()) return V{s};
        std::size_t pos = 0;
        while ((pos = s.find(find, pos)) != std::string::npos) {
            s.replace(pos, find.size(), repl);
            pos += repl.size();
            detail::CheckTextLength(s.size());
        }
        return V{s};
    };
    // Trim leading/trailing ASCII whitespace.
    b["trim"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        std::string s = a[0].AsString();
        auto notspace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
        return V{s};
    };
    b["trim_start"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        std::string s = a[0].AsString();
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); }));
        return V{s};
    };
    b["trim_end"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        std::string s = a[0].AsString();
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
        return V{s};
    };
    b["str_reverse"] = [](std::vector<V>& a) {
        std::string s = a.empty() ? "" : a[0].AsString();
        std::reverse(s.begin(), s.end());
        return V{s};
    };
    // capitalize("hello") -> "Hello" (first letter up, rest untouched).
    b["capitalize"] = [](std::vector<V>& a) {
        std::string s = a.empty() ? "" : a[0].AsString();
        if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
        return V{s};
    };
    // title_case("hello world") -> "Hello World" (capitalize each word).
    b["title_case"] = [](std::vector<V>& a) {
        std::string s = a.empty() ? "" : a[0].AsString();
        bool start = true;
        for (auto& c : s) {
            if (std::isspace((unsigned char)c)) { start = true; }
            else { c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c); start = false; }
        }
        return V{s};
    };
    // Repeat a string n times. Script-supplied counts are capped so one line can't hang the frame or exhaust memory
    // (issue #719); longer results are truncated, not errors.
    constexpr int kMaxRepeatCount = 1 << 20;
    auto repeatCount = [](std::vector<V>& a) {
        const double raw = a.size() > 1 ? a[1].AsFloat() : 0.0;
        if (!(raw > 0.0)) return 0;   // also covers NaN
        const double capped = raw < (double)kMaxRepeatCount ? raw : (double)kMaxRepeatCount;
        return (int)capped;
    };
    b["repeat"] = [repeatCount](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        const std::string s = a[0].AsString();
        int n = repeatCount(a);
        // A cap can still make n copies of a long s huge, so the budget is on the result size.
        if (!s.empty()) {
            const std::size_t maxLen = (std::size_t)kMaxRepeatCount * s.size() > (1u << 22) ? (1u << 22) / s.size() : kMaxRepeatCount;
            if ((std::size_t)n > maxLen) n = (int)maxLen;
        }
        std::string out;
        for (int i = 0; i < n; ++i) out += s;
        return V{out};
    };
    b["str_repeat"] = b["repeat"];   // the engine's second copy (no cap) now has the cap too
    // pad_left("7", 3) -> "  7" ; pad_left("7", 3, "0") -> "007"
    b["pad_left"] = [repeatCount](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        std::string s = a[0].AsString();
        const int width = repeatCount(a);
        char fill = (a.size() > 2 && !a[2].AsString().empty()) ? a[2].AsString()[0] : ' ';
        if ((int)s.size() < width) s.insert(s.begin(), (std::size_t)(width - (int)s.size()), fill);
        return V{s};
    };
    b["pad_right"] = [repeatCount](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        std::string s = a[0].AsString();
        const int width = repeatCount(a);
        char fill = (a.size() > 2 && !a[2].AsString().empty()) ? a[2].AsString()[0] : ' ';
        if ((int)s.size() < width) s.append((std::size_t)(width - (int)s.size()), fill);
        return V{s};
    };
    // format("hp={} of {}", 7, 9) -> "hp=7 of 9"; format("{0} / {1:.2f}", a, b): positions and format specs.
    b["format"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        return V{detail::FormatTemplate<V>(a[0].AsString(), a.data() + 1, a.size() - 1)};
    };
    b["split"] = [](std::vector<V>& a) {
        V out = V::MakeArray();
        if (a.empty()) return out;
        std::string s = a[0].AsString();
        std::string sep = a.size() > 1 ? a[1].AsString() : " ";
        auto arr = out.AsArray();
        if (sep.empty()) { for (char c : s) arr->push_back(V{std::string(1, c)}); return out; }
        std::size_t pos = 0, next;
        while ((next = s.find(sep, pos)) != std::string::npos) {
            arr->push_back(V{s.substr(pos, next - pos)});
            pos = next + sep.size();
        }
        arr->push_back(V{s.substr(pos)});
        return out;
    };
    b["join"] = [](std::vector<V>& a) {
        if (a.empty()) return V{std::string{}};
        auto arr = a[0].AsArray();
        std::string sep = a.size() > 1 ? a[1].AsString() : "";
        if (!arr) return V{std::string{}};
        std::string s;
        for (std::size_t i = 0; i < arr->size(); ++i) { if (i) s += sep; s += (*arr)[i].AsString(); detail::CheckTextLength(s.size()); }
        return V{s};
    };
    // ---- Encoding (new): code points, base64 (RFC 4648), hex, percent-encoding (RFC 3986) ----
    // ord("A") -> 65: the first character's code point (UTF-8 aware); -1 for "".
    b["ord"] = [](std::vector<V>& a) {
        const std::vector<std::uint32_t> cps = detail::Utf8Decode(a.empty() ? std::string() : a[0].AsString());
        return V{cps.empty() ? -1.0f : (float)cps[0]};
    };
    // chr(65) -> "A": a code point as UTF-8 text.
    b["chr"] = [](std::vector<V>& a) {
        const double n = a.empty() ? 0.0 : (double)a[0].AsFloat();
        return V{detail::Utf8Encode(n >= 0 && n <= 0x10FFFF ? (std::uint32_t)n : 0xFFFD)};
    };
    // utf8_len("héllo") -> 5 characters (str_len and .Length count bytes).
    b["utf8_len"] = [](std::vector<V>& a) {
        return V{(float)detail::Utf8Decode(a.empty() ? std::string() : a[0].AsString()).size()};
    };
    // utf8_chars("héllo") -> ["h", "é", "l", "l", "o"].
    b["utf8_chars"] = [](std::vector<V>& a) {
        V out = V::MakeArray();
        for (std::uint32_t cp : detail::Utf8Decode(a.empty() ? std::string() : a[0].AsString()))
            out.AsArray()->push_back(V{detail::Utf8Encode(cp)});
        return out;
    };
    b["base64_encode"] = [](std::vector<V>& a) { return V{detail::Base64Encode(a.empty() ? std::string() : a[0].AsString())}; };
    // base64_decode(text): the bytes as text, or "" when it is not base64.
    b["base64_decode"] = [](std::vector<V>& a) {
        std::string out;
        if (a.empty() || !detail::Base64Decode(a[0].AsString(), out)) out.clear();
        return V{out};
    };
    b["hex_encode"] = [](std::vector<V>& a) { return V{detail::HexEncode(a.empty() ? std::string() : a[0].AsString())}; };
    b["hex_decode"] = [](std::vector<V>& a) {
        std::string out;
        if (a.empty() || !detail::HexDecode(a[0].AsString(), out)) out.clear();
        return V{out};
    };
    b["url_encode"] = [](std::vector<V>& a) { return V{detail::UrlEncode(a.empty() ? std::string() : a[0].AsString())}; };
    b["url_decode"] = [](std::vector<V>& a) { return V{detail::UrlDecode(a.empty() ? std::string() : a[0].AsString())}; };
}

/// Calls a function value from a builtin: a lambda, {"__fn": name}, or a function's name as text.
template <class V> using BuiltinCaller = std::function<V(const V& fn, std::vector<V>& args)>;

namespace detail {
/// The order sort() and sort_str() use: numbers by value, anything with text as text.
template <class V> bool SortLess(const V& x, const V& y) {
    if (x.IsString() || y.IsString()) return x.AsString() < y.AsString();
    const std::int64_t* xi = IntOf(x);
    const std::int64_t* yi = IntOf(y);
    if (xi && yi) return *xi < *yi;
    return (double)x.AsFloat() < (double)y.AsFloat();
}
template <class V> bool SetHas(const std::vector<V>& s, const V& x) {
    for (const V& e : s) if (ValuesEqual(e, x)) return true;
    return false;
}
} // namespace detail

/// Lists and maps. Moved from the engine (OkayScriptVM_Math.cpp) with their rules unchanged; the callback ones take a
/// lambda as well as a function's name now (`call`). sort, zip, enumerate, flatten and the set_ functions are new:
/// a set is a list with no item twice (set_of keeps the first of each, in order).
template <class V, class Table> void InstallCollectionBuiltins(Table& b, BuiltinCaller<V> call) {
    using detail::SafeInt;
    b["array"] = [](std::vector<V>& a) {
        V v = V::MakeArray();
        auto arr = v.AsArray();
        for (auto& e : a) arr->push_back(e);   // array(1,2,3) -> [1,2,3]
        return v;
    };
    b["count"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray()) return V{(float)arr->size()};
        return V{0.0f};
    };
    b["push"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray()) arr->push_back(a[1]);
        return a.empty() ? V{} : a[0];
    };
    b["pop"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray())
            if (!arr->empty()) { V last = arr->back(); arr->pop_back(); return last; }
        return V{};
    };
    b["contains"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
            std::string n = a[1].AsString();
            for (auto& e : *arr) if (e.AsString() == n) return V{true};
        }
        return V{false};
    };
    b["index_of"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
            std::string n = a[1].AsString();
            for (std::size_t i = 0; i < arr->size(); ++i)
                if ((*arr)[i].AsString() == n) return V{(float)i};
        }
        return V{-1.0f};
    };
    b["remove_at"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray()) {
            int i = SafeInt(a[1].AsFloat());
            if (i >= 0 && i < (int)arr->size()) arr->erase(arr->begin() + i);
        }
        return a.empty() ? V{} : a[0];
    };
    b["sum"] = [](std::vector<V>& a) {
        float s = 0;
        if (!a.empty()) if (auto arr = a[0].AsArray()) for (auto& e : *arr) s += e.AsFloat();
        return V{s};
    };
    b["min_of"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) {
            float m = (*arr)[0].AsFloat();
            for (auto& e : *arr) m = detail::m::Min(m, e.AsFloat());
            return V{m};
        }
        return V{0.0f};
    };
    b["max_of"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) {
            float m = (*arr)[0].AsFloat();
            for (auto& e : *arr) m = detail::m::Max(m, e.AsFloat());
            return V{m};
        }
        return V{0.0f};
    };
    b["reverse"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray()) std::reverse(arr->begin(), arr->end());
        return a.empty() ? V{} : a[0];
    };
    b["sort_num"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray())
            std::sort(arr->begin(), arr->end(), [](const V& x, const V& y) { return x.AsFloat() < y.AsFloat(); });
        return a.empty() ? V{} : a[0];
    };
    b["sort_str"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray())
            std::sort(arr->begin(), arr->end(), [](const V& x, const V& y) { return x.AsString() < y.AsString(); });
        return a.empty() ? V{} : a[0];
    };
    b["first"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) return arr->front();
        return V{};
    };
    b["last"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty()) return arr->back();
        return V{};
    };
    b["clear"] = [](std::vector<V>& a) {
        if (!a.empty()) { if (auto arr = a[0].AsArray()) arr->clear(); else if (auto m = a[0].AsMap()) m->clear(); }
        return a.empty() ? V{} : a[0];
    };
    b["insert_at"] = [](std::vector<V>& a) {
        if (a.size() >= 3) if (auto arr = a[0].AsArray()) {
            int i = SafeInt(a[1].AsFloat());
            if (i < 0) i = 0;
            if (i > (int)arr->size()) i = (int)arr->size();
            arr->insert(arr->begin() + i, a[2]);
        }
        return a.empty() ? V{} : a[0];
    };
    b["slice"] = [](std::vector<V>& a) {
        V out = V::MakeArray(); auto res = out.AsArray();
        if (!a.empty()) if (auto arr = a[0].AsArray()) {
            int n = (int)arr->size();
            int start = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0;
            int end   = a.size() > 2 ? SafeInt(a[2].AsFloat()) : n;
            if (start < 0) start += n;
            if (end < 0) end += n;
            start = start < 0 ? 0 : (start > n ? n : start);
            end   = end   < 0 ? 0 : (end   > n ? n : end);
            for (int i = start; i < end; ++i) res->push_back((*arr)[i]);
        }
        return out;
    };
    // range(n) -> [0..n), range(lo, hi) -> [lo..hi), range(lo, hi, step).
    b["range"] = [](std::vector<V>& a) {
        V out = V::MakeArray(); auto res = out.AsArray();
        float lo = 0, hi = 0, step = 1;
        if (a.size() == 1) hi = a[0].AsFloat();
        else if (a.size() >= 2) { lo = a[0].AsFloat(); hi = a[1].AsFloat(); if (a.size() >= 3) step = a[2].AsFloat(); }
        if (step == 0) step = 1;
        int guard = 0;
        if (step > 0) for (float v = lo; v < hi && guard < 1000000; v += step, ++guard) res->push_back(V{v});
        else          for (float v = lo; v > hi && guard < 1000000; v += step, ++guard) res->push_back(V{v});
        return out;
    };
    b["map_values"] = [](std::vector<V>& a) {
        V out = V::MakeArray(); auto res = out.AsArray();
        if (!a.empty()) if (auto m = a[0].AsMap()) for (auto& kv : *m) res->push_back(kv.second);
        return out;
    };
    b["map_clear"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto m = a[0].AsMap()) m->clear();
        return a.empty() ? V{} : a[0];
    };
    // map_merge(dst, src): copy src's entries into dst (src wins on conflict).
    b["map_merge"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto d = a[0].AsMap()) if (auto s = a[1].AsMap())
            for (auto& kv : *s) (*d)[kv.first] = kv.second;
        return a.empty() ? V{} : a[0];
    };
    b["map"] = [](std::vector<V>&) { return V::MakeMap(); };
    b["map_set"] = [](std::vector<V>& a) {
        if (a.size() >= 3) if (auto m = a[0].AsMap()) (*m)[a[1].AsString()] = a[2];
        return a.empty() ? V{} : a[0];
    };
    b["map_get"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto m = a[0].AsMap()) {
            auto it = m->find(a[1].AsString());
            if (it != m->end()) return it->second;
        }
        return a.size() > 2 ? a[2] : V{};
    };
    b["map_has"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto m = a[0].AsMap()) return V{m->count(a[1].AsString()) != 0};
        return V{false};
    };
    b["map_remove"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto m = a[0].AsMap()) m->erase(a[1].AsString());
        return a.empty() ? V{} : a[0];
    };
    b["map_keys"] = [](std::vector<V>& a) {
        V out = V::MakeArray();
        if (!a.empty()) if (auto m = a[0].AsMap()) {
            auto arr = out.AsArray();
            for (auto& kv : *m) arr->push_back(V{kv.first});
        }
        return out;
    };
    b["map_count"] = [](std::vector<V>& a) {
        if (!a.empty()) if (auto m = a[0].AsMap()) return V{(float)m->size()};
        return V{0.0f};
    };
    // Higher-order ops: a list and a callback for each item. Each walks a copy of the list: the callback runs script,
    // which may change the very list being walked.
    b["map_fn"] = [call](std::vector<V>& a) {
        V out = V::MakeArray(); auto res = out.AsArray();
        if (a.size() >= 2) if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{e}; res->push_back(call(a[1], ca)); }
        return out;
    };
    b["filter_fn"] = [call](std::vector<V>& a) {
        V out = V::MakeArray(); auto res = out.AsArray();
        if (a.size() >= 2) if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{e}; if (call(a[1], ca).AsBool()) res->push_back(e); }
        return out;
    };
    // reduce_fn(arr, fn, init): acc = fn(acc, element) folded left.
    b["reduce_fn"] = [call](std::vector<V>& a) {
        if (a.size() < 2) return V{};
        V acc = a.size() >= 3 ? a[2] : V{};
        if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{acc, e}; acc = call(a[1], ca); }
        return acc;
    };
    b["for_each"] = [call](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{e}; call(a[1], ca); }
        return a.empty() ? V{} : a[0];
    };
    b["find_fn"] = [call](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{e}; if (call(a[1], ca).AsBool()) return e; }
        return V{};
    };
    b["any_fn"] = [call](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{e}; if (call(a[1], ca).AsBool()) return V{true}; }
        return V{false};
    };
    b["all_fn"] = [call](std::vector<V>& a) {
        if (a.size() >= 2) if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{e}; if (!call(a[1], ca).AsBool()) return V{false}; }
        return V{true};
    };
    b["count_fn"] = [call](std::vector<V>& a) {
        float n = 0;
        if (a.size() >= 2) if (auto arr = a[0].AsArray())
            for (const V& e : std::vector<V>(*arr)) { std::vector<V> ca{e}; if (call(a[1], ca).AsBool()) n += 1.0f; }
        return V{n};
    };
    // ---- New ----
    // sort(xs) sorts in place (numbers by value, text as text) and returns xs; sort(xs, (a, b) => a - b) by a
    // comparison (negative: a first). Stable.
    b["sort"] = [call](std::vector<V>& a) {
        if (a.empty()) return V{};
        if (auto arr = a[0].AsArray()) {
            if (a.size() > 1) {
                std::vector<V> items = *arr;
                std::stable_sort(items.begin(), items.end(), [&](const V& x, const V& y) {
                    std::vector<V> xy{x, y};
                    return call(a[1], xy).AsFloat() < 0.0f;
                });
                *arr = std::move(items);
            } else {
                std::stable_sort(arr->begin(), arr->end(), detail::SortLess<V>);
            }
        }
        return a[0];
    };
    // zip([1, 2], ["a", "b"]) -> [[1, "a"], [2, "b"]] (as long as the shorter list).
    b["zip"] = [](std::vector<V>& a) {
        V out = V::MakeArray();
        auto x = a.size() > 0 ? a[0].AsArray() : nullptr;
        auto y = a.size() > 1 ? a[1].AsArray() : nullptr;
        if (x && y)
            for (std::size_t i = 0; i < x->size() && i < y->size(); ++i)
                out.AsArray()->push_back(detail::ListOf<V>({(*x)[i], (*y)[i]}));
        return out;
    };
    // enumerate(["a", "b"]) -> [[0, "a"], [1, "b"]].
    b["enumerate"] = [](std::vector<V>& a) {
        V out = V::MakeArray();
        if (!a.empty()) if (auto x = a[0].AsArray())
            for (std::size_t i = 0; i < x->size(); ++i) out.AsArray()->push_back(detail::ListOf<V>({V{(float)i}, (*x)[i]}));
        return out;
    };
    // flatten([[1, 2], [3, [4]]]) -> [1, 2, 3, [4]] (one level).
    b["flatten"] = [](std::vector<V>& a) {
        V out = V::MakeArray();
        if (!a.empty()) if (auto x = a[0].AsArray())
            for (const V& e : *x) {
                if (auto inner = e.AsArray()) for (const V& f : *inner) out.AsArray()->push_back(f);
                else out.AsArray()->push_back(e);
            }
        return out;
    };
    // Sets: lists with no item twice (== decides, as for Contains).
    b["set_of"] = [](std::vector<V>& a) {
        V out = V::MakeArray();
        if (!a.empty()) if (auto x = a[0].AsArray())
            for (const V& e : *x) if (!detail::SetHas(*out.AsArray(), e)) out.AsArray()->push_back(e);
        return out;
    };
    b["set_add"] = [](std::vector<V>& a) {   // true when it was not there yet
        if (a.size() >= 2) if (auto s = a[0].AsArray()) {
            if (detail::SetHas(*s, a[1])) return V{false};
            s->push_back(a[1]);
            return V{true};
        }
        return V{false};
    };
    b["set_has"] = [](std::vector<V>& a) {
        if (a.size() >= 2) if (auto s = a[0].AsArray()) return V{detail::SetHas(*s, a[1])};
        return V{false};
    };
    b["set_remove"] = [](std::vector<V>& a) {   // true when it was there
        if (a.size() >= 2) if (auto s = a[0].AsArray())
            for (std::size_t i = 0; i < s->size(); ++i)
                if (ValuesEqual((*s)[i], a[1])) { s->erase(s->begin() + (std::ptrdiff_t)i); return V{true}; }
        return V{false};
    };
    auto setOp = [](int which) {
        return [which](std::vector<V>& a) {
            V out = V::MakeArray();
            auto x = a.size() > 0 ? a[0].AsArray() : nullptr;
            auto y = a.size() > 1 ? a[1].AsArray() : nullptr;
            std::vector<V> none;
            const std::vector<V>& xs = x ? *x : none;
            const std::vector<V>& ys = y ? *y : none;
            auto& o = *out.AsArray();
            for (const V& e : xs) {
                const bool inY = detail::SetHas(ys, e);
                if ((which == 0 || (which == 1 && inY) || (which == 2 && !inY)) && !detail::SetHas(o, e)) o.push_back(e);
            }
            if (which == 0) for (const V& e : ys) if (!detail::SetHas(o, e)) o.push_back(e);
            return out;
        };
    };
    b["set_union"] = setOp(0);
    b["set_intersect"] = setOp(1);
    b["set_diff"] = setOp(2);
}

// ---- JSON: any value (lists and maps included) to text and back, for save data, config and network payloads.
// Moved from the engine (OkayScriptVMInternal.hpp ToJson / JsonReader) and made a template; now it also writes and
// reads \uXXXX escapes (RFC 8259 section 7), writes whole numbers exactly, and can indent.
namespace detail {
inline void JsonEscape(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if ((unsigned char)c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned)(unsigned char)c); out += buf; }
                else out += c;
                break;
        }
    }
    out += '"';
}
template <class V> void ToJson(const V& v, std::string& out, int indent = 0, int depth = 0) {
    if (depth > 256) throw std::runtime_error("to_json: nested too deeply (more than 256 levels)");
    auto newline = [&](int d) { if (indent > 0) { out += '\n'; out.append((std::size_t)(indent * d), ' '); } };
    if (auto arr = v.AsArray()) {
        out += '[';
        for (std::size_t i = 0; i < arr->size(); ++i) {
            if (i) out += ',';
            newline(depth + 1);
            ToJson((*arr)[i], out, indent, depth + 1);
        }
        if (!arr->empty()) newline(depth);
        out += ']';
    } else if (auto m = v.AsMap()) {
        out += '{';
        bool first = true;
        for (auto& kv : *m) {
            if (!first) out += ',';
            first = false;
            newline(depth + 1);
            JsonEscape(kv.first, out);
            out += indent > 0 ? ": " : ":";
            ToJson(kv.second, out, indent, depth + 1);
        }
        if (!first) newline(depth);
        out += '}';
    } else if (v.IsString()) {
        JsonEscape(v.AsString(), out);
    } else if (v.IsBool()) {
        out += v.AsBool() ? "true" : "false";
    } else if (v.IsVec3()) {
        const auto p = v.AsVec3();
        out += '['; out += V{p.x}.AsString(); out += ','; out += V{p.y}.AsString(); out += ','; out += V{p.z}.AsString(); out += ']';
    } else {
        out += v.AsString();   // number -> its formatted form (a whole number: every digit)
    }
    CheckTextLength(out.size());
}
// A recursive-descent JSON parser. On malformed input it returns whatever it managed to parse (best-effort) —
// scripts should validate important data anyway.
template <class V> struct JsonReader {
    const std::string& s; std::size_t i = 0;
    int depth = 0;   // array/object nesting: too deep is an error, not a stack overflow
    explicit JsonReader(const std::string& src) : s(src) {}
    void Ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    V Parse() { Ws(); if (i >= s.size()) return V{}; char c = s[i];
        if (c == '{' || c == '[') {
            if (++depth > 256) throw std::runtime_error("from_json: nested too deeply (more than 256 levels)");
            V v = c == '{' ? Obj() : Arr();
            --depth;
            return v;
        }
        if (c == '"') return V{Str()};
        if (c == 't') { i += 4; return V{true}; }
        if (c == 'f') { i += 5; return V{false}; }
        if (c == 'n') { i += 4; return V{}; }
        return Num();
    }
    unsigned Hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const int d = i < s.size() ? HexDigit(s[i]) : -1;
            if (d < 0) return 0xFFFD;
            v = v * 16 + (unsigned)d;
            ++i;
        }
        return v;
    }
    std::string Str() {
        std::string out; ++i;  // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[++i];
                if (e == 'u') {   // \uXXXX, a surrogate pair for a character past U+FFFF
                    ++i;
                    unsigned cp = Hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        const unsigned lo = Hex4();
                        cp = (lo >= 0xDC00 && lo <= 0xDFFF) ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
                    }
                    out += Utf8Encode(cp);
                    continue;
                }
                out += (e=='n')?'\n':(e=='t')?'\t':(e=='r')?'\r':(e=='b')?'\b':(e=='f')?'\f':e;
            } else out += s[i];
            ++i;
        }
        if (i < s.size()) ++i;  // closing quote
        return out;
    }
    V Num() {
        std::size_t start = i;
        bool whole = true;
        while (i < s.size() && (std::isdigit((unsigned char)s[i])||s[i]=='-'||s[i]=='+'||s[i]=='.'||s[i]=='e'||s[i]=='E')) {
            if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') whole = false;
            ++i;
        }
        const std::string t = s.substr(start, i - start);
        if constexpr (HasInt<V>::value) {   // a whole number too big for a float stays exact
            if (whole && t.size() > 7) {
                char* end = nullptr;
                const long long n = std::strtoll(t.c_str(), &end, 10);
                if (end && *end == 0) return MakeInt<V>((std::int64_t)n);
            }
        }
        if (i == start) { ++i; return V{}; }   // not a value: skip the character
        return V{(float)std::atof(t.c_str())};
    }
    V Arr() {
        V v = V::MakeArray(); auto a = v.AsArray(); ++i;  // '['
        Ws(); if (i < s.size() && s[i] == ']') { ++i; return v; }
        while (i < s.size()) { a->push_back(Parse()); Ws();
            if (i < s.size() && s[i] == ',') { ++i; Ws(); continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            break;
        }
        return v;
    }
    V Obj() {
        V v = V::MakeMap(); auto m = v.AsMap(); ++i;  // '{'
        Ws(); if (i < s.size() && s[i] == '}') { ++i; return v; }
        while (i < s.size()) { Ws();
            if (s[i] != '"') break;
            std::string key = Str(); Ws();
            if (i < s.size() && s[i] == ':') ++i;
            (*m)[key] = Parse(); Ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; break; }
            break;
        }
        return v;
    }
};
} // namespace detail

/// to_json(v [, indent]) / json_stringify and from_json(text) / json_parse.
template <class V, class Table> void InstallJsonBuiltins(Table& b) {
    b["to_json"] = [](std::vector<V>& a) {
        std::string out;
        if (!a.empty()) detail::ToJson(a[0], out, a.size() > 1 ? detail::SafeInt(a[1].AsFloat()) : 0);
        else out = "null";
        return V{out};
    };
    b["from_json"] = [](std::vector<V>& a) {
        if (a.empty()) return V{};
        std::string js = a[0].AsString();   // keep alive: JsonReader holds a reference
        detail::JsonReader<V> r(js);
        return r.Parse();
    };
    b["json_stringify"] = b["to_json"];
    b["json_parse"] = b["from_json"];
}

/// Where random numbers come from: the engine's shared generator, or the CLI's own (detail::Rng).
struct RandomSource {
    std::function<float(float, float)> range;   ///< a float in [lo, hi)
    std::function<int(int, int)> rangeInt;      ///< a whole number in [lo, hi) (lo when hi <= lo)
    std::function<void(std::uint64_t)> seed;    ///< start the sequence again from this seed
};
namespace detail {
/// A small seedable generator for embedders with none of their own: SplitMix64 (the published constants of
/// Steele, Lea and Flood's SplitMix; a 64-bit counter scrambled into each draw).
struct Rng {
    std::uint64_t state = 0x853C49E6748FEA9Bull;
    std::uint64_t Next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float Unit() { return (float)((Next() >> 40) * (1.0 / 16777216.0)); }   // 24 bits: [0, 1)
    RandomSource Source() {
        return RandomSource{
            [this](float lo, float hi) { const float r = lo + (hi - lo) * Unit(); return r < hi || hi <= lo ? r : lo; },
            [this](int lo, int hi) {
                if (hi <= lo) return lo;
                const std::uint64_t span = (std::uint64_t)((std::int64_t)hi - (std::int64_t)lo);
                return (int)((std::int64_t)lo + (std::int64_t)(Next() % span));
            },
            [this](std::uint64_t s) { state = s; }};
    }
};
} // namespace detail

/// rand([lo, hi]) a float (0..1 by default), randi(lo, hi) a whole number lo..hi-1, chance(p), choose(list),
/// shuffle(list) in place, rand_seed(n) to repeat a sequence. Moved from the engine (OkayScriptVM_Input.cpp and
/// _Math.cpp) with the generator taken out: `r` is the engine's shared one there.
template <class V, class Table> void InstallRandomBuiltins(Table& b, RandomSource r) {
    using detail::SafeInt;
    auto src = std::make_shared<RandomSource>(std::move(r));
    b["rand"] = [src](std::vector<V>& a) {
        float lo = a.size() > 0 ? a[0].AsFloat() : 0.0f;
        float hi = a.size() > 1 ? a[1].AsFloat() : 1.0f;
        return V{src->range(lo, hi)};
    };
    b["randi"] = [src](std::vector<V>& a) {
        int lo = a.size() > 0 ? SafeInt(a[0].AsFloat()) : 0;
        int hi = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 1;
        if (hi < lo) std::swap(lo, hi);
        return V{(float)src->rangeInt(lo, hi)};
    };
    // True with probability p (0..1) — random events, drops, spawns.
    b["chance"] = [src](std::vector<V>& a) {
        float p = a.empty() ? 0.5f : a[0].AsFloat();
        return V{src->range(0.0f, 1.0f) < p};
    };
    b["choose"] = [src](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray()) if (!arr->empty())
            return (*arr)[(std::size_t)src->rangeInt(0, (int)arr->size())];   // max is exclusive
        return V{};
    };
    b["shuffle"] = [src](std::vector<V>& a) {
        if (!a.empty()) if (auto arr = a[0].AsArray())
            for (int i = (int)arr->size() - 1; i > 0; --i)
                std::swap((*arr)[i], (*arr)[src->rangeInt(0, i + 1)]);   // 0..i inclusive: a Fisher-Yates step
        return a.empty() ? V{} : a[0];
    };
    b["rand_seed"] = [src](std::vector<V>& a) {
        const std::int64_t* i = a.empty() ? nullptr : detail::IntOf(a[0]);
        src->seed(i ? (std::uint64_t)*i : (std::uint64_t)detail::SafeInt64(a.empty() ? 0.0f : a[0].AsFloat()));
        return V{};
    };
}

/// now() the seconds since 1970 (a whole number where the value type has them: a float can't hold today's time to the
/// second), now_ms() the same in milliseconds, clock() seconds since the program started (for timing),
/// date_text([seconds]) "YYYY-MM-DD hh:mm:ss" in UTC.
template <class V, class Table> void InstallTimeBuiltins(Table& b) {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    b["now"] = [](std::vector<V>&) {
        return detail::MakeInt<V>((std::int64_t)duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
    };
    b["now_ms"] = [](std::vector<V>&) {
        return detail::MakeInt<V>((std::int64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    };
    b["clock"] = [](std::vector<V>&) {
        return V{(float)duration_cast<duration<double>>(steady_clock::now() - start).count()};
    };
    b["date_text"] = [](std::vector<V>& a) {
        std::int64_t t = (std::int64_t)duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        if (!a.empty()) {
            const std::int64_t* i = detail::IntOf(a[0]);
            t = i ? *i : detail::SafeInt64(a[0].AsFloat());
        }
        // Days since 1970-01-01 to a date, by the Gregorian leap-year rule (every 4th year, not every 100th, but
        // every 400th), a year and then a month at a time; times before 1970 count back the same way.
        std::int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
        const std::int64_t secs = t - days * 86400;
        auto leap = [](std::int64_t yr) { return (yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0; };
        std::int64_t y = 1970;
        if (days > 146097LL * 100 || days < -146097LL * 100) days = 0;   // beyond 40000 years: 1970
        while (days >= 146097) { days -= 146097; y += 400; }   // 400 years are always 146097 days
        while (days < 0) { days += 146097; y -= 400; }
        while (days >= (leap(y) ? 366 : 365)) { days -= leap(y) ? 366 : 365; ++y; }
        static const int kMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        std::int64_t mo = 1;
        for (int k = 0; k < 12; ++k) {
            const int len = kMonth[k] + (k == 1 && leap(y) ? 1 : 0);
            if (days < len) break;
            days -= len;
            ++mo;
        }
        const std::int64_t d = days + 1;
        char buf[48];
        std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld %02lld:%02lld:%02lld", (long long)y, (long long)mo, (long long)d,
                      (long long)(secs / 3600), (long long)(secs / 60 % 60), (long long)(secs % 60));
        return V{std::string(buf)};
    };
}

} // namespace okayscript
