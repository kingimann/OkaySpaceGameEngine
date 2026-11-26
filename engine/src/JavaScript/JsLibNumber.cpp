// okay::js library: Number (ECMA-262 21.1) and Math (21.3). Written from those
// sections. toFixed / toPrecision / toExponential round the exact decimal value
// of the double half-up as 21.1.3.3-21.1.3.5 say (picking the larger n on a tie);
// we get the exact digits from the C library's correctly rounded %e with enough
// precision to print every digit, then round them ourselves. toString(radix) for
// radix 2-36 writes the integer part exactly and up to 52 fraction digits.
// Math.random is our own xorshift64* generator, seeded per runtime. Date (21.4) is at the end: see its notes.
#include "JsInternal.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <string>

namespace okay::js {

namespace {

double ThisNum(Runtime& rt, const Value& v, const char* m) {
    if (v.IsNum()) return v.n;
    if (v.IsObj() && v.O()->cls == Cls::NumberBox) return static_cast<BoxObj*>(v.O())->prim.n;
    rt.ThrowType(std::string("Number.prototype.") + m + " needs a number");
}

// x = digits[0].digits[1..] * 10^exp (x > 0, finite): every significant digit, exactly.
void ExactDigits(double x, std::string& digits, int& exp) {
    char buf[1200];
    std::snprintf(buf, sizeof buf, "%.1100e", x);
    const char* e = std::strchr(buf, 'e');
    digits.clear();
    for (const char* c = buf; c < e; ++c)
        if (*c >= '0' && *c <= '9') digits += *c;
    exp = std::atoi(e + 1);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
}

// Round to n significant digits, half up; may carry into a new leading digit (exp + 1).
void RoundDigits(std::string& d, int& exp, int n) {
    if ((int)d.size() <= n) {
        d.append((std::size_t)(n - (int)d.size()), '0');
        return;
    }
    bool up = d[(std::size_t)n] >= '5';
    d.resize((std::size_t)n);
    if (!up) return;
    int i = n - 1;
    for (; i >= 0; --i) {
        if (d[(std::size_t)i] == '9') d[(std::size_t)i] = '0';
        else {
            ++d[(std::size_t)i];
            break;
        }
    }
    if (i < 0) {
        d.insert(d.begin(), '1');
        d.pop_back();
        ++exp;
    }
}

std::string ToFixedText(double x, int f) {
    std::string sign = x < 0 ? "-" : "";
    x = std::fabs(x);
    if (x == 0) return f ? "0." + std::string((std::size_t)f, '0') : "0";
    std::string d;
    int e;
    ExactDigits(x, d, e);
    int keep = e + 1 + f;   // significant digits up to the f-th decimal place
    std::string digits;
    int intDigits;
    if (keep < 0) {
        digits = std::string((std::size_t)f, '0');
        intDigits = 0;
    } else if (keep == 0) {
        bool up = d[0] >= '5';
        digits = std::string((std::size_t)f, '0');
        intDigits = 0;
        if (up) {
            if (f == 0) return sign + "1";
            digits[(std::size_t)f - 1] = '1';
        }
        std::string out = "0";
        if (f) out += "." + digits;
        return (up || x != 0) ? sign + out : out;
    } else {
        int e2 = e;
        RoundDigits(d, e2, keep);
        if (e2 != e) d += '0';   // a carry added a digit in front; keep the same number of decimals
        intDigits = e2 + 1;
        digits = d;
    }
    std::string out;
    if (intDigits <= 0) {
        out = "0";
        if (f) out += "." + std::string((std::size_t)(-intDigits), '0') + digits.substr(0, (std::size_t)(f + intDigits));
    } else {
        out = digits.substr(0, (std::size_t)intDigits);
        if (f) out += "." + digits.substr((std::size_t)intDigits, (std::size_t)f);
    }
    return sign + out;
}

std::string RadixText(double x, int radix) {
    const char* dig = "0123456789abcdefghijklmnopqrstuvwxyz";
    bool neg = x < 0;
    x = std::fabs(x);
    double ip = std::floor(x), fp = x - ip;
    std::string is;
    if (ip == 0) is = "0";
    while (ip > 0) {
        double q = std::floor(ip / radix);
        int r = (int)(ip - q * radix);
        if (r < 0) r = 0;
        if (r >= radix) r = radix - 1;
        is += dig[r];
        ip = q;
    }
    std::string out(is.rbegin(), is.rend());
    if (fp > 0) {
        out += '.';
        for (int i = 0; i < 52 && fp > 0; ++i) {
            fp *= radix;
            int d = (int)std::floor(fp);
            out += dig[d];
            fp -= d;
        }
    }
    return neg ? "-" + out : out;
}

std::uint64_t NextRandom(std::uint64_t& s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
}

double MinMax(Runtime& rt, CallArgs& a, bool isMax) {
    double r = isMax ? -INFINITY : INFINITY;
    bool nan = false;
    for (int i = 0; i < a.argc; ++i) {
        double d = rt.ToNumber(a[i]);
        if (std::isnan(d)) { nan = true; continue; }
        if (isMax ? (d > r || (d == 0 && r == 0 && !std::signbit(d))) : (d < r || (d == 0 && r == 0 && std::signbit(d)))) r = d;
    }
    return nan ? NAN : r;
}

} // namespace

void InstallNumberLib(Runtime& rt) {
    BoxObj* np = rt.Alloc<BoxObj>();
    np->cls = Cls::NumberBox;
    np->prim = Value::Num(0);
    np->proto = rt.objectProto;
    rt.numberProto = np;
    NativeFunc* ctor = DefCtor(rt, "Number", [](Runtime& rt, CallArgs& a) {
        double d = 0;
        if (a.argc) {   // 21.1.1.1: ToNumeric, so Number(10n) is 10
            Value p = rt.ToNumeric(a[0]);
            d = p.IsBig() ? BigToDouble(p) : p.n;
        }
        if (!a.newTarget) return Value::Num(d);
        Object* o = rt.ToObject(Value::Num(d));
        Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
        if (pr.IsObj()) o->proto = pr.O();
        return Value::Obj(o);
    }, 1, np);
    DefValue(rt, ctor, "MAX_SAFE_INTEGER", Value::Num(9007199254740991.0), 0);
    DefValue(rt, ctor, "MIN_SAFE_INTEGER", Value::Num(-9007199254740991.0), 0);
    DefValue(rt, ctor, "EPSILON", Value::Num(2.220446049250313e-16), 0);
    DefValue(rt, ctor, "MAX_VALUE", Value::Num(1.7976931348623157e308), 0);
    DefValue(rt, ctor, "MIN_VALUE", Value::Num(5e-324), 0);
    DefValue(rt, ctor, "POSITIVE_INFINITY", Value::Num(INFINITY), 0);
    DefValue(rt, ctor, "NEGATIVE_INFINITY", Value::Num(-INFINITY), 0);
    DefValue(rt, ctor, "NaN", Value::Num(NAN), 0);
    DefNative(rt, ctor, "isFinite", [](Runtime&, CallArgs& a) { return Value::Bool(a[0].IsNum() && std::isfinite(a[0].n)); }, 1);
    DefNative(rt, ctor, "isNaN", [](Runtime&, CallArgs& a) { return Value::Bool(a[0].IsNum() && std::isnan(a[0].n)); }, 1);
    DefNative(rt, ctor, "isInteger", [](Runtime&, CallArgs& a) {
        return Value::Bool(a[0].IsNum() && std::isfinite(a[0].n) && std::trunc(a[0].n) == a[0].n);
    }, 1);
    DefNative(rt, ctor, "isSafeInteger", [](Runtime&, CallArgs& a) {
        return Value::Bool(a[0].IsNum() && std::isfinite(a[0].n) && std::trunc(a[0].n) == a[0].n && std::fabs(a[0].n) <= 9007199254740991.0);
    }, 1);

    DefNative(rt, np, "valueOf", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisNum(rt, a.thisv, "valueOf")); }, 0);
    DefNative(rt, np, "toString", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toString");
        double rd = a[0].IsUndef() ? 10 : rt.ToIntegerOrInfinity(a[0]);   // checked before the cast (Infinity)
        if (rd < 2 || rd > 36) rt.ThrowRange("toString's radix must be between 2 and 36");
        int radix = (int)rd;
        if (radix == 10 || !std::isfinite(x)) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        return Value::Str(rt.NewStringAscii(RadixText(x, radix).c_str()));
    }, 1);
    // toLocaleString: InstallIntlLib (ECMA-402 15.4.1)
    DefNative(rt, np, "toFixed", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toFixed");
        double f = rt.ToIntegerOrInfinity(a[0]);
        if (f < 0 || f > 100) rt.ThrowRange("toFixed takes 0 to 100 digits");
        if (!std::isfinite(x) || std::fabs(x) >= 1e21) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        return Value::Str(rt.NewStringAscii(ToFixedText(x, (int)f).c_str()));
    }, 1);
    DefNative(rt, np, "toPrecision", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toPrecision");
        if (a[0].IsUndef() || !std::isfinite(x)) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        double pd = rt.ToIntegerOrInfinity(a[0]);
        if (pd < 1 || pd > 100) rt.ThrowRange("toPrecision takes 1 to 100 digits");
        int p = (int)pd;
        std::string sign = x < 0 ? "-" : "";
        x = std::fabs(x);
        std::string d;
        int e;
        if (x == 0) { d = std::string((std::size_t)p, '0'); e = 0; }
        else {
            ExactDigits(x, d, e);
            RoundDigits(d, e, p);
        }
        std::string out;
        if (e < -6 || e >= p) {
            out = d.substr(0, 1);
            if (p > 1) out += "." + d.substr(1);
            out += std::string("e") + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
        } else if (e == p - 1) out = d;
        else if (e >= 0) out = d.substr(0, (std::size_t)e + 1) + "." + d.substr((std::size_t)e + 1);
        else out = "0." + std::string((std::size_t)(-(e + 1)), '0') + d;
        return Value::Str(rt.NewStringAscii((sign + out).c_str()));
    }, 1);
    DefNative(rt, np, "toExponential", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toExponential");
        double fd = rt.ToIntegerOrInfinity(a[0]);
        if (!std::isfinite(x)) return Value::Str(rt.NewString(rt.NumberToU16(x)));
        if (fd < 0 || fd > 100) rt.ThrowRange("toExponential takes 0 to 100 digits");
        std::string sign = x < 0 ? "-" : "";
        x = std::fabs(x);
        std::string d;
        int e = 0;
        if (x == 0) d = a[0].IsUndef() ? "0" : std::string((std::size_t)fd + 1, '0');
        else if (a[0].IsUndef()) {
            // as many digits as it takes to tell x apart (the digits of Number::toString)
            std::string s = Runtime::NumberToStdString(x);
            ExactDigits(x, d, e);
            std::string shortest;
            for (char c : s) {
                if (c == 'e') break;
                if (c >= '0' && c <= '9') shortest += c;
            }
            std::size_t nz = shortest.find_first_not_of('0');
            shortest = nz == std::string::npos ? "0" : shortest.substr(nz);
            while (shortest.size() > 1 && shortest.back() == '0') shortest.pop_back();
            RoundDigits(d, e, (int)shortest.size());
        } else {
            ExactDigits(x, d, e);
            RoundDigits(d, e, (int)fd + 1);
        }
        std::string out = d.substr(0, 1);
        if (d.size() > 1) out += "." + d.substr(1);
        out += std::string("e") + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
        return Value::Str(rt.NewStringAscii((sign + out).c_str()));
    }, 1);
    // Number.parseFloat / parseInt are the global functions (21.1.2.12-13); set after the globals exist
}

void InstallMathLib(Runtime& rt) {
    Object* m = rt.NewObject();
    DefValue(rt, rt.global, "Math", Value::Obj(m));
    m->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Math")), kConfigurable);
    DefValue(rt, m, "E", Value::Num(2.718281828459045), 0);
    DefValue(rt, m, "LN10", Value::Num(2.302585092994046), 0);
    DefValue(rt, m, "LN2", Value::Num(0.6931471805599453), 0);
    DefValue(rt, m, "LOG10E", Value::Num(0.4342944819032518), 0);
    DefValue(rt, m, "LOG2E", Value::Num(1.4426950408889634), 0);
    DefValue(rt, m, "PI", Value::Num(3.141592653589793), 0);
    DefValue(rt, m, "SQRT1_2", Value::Num(0.7071067811865476), 0);
    DefValue(rt, m, "SQRT2", Value::Num(1.4142135623730951), 0);
#define M1(NAME, EXPR) DefNative(rt, m, NAME, [](Runtime& rt, CallArgs& a) { double x = rt.ToNumber(a[0]); return Value::Num(EXPR); }, 1)
    M1("abs", std::fabs(x));
    M1("acos", std::acos(x));
    M1("acosh", std::acosh(x));
    M1("asin", std::asin(x));
    M1("asinh", std::asinh(x));
    M1("atan", std::atan(x));
    M1("atanh", std::atanh(x));
    M1("cbrt", std::cbrt(x));
    M1("ceil", std::ceil(x));
    M1("cos", std::cos(x));
    M1("cosh", std::cosh(x));
    M1("exp", std::exp(x));
    M1("expm1", std::expm1(x));
    M1("floor", std::floor(x));
    M1("fround", (double)(float)x);
    M1("log", std::log(x));
    M1("log1p", std::log1p(x));
    M1("log10", std::log10(x));
    M1("log2", std::log2(x));
    M1("sign", std::isnan(x) ? NAN : x > 0 ? 1.0 : x < 0 ? -1.0 : x);
    M1("sin", std::sin(x));
    M1("sinh", std::sinh(x));
    M1("sqrt", std::sqrt(x));
    M1("tan", std::tan(x));
    M1("tanh", std::tanh(x));
    M1("trunc", std::trunc(x));
#undef M1
    DefNative(rt, m, "round", [](Runtime& rt, CallArgs& a) {   // 21.3.2.28: halves go up (toward +Infinity)
        double x = rt.ToNumber(a[0]);
        if (!std::isfinite(x) || x == 0) return Value::Num(x);
        if (x < 0 && x >= -0.5) return Value::Num(-0.0);
        double r = std::floor(x);
        if (x - r >= 0.5) r += 1;
        return Value::Num(r);
    }, 1);
    DefNative(rt, m, "clz32", [](Runtime& rt, CallArgs& a) {
        std::uint32_t v = rt.ToUint32(a[0]);
        int n = 0;
        if (!v) return Value::Num(32);
        while (!(v & 0x80000000u)) { v <<= 1; ++n; }
        return Value::Num(n);
    }, 1);
    DefNative(rt, m, "imul", [](Runtime& rt, CallArgs& a) {
        std::uint32_t x = rt.ToUint32(a[0]), y = rt.ToUint32(a[1]);
        return Value::Num((double)(std::int32_t)(x * y));
    }, 2);
    DefNative(rt, m, "atan2", [](Runtime& rt, CallArgs& a) {
        double y = rt.ToNumber(a[0]), x = rt.ToNumber(a[1]);
        return Value::Num(std::atan2(y, x));
    }, 2);
    DefNative(rt, m, "pow", [](Runtime& rt, CallArgs& a) {
        double b = rt.ToNumber(a[0]), e = rt.ToNumber(a[1]);
        if (std::isnan(e)) return Value::Num(NAN);
        if (e == 0) return Value::Num(1);
        if ((b == 1 || b == -1) && std::isinf(e)) return Value::Num(NAN);
        return Value::Num(std::pow(b, e));
    }, 2);
    DefNative(rt, m, "max", [](Runtime& rt, CallArgs& a) { return Value::Num(MinMax(rt, a, true)); }, 2);
    DefNative(rt, m, "min", [](Runtime& rt, CallArgs& a) { return Value::Num(MinMax(rt, a, false)); }, 2);
    DefNative(rt, m, "hypot", [](Runtime& rt, CallArgs& a) {
        double s = 0;
        bool inf = false, nan = false;
        std::vector<double> v;
        for (int i = 0; i < a.argc; ++i) v.push_back(rt.ToNumber(a[i]));
        double big = 0;
        for (double d : v) {
            if (std::isinf(d)) inf = true;
            if (std::isnan(d)) nan = true;
            big = std::max(big, std::fabs(d));
        }
        if (inf) return Value::Num(INFINITY);
        if (nan) return Value::Num(NAN);
        if (big == 0) return Value::Num(0);
        for (double d : v) s += (d / big) * (d / big);
        return Value::Num(big * std::sqrt(s));
    }, 2);
    std::uint64_t seed = (std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ (std::uint64_t)(std::uintptr_t)&rt;
    if (!seed) seed = 88172645463325252ull;
    NativeFunc* rnd = DefNative(rt, m, "random", [](Runtime& rt, CallArgs& a) {
        (void)rt;
        std::uint64_t s = (std::uint64_t)a.callee->slots[0].n;
        std::uint64_t bits = (std::uint64_t)a.callee->slots[1].n;
        (void)bits;
        std::uint64_t st = s;
        std::uint64_t r = NextRandom(st);
        a.callee->slots[0] = Value::Num((double)(st & ((1ull << 52) - 1)) + 1);   // keep the state exactly in a double
        return Value::Num((double)(r >> 11) * (1.0 / 9007199254740992.0));
    }, 0);
    rnd->slots = {Value::Num((double)(seed & ((1ull << 52) - 1)) + 1), Value::Num(0)};

    // Number.parseFloat / parseInt are the global functions
    Value numCtor = rt.global->FindRaw(rt.Intern("Number"))->v;
    (void)numCtor;
}

// ============================================================ Date (ECMA-262 21.4)
// Time values are milliseconds since 1970-01-01T00:00:00Z in a double (21.4.1.1); the day, year and month
// arithmetic is the spec's own (21.4.1.3-21.4.1.13: Day, DayFromYear, YearFromTime, MonthFromTime,
// DateFromTime, MakeTime, MakeDay, MakeDate, TimeClip). The local time zone comes from the platform's C
// runtime (localtime_r / localtime_s): we ask it for the local wall clock of a UTC second and take the
// difference, so there is no time zone database of our own. Date.parse reads the Date Time String Format
// of 21.4.1.32 and, as 21.4.3.2 allows, the forms toString and toUTCString write plus the usual
// "Month D, YYYY [h:mm[:ss] [AM|PM]] [GMT+hhmm]" and "M/D/YYYY" forms.
namespace {

constexpr double kMsPerDay = 86400000.0;
constexpr double kMaxTime = 8.64e15;
const char* const kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kDayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

double DayOf(double t) { return std::floor(t / kMsPerDay); }
double TimeInDay(double t) {
    double r = std::fmod(t, kMsPerDay);
    return r < 0 ? r + kMsPerDay : r;
}
double DayFromYear(double y) {
    return 365.0 * (y - 1970) + std::floor((y - 1969) / 4) - std::floor((y - 1901) / 100) + std::floor((y - 1601) / 400);
}
bool LeapYear(double y) {
    std::int64_t n = (std::int64_t)y;
    return (n % 4 == 0 && n % 100 != 0) || n % 400 == 0;
}
double YearFromTime(double t) {
    double day = DayOf(t);
    double y = std::floor(day / 365.2425) + 1970;   // within one year of the answer
    while (DayFromYear(y) > day) y -= 1;
    while (DayFromYear(y + 1) <= day) y += 1;
    return y;
}
// Days before month m (0-11) in a year.
int DaysBeforeMonth(int m, bool leap) {
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    return cum[m] + (leap && m >= 2 ? 1 : 0);
}
struct Fields {
    double year, month, date, hour, minute, second, ms, weekday;
};
Fields Split(double t) {
    Fields f;
    f.year = YearFromTime(t);
    bool leap = LeapYear(f.year);
    int inYear = (int)(DayOf(t) - DayFromYear(f.year));
    int m = 11;
    while (m > 0 && DaysBeforeMonth(m, leap) > inYear) --m;
    f.month = m;
    f.date = inYear - DaysBeforeMonth(m, leap) + 1;
    double tid = TimeInDay(t);
    f.hour = std::floor(tid / 3600000.0);
    f.minute = std::fmod(std::floor(tid / 60000.0), 60.0);
    f.second = std::fmod(std::floor(tid / 1000.0), 60.0);
    f.ms = std::fmod(tid, 1000.0);
    double wd = std::fmod(DayOf(t) + 4, 7.0);
    f.weekday = wd < 0 ? wd + 7 : wd;
    return f;
}
double MakeTime(double h, double m, double s, double ms) {
    if (!std::isfinite(h) || !std::isfinite(m) || !std::isfinite(s) || !std::isfinite(ms)) return NAN;
    return std::trunc(h) * 3600000.0 + std::trunc(m) * 60000.0 + std::trunc(s) * 1000.0 + std::trunc(ms);
}
double MakeDay(double year, double month, double date) {
    if (!std::isfinite(year) || !std::isfinite(month) || !std::isfinite(date)) return NAN;
    double y = std::trunc(year), m = std::trunc(month), dt = std::trunc(date);
    double ym = y + std::floor(m / 12);
    if (!std::isfinite(ym) || std::fabs(ym) > 400000) return NAN;   // past every representable time value
    int mn = (int)(m - std::floor(m / 12) * 12);
    return DayFromYear(ym) + DaysBeforeMonth(mn, LeapYear(ym)) + dt - 1;
}
double MakeDate(double day, double time) {
    double tv = day * kMsPerDay + time;
    return std::isfinite(tv) ? tv : NAN;
}
double TimeClip(double t) {
    if (!std::isfinite(t) || std::fabs(t) > kMaxTime) return NAN;
    return std::trunc(t) + 0.0;
}

// The local time zone's offset from UTC (ms) at the UTC time t, from the C runtime.
double LocalOffset(double t) {
    if (!std::isfinite(t)) return 0;
    double s = std::floor(t / 1000);
    // the C runtimes handle 1970..3000 everywhere (Windows' localtime_s stops at 3000); outside that use the edge's offset
    const double hi = sizeof(std::time_t) >= 8 ? 32503593600.0 : 2147483647.0;
    if (s < 86400) s = 86400;
    if (s > hi) s = hi;
    std::time_t tt = (std::time_t)s;
    std::tm lt{};
#ifdef _WIN32
    if (localtime_s(&lt, &tt) != 0) return 0;
#else
    if (!localtime_r(&tt, &lt)) return 0;
#endif
    double localSec = MakeDay(lt.tm_year + 1900.0, lt.tm_mon, lt.tm_mday) * 86400.0 + lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec;
    return (localSec - s) * 1000.0;
}
double LocalTime(double t) { return t + LocalOffset(t); }
// UTC(t) of 21.4.1.26: the offset in force at the local wall clock time t (the earlier one in a repeated hour).
double UtcFromLocal(double t) {
    if (!std::isfinite(t)) return NAN;
    double guess = t - LocalOffset(t);
    return t - LocalOffset(guess);
}

double NowMs() {
    using namespace std::chrono;
    return (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

double ThisTime(Runtime& rt, const Value& v, const char* m) {
    if (v.IsObj() && v.O()->cls == Cls::DateBox) return static_cast<BoxObj*>(v.O())->prim.n;
    rt.ThrowType(std::string("Date.prototype.") + m + " needs a Date as 'this'");
}
void SetThisTime(const Value& v, double t) { static_cast<BoxObj*>(v.O())->prim = Value::Num(t); }

std::string Pad(double v, int width) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*lld", width, (long long)v);
    return buf;
}
std::string YearText(double y) { return (y < 0 ? "-" : "") + Pad(std::fabs(y), 4); }
std::string OffsetText(double offMs, bool colon) {
    double mins = std::fabs(std::round(offMs / 60000));
    return std::string(offMs < 0 ? "-" : "+") + Pad(std::floor(mins / 60), 2) + (colon ? ":" : "") + Pad(std::fmod(mins, 60), 2);
}
// 21.4.4.41.2 DateString, 21.4.4.41.1 TimeString, 21.4.4.41.3 TimeZoneString (no zone name: it is optional)
std::string DatePart(const Fields& f) {
    return std::string(kDayNames[(int)f.weekday]) + " " + kMonthNames[(int)f.month] + " " + Pad(f.date, 2) + " " + YearText(f.year);
}
std::string TimePart(const Fields& f) { return Pad(f.hour, 2) + ":" + Pad(f.minute, 2) + ":" + Pad(f.second, 2); }
std::string ToDateText(double tv, int which) {   // 0 toString, 1 toDateString, 2 toTimeString
    if (std::isnan(tv)) return "Invalid Date";
    double off = LocalOffset(tv);
    Fields f = Split(tv + off);
    if (which == 1) return DatePart(f);
    std::string tz = " GMT" + OffsetText(off, false);
    if (which == 2) return TimePart(f) + tz;
    return DatePart(f) + " " + TimePart(f) + tz;
}
std::string IsoText(double tv) {
    Fields f = Split(tv);
    std::string y = (f.year >= 0 && f.year <= 9999) ? Pad(f.year, 4) : (f.year < 0 ? "-" : "+") + Pad(std::fabs(f.year), 6);
    return y + "-" + Pad(f.month + 1, 2) + "-" + Pad(f.date, 2) + "T" + TimePart(f) + "." + Pad(f.ms, 3) + "Z";
}
std::string UtcText(double tv) {
    if (std::isnan(tv)) return "Invalid Date";
    Fields f = Split(tv);
    return std::string(kDayNames[(int)f.weekday]) + ", " + Pad(f.date, 2) + " " + kMonthNames[(int)f.month] + " " + YearText(f.year) +
           " " + TimePart(f) + " GMT";
}

// ---- Date.parse
bool Digits(const std::u16string& s, std::size_t& i, int n, double& out) {
    double v = 0;
    for (int k = 0; k < n; ++k, ++i) {
        if (i >= s.size() || s[i] < u'0' || s[i] > u'9') return false;
        v = v * 10 + (s[i] - u'0');
    }
    out = v;
    return true;
}
// 21.4.1.32: [+-YY]YYYY[-MM[-DD]][THH:mm[:ss[.sss]][Z|+HH:mm|-HH:mm]]; NaN when it does not match.
double ParseIso(const std::u16string& s, bool& matched) {
    matched = false;
    std::size_t i = 0;
    double y = 0, mo = 1, d = 1, h = 0, mi = 0, sec = 0, ms = 0;
    if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) {
        bool neg = s[i] == u'-';
        ++i;
        if (!Digits(s, i, 6, y)) return NAN;
        if (neg && y == 0) return NAN;   // -000000 is not allowed
        if (neg) y = -y;
    } else if (!Digits(s, i, 4, y)) return NAN;
    if (i < s.size() && s[i] == u'-') {
        ++i;
        if (!Digits(s, i, 2, mo)) return NAN;
        if (i < s.size() && s[i] == u'-') {
            ++i;
            if (!Digits(s, i, 2, d)) return NAN;
        }
    }
    bool hasTime = false, hasZone = false;
    double off = 0;
    if (i < s.size() && (s[i] == u'T' || s[i] == u't')) {
        ++i;
        hasTime = true;
        if (!Digits(s, i, 2, h) || i >= s.size() || s[i] != u':') return NAN;
        ++i;
        if (!Digits(s, i, 2, mi)) return NAN;
        if (i < s.size() && s[i] == u':') {
            ++i;
            if (!Digits(s, i, 2, sec)) return NAN;
            if (i < s.size() && (s[i] == u'.' || s[i] == u',')) {
                ++i;
                double scale = 100;
                std::size_t start = i;
                while (i < s.size() && s[i] >= u'0' && s[i] <= u'9') {   // more than 3 digits: the rest are dropped
                    ms += (s[i] - u'0') * scale;
                    scale /= 10;
                    ++i;
                }
                if (i == start) return NAN;
                ms = std::floor(ms);
            }
        }
        if (i < s.size() && (s[i] == u'Z' || s[i] == u'z')) {
            ++i;
            hasZone = true;
        } else if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) {
            double sign = s[i] == u'-' ? -1 : 1, oh = 0, om = 0;
            ++i;
            if (!Digits(s, i, 2, oh)) return NAN;
            if (i < s.size() && s[i] == u':') ++i;
            if (!Digits(s, i, 2, om)) return NAN;
            if (oh > 23 || om > 59) return NAN;
            off = sign * (oh * 60 + om) * 60000;
            hasZone = true;
        }
    }
    if (i != s.size()) return NAN;
    matched = true;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 24 || mi > 59 || sec > 59) return NAN;
    if (h == 24 && (mi != 0 || sec != 0 || ms != 0)) return NAN;
    double dayNum = MakeDay(y, mo - 1, d);
    if (std::isnan(dayNum) || Split(dayNum * kMsPerDay).month != mo - 1) return NAN;   // 2021-02-30
    double t = MakeDate(dayNum, MakeTime(h, mi, sec, ms));
    if (hasZone) t -= off;
    else if (hasTime) t = UtcFromLocal(t);   // a date-time with no offset is local time; a date alone is UTC
    return TimeClip(t);
}

bool StartsWithNoCase(const std::u16string& w, const char* p) {
    std::size_t n = std::strlen(p);
    if (w.size() < n) return false;
    for (std::size_t k = 0; k < n; ++k)
        if (JsLower(w[k]) != (char16_t)p[k]) return false;
    return true;
}
bool WordIs(const std::u16string& w, const char* p) { return w.size() == std::strlen(p) && StartsWithNoCase(w, p); }

// The other forms (implementation-defined by 21.4.3.2): words, numbers, h:mm[:ss[.ms]], M/D/Y, Y-M-D, zones.
double ParseLoose(const std::u16string& s) {
    double year = NAN, month = NAN, day = NAN, h = 0, mi = 0, sec = 0, ms = 0;
    int ampm = 0;                     // 1 am, 2 pm
    bool hasZone = false, haveTime = false;
    double off = 0;
    std::vector<double> nums;         // plain numbers in order
    std::size_t i = 0;
    const std::size_t n = s.size();
    auto isDigit = [&](std::size_t k) { return k < n && s[k] >= u'0' && s[k] <= u'9'; };
    auto readNum = [&](double& v, int& count) {
        v = 0;
        count = 0;
        while (isDigit(i)) {
            if (count < 12) v = v * 10 + (s[i] - u'0');
            ++count;
            ++i;
        }
    };
    while (i < n) {
        char16_t c = s[i];
        if (IsJsSpace(c) || c == u',') { ++i; continue; }
        if (c == u'(') {   // a comment such as "(Central European Standard Time)"
            int depth = 0;
            while (i < n) {
                if (s[i] == u'(') ++depth;
                else if (s[i] == u')' && --depth == 0) { ++i; break; }
                ++i;
            }
            continue;
        }
        if ((c == u'+' || c == u'-') && isDigit(i + 1) && (haveTime || hasZone)) {   // +hhmm / -hh:mm after a time or GMT
            double sign = c == u'-' ? -1 : 1, v;
            int cnt;
            ++i;
            readNum(v, cnt);
            double oh, om = 0;
            if (cnt <= 2) {
                oh = v;
                if (i < n && s[i] == u':') {
                    ++i;
                    readNum(om, cnt);
                }
            } else {
                oh = std::floor(v / 100);
                om = std::fmod(v, 100);
            }
            if (oh > 23 || om > 59) return NAN;
            off = sign * (oh * 60 + om) * 60000;
            hasZone = true;
            continue;
        }
        if (isDigit(i)) {
            double v;
            int cnt;
            readNum(v, cnt);
            if (cnt > 9) return NAN;
            if (i < n && s[i] == u':') {   // a time
                if (haveTime) return NAN;
                haveTime = true;
                h = v;
                ++i;
                readNum(mi, cnt);
                if (!cnt) return NAN;
                if (i < n && s[i] == u':') {
                    ++i;
                    readNum(sec, cnt);
                    if (!cnt) return NAN;
                    if (i < n && s[i] == u'.') {
                        ++i;
                        double scale = 100;
                        while (isDigit(i)) { ms += (s[i] - u'0') * scale; scale /= 10; ++i; }
                        ms = std::floor(ms);
                    }
                }
                continue;
            }
            if (i < n && (s[i] == u'/' || (s[i] == u'-' && isDigit(i + 1)))) {   // M/D/Y or Y/M/D or Y-M-D
                char16_t sep = s[i];
                double b = 0, c3 = NAN;
                ++i;
                readNum(b, cnt);
                if (i < n && s[i] == sep && isDigit(i + 1)) {
                    ++i;
                    readNum(c3, cnt);
                }
                if (v > 31 || cnt == 0) {   // Y/M/D
                    year = v;
                    month = b - 1;
                    day = std::isnan(c3) ? 1 : c3;
                } else {                    // M/D/Y
                    month = v - 1;
                    day = b;
                    year = c3;
                }
                continue;
            }
            nums.push_back(v);
            continue;
        }
        if (JsLower(c) >= u'a' && JsLower(c) <= u'z') {
            std::size_t st = i;
            while (i < n && JsLower(s[i]) >= u'a' && JsLower(s[i]) <= u'z') ++i;
            if (i < n && s[i] == u'.') ++i;   // "Sept." / "a.m"
            std::u16string w = s.substr(st, i - st);
            if (!w.empty() && w.back() == u'.') w.pop_back();
            bool known = false;
            static const char* const months[12] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
            static const char* const days[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
            for (int m = 0; m < 12 && !known && w.size() >= 3; ++m)
                if (StartsWithNoCase(w, months[m])) { month = m; known = true; }
            for (int d = 0; d < 7 && !known && w.size() >= 3; ++d)
                if (StartsWithNoCase(w, days[d])) known = true;   // the weekday is ignored
            if (!known) {
                if (WordIs(w, "am") || WordIs(w, "a")) { ampm = 1; known = true; }
                else if (WordIs(w, "pm") || WordIs(w, "p")) { ampm = 2; known = true; }
                else if (WordIs(w, "gmt") || WordIs(w, "utc") || WordIs(w, "ut") || WordIs(w, "z")) { hasZone = true; known = true; }
                else if (WordIs(w, "t") && haveTime == false) known = true;
            }
            if (!known) return NAN;
            continue;
        }
        return NAN;
    }
    // plain numbers: the day and the year (around a month name), or the year of "M/D" written without one
    for (double v : nums) {
        if (std::isnan(day) && v >= 1 && v <= 31 && !std::isnan(month)) day = v;
        else if (std::isnan(year)) year = v;
        else return NAN;
    }
    if (std::isnan(year) || std::isnan(month)) return NAN;
    if (std::isnan(day)) day = 1;
    if (year < 50) year += 2000;
    else if (year < 100) year += 1900;
    if (month < 0 || month > 11 || day < 1 || day > 31 || h > 24 || mi > 59 || sec > 59) return NAN;
    if (ampm) {
        if (h < 1 || h > 12) return NAN;
        if (h == 12) h = 0;
        if (ampm == 2) h += 12;
    }
    double t = MakeDate(MakeDay(year, month, day), MakeTime(h, mi, sec, ms));
    t = hasZone ? t - off : UtcFromLocal(t);
    return TimeClip(t);
}

double ParseDate(Runtime& rt, const std::u16string& raw) {
    rt.Charge(raw.size() / 8 + 1);
    if (raw.size() > 512) return NAN;   // no date text is this long
    std::size_t b = 0, e = raw.size();
    while (b < e && IsJsSpace(raw[b])) ++b;
    while (e > b && IsJsSpace(raw[e - 1])) --e;
    std::u16string s = raw.substr(b, e - b);
    bool matched = false;
    double t = ParseIso(s, matched);
    if (matched) return t;
    return ParseLoose(s);
}

// The date and time fields: 0 year, 1 month, 2 date, 3 hours, 4 minutes, 5 seconds, 6 ms, 7 weekday.
double FieldOf(const Fields& f, int k) {
    switch (k) {
        case 0: return f.year;
        case 1: return f.month;
        case 2: return f.date;
        case 3: return f.hour;
        case 4: return f.minute;
        case 5: return f.second;
        case 6: return f.ms;
        default: return f.weekday;
    }
}

// new Date(y, m[, d[, h[, min[, s[, ms]]]]]) and Date.UTC: the time value of the fields (before UTC()).
double FieldsTime(Runtime& rt, CallArgs& a) {
    double v[7] = {NAN, 0, 1, 0, 0, 0, 0};
    for (int k = 0; k < 7 && k < a.argc; ++k) v[k] = rt.ToNumber(a[k]);
    if (!std::isnan(v[0])) {
        double yi = std::trunc(v[0]);
        if (yi >= 0 && yi <= 99) v[0] = 1900 + yi;
    }
    return MakeDate(MakeDay(v[0], v[1], v[2]), MakeTime(v[3], v[4], v[5], v[6]));
}

Value NewDateObject(Runtime& rt, Object* newTarget, double tv) {
    BoxObj* o = rt.Alloc<BoxObj>();
    o->cls = Cls::DateBox;
    o->prim = Value::Num(tv);
    Value pr = rt.Get(Value::Obj(newTarget), rt.atoms->prototype);
    o->proto = pr.IsObj() ? pr.O() : rt.dateProto;
    return Value::Obj(o);
}

Value DateCtor(Runtime& rt, CallArgs& a) {
    if (!a.newTarget) return Value::Str(rt.NewStringAscii(ToDateText(NowMs(), 0).c_str()));   // Date() is a string
    double tv;
    if (a.argc == 0) tv = NowMs();
    else if (a.argc == 1) {
        if (a[0].IsObj() && a[0].O()->cls == Cls::DateBox) tv = static_cast<BoxObj*>(a[0].O())->prim.n;
        else {
            Value p = rt.ToPrimitive(a[0], 0);
            tv = p.IsStr() ? ParseDate(rt, p.S()->s) : rt.ToNumber(p);
        }
        tv = TimeClip(tv);
    } else tv = TimeClip(UtcFromLocal(FieldsTime(rt, a)));
    return NewDateObject(rt, a.newTarget, tv);
}

// getters: slots {field, utc}
Value DateGet(Runtime& rt, CallArgs& a) {
    double tv = ThisTime(rt, a.thisv, "get");
    if (std::isnan(tv)) return Value::Num(NAN);
    int field = (int)a.callee->slots[0].n;
    bool utc = a.callee->slots[1].b;
    Fields f = Split(utc ? tv : LocalTime(tv));
    double r = FieldOf(f, field);
    if (field == 8) r = f.year - 1900;   // getYear (B.2.3.1)
    return Value::Num(r);
}

// setters (21.4.4.20-28): slots {first field, how many arguments it takes, utc}
Value DateSet(Runtime& rt, CallArgs& a) {
    double tv = ThisTime(rt, a.thisv, "set");
    int first = (int)a.callee->slots[0].n, count = (int)a.callee->slots[1].n;
    bool utc = a.callee->slots[2].b;
    double args[4];
    int given = a.argc < 1 ? 1 : (a.argc < count ? a.argc : count);
    for (int k = 0; k < given; ++k) args[k] = rt.ToNumber(a[k]);   // converted before the NaN check, as the spec orders it
    double t;
    if (first == 0) t = std::isnan(tv) ? 0 : (utc ? tv : LocalTime(tv));   // setFullYear starts from +0 on an invalid date
    else {
        if (std::isnan(tv)) return Value::Num(NAN);
        t = utc ? tv : LocalTime(tv);
    }
    Fields f = Split(t);
    double v[7] = {f.year, f.month, f.date, f.hour, f.minute, f.second, f.ms};
    for (int k = 0; k < given; ++k) v[first + k] = args[k];
    double nt = MakeDate(MakeDay(v[0], v[1], v[2]), MakeTime(v[3], v[4], v[5], v[6]));
    double u = TimeClip(utc ? nt : UtcFromLocal(nt));
    SetThisTime(a.thisv, u);
    return Value::Num(u);
}

} // namespace

void InstallDateLib(Runtime& rt) {
    Object* dp = rt.NewObject(rt.objectProto);   // Date.prototype is an ordinary object (21.4.4), not a Date
    rt.dateProto = dp;
    NativeFunc* ctor = DefCtor(rt, "Date", DateCtor, 7, dp);
    DefNative(rt, ctor, "now", [](Runtime&, CallArgs&) { return Value::Num(NowMs()); }, 0);
    DefNative(rt, ctor, "parse", [](Runtime& rt, CallArgs& a) { return Value::Num(ParseDate(rt, rt.ToU16(a[0]))); }, 1);
    DefNative(rt, ctor, "UTC", [](Runtime& rt, CallArgs& a) { return Value::Num(TimeClip(FieldsTime(rt, a))); }, 7);

    static const char* const getters[][2] = {
        {"getFullYear", "getUTCFullYear"}, {"getMonth", "getUTCMonth"}, {"getDate", "getUTCDate"},
        {"getHours", "getUTCHours"}, {"getMinutes", "getUTCMinutes"}, {"getSeconds", "getUTCSeconds"},
        {"getMilliseconds", "getUTCMilliseconds"}, {"getDay", "getUTCDay"},
    };
    for (int k = 0; k < 8; ++k)
        for (int u = 0; u < 2; ++u) DefNative(rt, dp, getters[k][u], DateGet, 0)->slots = {Value::Num(k), Value::Bool(u == 1)};
    DefNative(rt, dp, "getYear", DateGet, 0)->slots = {Value::Num(8), Value::Bool(false)};
    struct SetterDef { const char* local; const char* utc; int first, count; };
    static const SetterDef setters[] = {
        {"setFullYear", "setUTCFullYear", 0, 3}, {"setMonth", "setUTCMonth", 1, 2}, {"setDate", "setUTCDate", 2, 1},
        {"setHours", "setUTCHours", 3, 4}, {"setMinutes", "setUTCMinutes", 4, 3}, {"setSeconds", "setUTCSeconds", 5, 2},
        {"setMilliseconds", "setUTCMilliseconds", 6, 1},
    };
    for (const SetterDef& s : setters)
        for (int u = 0; u < 2; ++u)
            DefNative(rt, dp, u ? s.utc : s.local, DateSet, s.count)->slots = {Value::Num(s.first), Value::Num(s.count), Value::Bool(u == 1)};
    DefNative(rt, dp, "setYear", [](Runtime& rt, CallArgs& a) {   // B.2.3.2
        double tv = ThisTime(rt, a.thisv, "setYear");
        double t = std::isnan(tv) ? 0 : LocalTime(tv);
        double y = rt.ToNumber(a[0]);
        if (std::isnan(y)) { SetThisTime(a.thisv, NAN); return Value::Num(NAN); }
        double yi = std::trunc(y);
        if (yi >= 0 && yi <= 99) yi += 1900;
        Fields f = Split(t);
        double u = TimeClip(UtcFromLocal(MakeDate(MakeDay(yi, f.month, f.date), TimeInDay(t))));
        SetThisTime(a.thisv, u);
        return Value::Num(u);
    }, 1);
    DefNative(rt, dp, "getTime", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisTime(rt, a.thisv, "getTime")); }, 0);
    DefNative(rt, dp, "valueOf", [](Runtime& rt, CallArgs& a) { return Value::Num(ThisTime(rt, a.thisv, "valueOf")); }, 0);
    DefNative(rt, dp, "setTime", [](Runtime& rt, CallArgs& a) {
        ThisTime(rt, a.thisv, "setTime");
        double t = TimeClip(rt.ToNumber(a[0]));
        SetThisTime(a.thisv, t);
        return Value::Num(t);
    }, 1);
    DefNative(rt, dp, "getTimezoneOffset", [](Runtime& rt, CallArgs& a) {
        double tv = ThisTime(rt, a.thisv, "getTimezoneOffset");
        if (std::isnan(tv)) return Value::Num(NAN);
        return Value::Num((tv - LocalTime(tv)) / 60000.0);
    }, 0);
    DefNative(rt, dp, "toString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(ToDateText(ThisTime(rt, a.thisv, "toString"), 0).c_str()));
    }, 0);
    DefNative(rt, dp, "toDateString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(ToDateText(ThisTime(rt, a.thisv, "toDateString"), 1).c_str()));
    }, 0);
    DefNative(rt, dp, "toTimeString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(ToDateText(ThisTime(rt, a.thisv, "toTimeString"), 2).c_str()));
    }, 0);
    NativeFunc* utcString = DefNative(rt, dp, "toUTCString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringAscii(UtcText(ThisTime(rt, a.thisv, "toUTCString")).c_str()));
    }, 0);
    DefValue(rt, dp, "toGMTString", Value::Obj(utcString));   // B.2.3.3: the same function object
    DefNative(rt, dp, "toISOString", [](Runtime& rt, CallArgs& a) {
        double tv = ThisTime(rt, a.thisv, "toISOString");
        if (!std::isfinite(tv)) rt.ThrowRange("toISOString: invalid date");
        return Value::Str(rt.NewStringAscii(IsoText(tv).c_str()));
    }, 0);
    DefNative(rt, dp, "toJSON", [](Runtime& rt, CallArgs& a) {   // 21.4.4.37
        Object* o = rt.ToObject(a.thisv);
        Value tv = rt.ToPrimitive(Value::Obj(o), 1);
        if (tv.IsNum() && !std::isfinite(tv.n)) return Value::Null();
        Value f = rt.Get(Value::Obj(o), rt.Intern("toISOString"));
        if (!rt.IsCallable(f)) rt.ThrowType("toJSON: toISOString is not a function");
        return rt.Call(f, Value::Obj(o), nullptr, 0);
    }, 1);
    // toLocaleString, toLocaleDateString, toLocaleTimeString: InstallIntlLib (the en-US forms of ECMA-402)
    // 21.4.4.45 Date.prototype[@@toPrimitive]: "default" means "string" for dates
    NativeFunc* toPrim = rt.NewNative([](Runtime& rt, CallArgs& a) {
        if (!a.thisv.IsObj()) rt.ThrowType("Date.prototype[Symbol.toPrimitive] needs an object");
        std::u16string hint = a[0].IsStr() ? a[0].S()->s : u"";
        bool str;
        if (hint == u"string" || hint == u"default") str = true;
        else if (hint == u"number") str = false;
        else rt.ThrowType("Date.prototype[Symbol.toPrimitive]: bad hint");
        GcObj* order[2] = {rt.atoms->toString, rt.atoms->valueOf};
        if (!str) std::swap(order[0], order[1]);
        for (GcObj* k : order) {   // 7.1.1.1 OrdinaryToPrimitive
            Value f = rt.Get(a.thisv, k);
            if (rt.IsCallable(f)) {
                Value r = rt.Call(f, a.thisv, nullptr, 0);
                if (!r.IsObj()) return r;
            }
        }
        rt.ThrowType("this object can not be turned into a primitive value");
    }, "[Symbol.toPrimitive]", 1);
    dp->AddRaw(rt.symToPrimitive, Value::Obj(toPrim), kConfigurable);
}

// For console.log: the ISO text of a Date, or "Invalid Date".
std::string DateDisplay(Object* o) {
    double tv = static_cast<BoxObj*>(o)->prim.n;
    return std::isnan(tv) ? "Invalid Date" : IsoText(tv);
}

// ============================================================ BigInt (ECMA-262 6.1.6.2, 7.1.13-7.1.15, 21.2)
// Sign and magnitude, the magnitude in 32-bit limbs, least significant first, never with a zero top limb (zero
// is the empty magnitude and is never negative). The arithmetic is the schoolbook kind (Knuth, The Art of
// Computer Programming vol. 2, 4.3.1: addition, subtraction, multiplication and Algorithm D for division);
// bitwise operators work on the infinite two's complement form 6.1.6.2 describes. Values are capped at
// kMaxBigBits bits (a RangeError past that) and every operation charges the budget for its work before doing
// it, so `10n ** 10n ** 10n` or a huge multiplication stops instead of hanging.
namespace {

constexpr std::size_t kMaxBigBits = 1u << 20;
constexpr std::size_t kMaxBigLimbs = kMaxBigBits / 32;

int CmpMag(const Limbs& a, const Limbs& b) {   // no zero top limbs, so a longer magnitude is the larger one
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    auto diff = std::mismatch(a.rbegin(), a.rend(), b.rbegin());
    if (diff.first == a.rend()) return 0;
    return *diff.first < *diff.second ? -1 : 1;
}
Limbs AddMag(const Limbs& a, const Limbs& b) {
    const Limbs& x = a.size() >= b.size() ? a : b;
    const Limbs& y = a.size() >= b.size() ? b : a;
    Limbs r(x.size() + 1);
    std::uint64_t c = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        c += (std::uint64_t)x[i] + (i < y.size() ? y[i] : 0);
        r[i] = (std::uint32_t)c;
        c >>= 32;
    }
    r[x.size()] = (std::uint32_t)c;
    DropTopZeros(r);
    return r;
}
Limbs SubMag(const Limbs& a, const Limbs& b) {   // |a| >= |b|
    Limbs r(a.size());
    std::int64_t br = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::int64_t d = (std::int64_t)a[i] - (i < b.size() ? b[i] : 0) - br;
        br = d < 0;
        r[i] = (std::uint32_t)(d + (br << 32));
    }
    DropTopZeros(r);
    return r;
}
std::size_t BitLength(const Limbs& m) {
    if (m.empty()) return 0;
    std::size_t n = (m.size() - 1) * 32;
    for (std::uint32_t top = m.back(); top; top >>= 1) ++n;
    return n;
}
void CheckSize(Runtime& rt, std::size_t limbs) {
    if (limbs > kMaxBigLimbs + 1) rt.ThrowRange("the BigInt is too big (over 1048576 bits)");
}
Limbs MulMag(Runtime& rt, const Limbs& a, const Limbs& b) {
    if (a.empty() || b.empty()) return {};
    CheckSize(rt, a.size() + b.size() - 1);
    rt.Charge((std::uint64_t)a.size() * b.size() / 16 + 1);
    Limbs r(a.size() + b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t c = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            c += (std::uint64_t)a[i] * b[j] + r[i + j];
            r[i + j] = (std::uint32_t)c;
            c >>= 32;
        }
        r[i + b.size()] = (std::uint32_t)c;
    }
    DropTopZeros(r);
    CheckSize(rt, r.size());
    return r;
}
// q = a / b, r = a % b on magnitudes (b not zero): Knuth's Algorithm D with a normalized divisor.
void DivMag(Runtime& rt, const Limbs& a, const Limbs& b, Limbs& q, Limbs& r) {
    q.clear();
    r.clear();
    if (CmpMag(a, b) < 0) { r = a; return; }
    rt.Charge((std::uint64_t)(a.size() - b.size() + 1) * b.size() / 16 + 1);
    if (b.size() == 1) {
        q.assign(a.size(), 0);
        std::uint64_t rem = 0;
        for (std::size_t i = a.size(); i-- > 0;) {
            std::uint64_t cur = (rem << 32) | a[i];
            q[i] = (std::uint32_t)(cur / b[0]);
            rem = cur % b[0];
        }
        DropTopZeros(q);
        if (rem) r.push_back((std::uint32_t)rem);
        return;
    }
    int s = 0;   // shift so the divisor's top bit is set
    for (std::uint32_t top = b.back(); !(top & 0x80000000u); top <<= 1) ++s;
    const std::size_t n = b.size(), m = a.size() - n;
    Limbs v(n), u(a.size() + 1);
    for (std::size_t i = n; i-- > 0;) v[i] = (b[i] << s) | (s && i ? b[i - 1] >> (32 - s) : 0);
    u[a.size()] = s ? a.back() >> (32 - s) : 0;
    for (std::size_t i = a.size(); i-- > 0;) u[i] = (a[i] << s) | (s && i ? a[i - 1] >> (32 - s) : 0);
    q.assign(m + 1, 0);
    const std::uint64_t B = 1ull << 32;
    for (std::size_t j = m + 1; j-- > 0;) {
        // estimate the quotient digit from the top two limbs, then correct it at most twice (step D3)
        std::uint64_t num = ((std::uint64_t)u[j + n] << 32) | u[j + n - 1];
        std::uint64_t qh = num / v[n - 1], rh = num % v[n - 1];
        while (qh >= B || qh * v[n - 2] > ((rh << 32) | u[j + n - 2])) {
            --qh;
            rh += v[n - 1];
            if (rh >= B) break;
        }
        // multiply and subtract (D4); if that went below zero, add back once (D6)
        std::int64_t borrow = 0;
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < n; ++i) {
            std::uint64_t p = qh * v[i] + carry;
            carry = p >> 32;
            std::int64_t d = (std::int64_t)u[i + j] - (std::int64_t)(p & 0xFFFFFFFFu) - borrow;
            borrow = d < 0;
            u[i + j] = (std::uint32_t)(d + (borrow << 32));
        }
        std::int64_t d = (std::int64_t)u[j + n] - (std::int64_t)carry - borrow;
        u[j + n] = (std::uint32_t)d;
        if (d < 0) {
            --qh;
            std::uint64_t c = 0;
            for (std::size_t i = 0; i < n; ++i) {
                c += (std::uint64_t)u[i + j] + v[i];
                u[i + j] = (std::uint32_t)c;
                c >>= 32;
            }
            u[j + n] += (std::uint32_t)c;
        }
        q[j] = (std::uint32_t)qh;
    }
    DropTopZeros(q);
    r.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) r[i] = (u[i] >> s) | (s ? u[i + 1] << (32 - s) : 0);
    DropTopZeros(r);
}
Limbs ShlMag(Runtime& rt, const Limbs& a, std::uint64_t k) {
    if (a.empty()) return {};
    if (k > kMaxBigBits) rt.ThrowRange("the BigInt is too big (over 1048576 bits)");
    CheckSize(rt, a.size() + (std::size_t)(k / 32));
    std::size_t w = (std::size_t)(k / 32), s = (std::size_t)(k % 32);
    Limbs r(a.size() + w + 1, 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        r[i + w] |= a[i] << s;
        if (s) r[i + w + 1] |= a[i] >> (32 - s);
    }
    DropTopZeros(r);
    CheckSize(rt, r.size());
    return r;
}
Limbs ShrMag(const Limbs& a, std::uint64_t k, bool& lostBits) {
    lostBits = false;
    if (k >= (std::uint64_t)a.size() * 32) { lostBits = !a.empty(); return {}; }
    std::size_t w = (std::size_t)(k / 32), s = (std::size_t)(k % 32);
    for (std::size_t i = 0; i < w; ++i) lostBits |= a[i] != 0;
    if (s && (a[w] & ((1u << s) - 1))) lostBits = true;
    Limbs r(a.size() - w);
    for (std::size_t i = 0; i < r.size(); ++i)
        r[i] = (a[i + w] >> s) | (s && i + w + 1 < a.size() ? a[i + w + 1] << (32 - s) : 0);
    DropTopZeros(r);
    return r;
}
Limbs SmallMag(std::uint64_t v) {
    Limbs r;
    while (v) { r.push_back((std::uint32_t)v); v >>= 32; }
    return r;
}

using Big = BigNum;
Big Norm(Big b) {
    DropTopZeros(b.mag);
    if (b.mag.empty()) b.neg = false;
    return b;
}
Big AddBig(const Big& a, const Big& b) {
    if (a.neg == b.neg) return Norm({a.neg, AddMag(a.mag, b.mag)});
    int c = CmpMag(a.mag, b.mag);
    if (c == 0) return {};
    return c > 0 ? Norm({a.neg, SubMag(a.mag, b.mag)}) : Norm({b.neg, SubMag(b.mag, a.mag)});
}
Big NegBig(Big a) {
    if (!a.mag.empty()) a.neg = !a.neg;
    return a;
}
// the infinite two's complement form, `len` limbs long (len covers the sign)
Limbs ToTwos(const Big& a, std::size_t len) {
    Limbs r(len, 0);
    for (std::size_t i = 0; i < a.mag.size() && i < len; ++i) r[i] = a.mag[i];
    if (a.neg) {
        std::uint64_t c = 1;
        for (std::size_t i = 0; i < len; ++i) {
            c += (std::uint32_t)~r[i];
            r[i] = (std::uint32_t)c;
            c >>= 32;
        }
    }
    return r;
}
Big FromTwos(Limbs r) {
    Big b;
    b.neg = !r.empty() && (r.back() & 0x80000000u);
    if (b.neg) {
        std::uint64_t c = 1;
        for (std::size_t i = 0; i < r.size(); ++i) {
            c += (std::uint32_t)~r[i];
            r[i] = (std::uint32_t)c;
            c >>= 32;
        }
    }
    b.mag = std::move(r);
    return Norm(std::move(b));
}
Big ShiftBig(Runtime& rt, const Big& a, const Big& by, bool left) {
    // 6.1.6.2.9 / .10: a << k is a * 2^k; a >> k is floor(a / 2^k); a negative k shifts the other way
    bool goLeft = left != by.neg;
    std::uint64_t k;
    if (by.mag.size() > 2) {
        if (goLeft && !a.mag.empty()) rt.ThrowRange("the BigInt is too big (over 1048576 bits)");
        k = ~0ull;
    } else k = by.mag.empty() ? 0 : by.mag.size() == 1 ? by.mag[0] : ((std::uint64_t)by.mag[1] << 32) | by.mag[0];
    if (goLeft) return Norm({a.neg, ShlMag(rt, a.mag, k)});
    bool lost;
    Big r = Norm({a.neg, ShrMag(a.mag, k, lost)});
    if (a.neg && lost) r = AddBig(r, Big{true, {1}});   // floor for negative numbers
    return r;
}
Big PowBig(Runtime& rt, const Big& base, const Big& e) {
    if (e.neg) rt.ThrowRange("a BigInt exponent can not be negative");
    if (e.mag.empty()) return Big{false, {1}};
    if (base.mag.empty()) return {};
    if (base.mag.size() == 1 && base.mag[0] == 1) return Norm({base.neg && (e.mag[0] & 1), {1}});
    std::size_t bits = BitLength(base.mag);
    if (e.mag.size() > 1 || (double)(bits - 1) * e.mag[0] > (double)kMaxBigBits) rt.ThrowRange("the BigInt is too big (over 1048576 bits)");
    std::uint32_t n = e.mag[0];
    Big r{false, {1}}, b = base;
    while (true) {   // square and multiply
        if (n & 1) r = Norm({r.neg != b.neg, MulMag(rt, r.mag, b.mag)});
        n >>= 1;
        if (!n) break;
        b = Norm({false, MulMag(rt, b.mag, b.mag)});
    }
    return r;
}

const Big& BigOf(const Value& v) { return static_cast<BigIntV*>(v.p)->v; }

} // namespace

void DropTopZeros(Limbs& m) {
    while (!m.empty() && m.back() == 0) m.pop_back();
}

Value NewBigValue(Runtime& rt, bool neg, Limbs mag) {
    DropTopZeros(mag);
    CheckSize(rt, mag.size());
    BigIntV* b = rt.Alloc<BigIntV>(mag.size() * 4);
    b->v.neg = neg && !mag.empty();
    b->v.mag = std::move(mag);
    return Value::Big(b);
}
static Value Wrap(Runtime& rt, Big b) { return NewBigValue(rt, b.neg, std::move(b.mag)); }

bool BigIsZero(const Value& v) { return static_cast<BigIntV*>(v.p)->v.mag.empty(); }

bool BigEquals(const Value& a, const Value& b) {
    const BigIntV* x = static_cast<BigIntV*>(a.p);
    const BigIntV* y = static_cast<BigIntV*>(b.p);
    return x->v.neg == y->v.neg && x->v.mag == y->v.mag;
}
std::size_t BigHash(const Value& v) {
    const BigIntV* x = static_cast<BigIntV*>(v.p);
    std::uint64_t h = x->v.neg ? 0x51 : 0x17;
    for (std::uint32_t l : x->v.mag) h = (h ^ l) * 0x100000001B3ull;
    return (std::size_t)(h ^ (h >> 31));
}

// 6.1.6.2.21 BigInt::toString (radix 2-36): digits come from dividing by the biggest power of the radix that fits a limb
std::u16string BigToString(Runtime& rt, const Value& v, int radix) {
    const BigIntV* x = static_cast<BigIntV*>(v.p);
    if (x->v.mag.empty()) return u"0";
    rt.Charge((std::uint64_t)x->v.mag.size() * x->v.mag.size() / 4 + 1);
    std::uint32_t chunk = (std::uint32_t)radix;
    int per = 1;
    while ((std::uint64_t)chunk * radix <= 0xFFFFFFFFull) { chunk *= (std::uint32_t)radix; ++per; }
    Limbs cur = x->v.mag;
    std::string digits;   // least significant first
    while (!cur.empty()) {
        std::uint64_t rem = 0;
        for (std::size_t i = cur.size(); i-- > 0;) {
            std::uint64_t c = (rem << 32) | cur[i];
            cur[i] = (std::uint32_t)(c / chunk);
            rem = c % chunk;
        }
        DropTopZeros(cur);
        for (int k = 0; k < per && (rem || !cur.empty()); ++k) {
            digits += "0123456789abcdefghijklmnopqrstuvwxyz"[rem % (std::uint32_t)radix];
            rem /= (std::uint32_t)radix;
        }
    }
    std::u16string out;
    if (x->v.neg) out += u'-';
    for (std::size_t i = digits.size(); i-- > 0;) out += (char16_t)digits[i];
    return out;
}

// 7.1.14 StringToBigInt: false when the text is not a StringIntegerLiteral (RangeError when it is too long)
bool StringToBigInt(Runtime& rt, const std::u16string& s0, Value& out, bool charge) {
    std::size_t b = 0, e = s0.size();
    while (b < e && IsJsSpace(s0[b])) ++b;
    while (e > b && IsJsSpace(s0[e - 1])) --e;
    int radix = 10;
    bool neg = false;
    if (e - b > 2 && s0[b] == u'0') {
        char16_t k = s0[b + 1] | 0x20;
        if (k == u'x') radix = 16;
        else if (k == u'o') radix = 8;
        else if (k == u'b') radix = 2;
        if (radix != 10) b += 2;
    }
    if (radix == 10 && b < e && (s0[b] == u'+' || s0[b] == u'-')) {
        neg = s0[b] == u'-';
        ++b;
        if (b == e) return false;
    }
    if (e - b > kMaxBigBits) rt.ThrowRange("the BigInt is too big (over 1048576 bits)");
    if (charge) rt.Charge((std::uint64_t)(e - b) * (e - b) / 512 + 1);
    Limbs mag;
    for (std::size_t i = b; i < e; ++i) {
        char16_t c = s0[i];
        int d = c >= u'0' && c <= u'9' ? c - u'0' : (c | 0x20) >= u'a' && (c | 0x20) <= u'z' ? (c | 0x20) - u'a' + 10 : 99;
        if (d >= radix) return false;
        std::uint64_t carry = (std::uint64_t)d;
        for (std::uint32_t& l : mag) {
            carry += (std::uint64_t)l * (std::uint64_t)radix;
            l = (std::uint32_t)carry;
            carry >>= 32;
        }
        if (carry) mag.push_back((std::uint32_t)carry);
    }
    out = NewBigValue(rt, neg, std::move(mag));
    return true;
}

// 21.2.1.1.1 NumberToBigInt (the number is an integer)
Value BigFromDouble(Runtime& rt, double d) {
    if (!std::isfinite(d) || std::trunc(d) != d) rt.ThrowRange("only an integer Number can become a BigInt");
    bool neg = d < 0;
    d = std::fabs(d);
    if (d < 18446744073709551616.0) return NewBigValue(rt, neg, SmallMag((std::uint64_t)d));
    int ex;
    double m = std::frexp(d, &ex);   // d = m * 2^ex, m in [0.5, 1)
    std::uint64_t mant = (std::uint64_t)std::ldexp(m, 53);
    return NewBigValue(rt, neg, ShlMag(rt, SmallMag(mant), (std::uint64_t)(ex - 53)));
}

// 6.1.6.2 to Number: the nearest double, ties to even (the top 64 bits with a sticky bit for the rest)
double BigToDouble(const Value& v) {
    const BigIntV* x = static_cast<BigIntV*>(v.p);
    std::size_t bits = BitLength(x->v.mag);
    double r;
    if (bits <= 64) {
        std::uint64_t u = 0;
        for (std::size_t i = x->v.mag.size(); i-- > 0;) u = (u << 32) | x->v.mag[i];
        r = (double)u;
    } else {
        if (bits > 1100) return x->v.neg ? -INFINITY : INFINITY;
        bool lost;
        Limbs top = ShrMag(x->v.mag, bits - 64, lost);
        std::uint64_t u = ((std::uint64_t)top[1] << 32) | top[0];
        if (lost) u |= 1;
        r = std::ldexp((double)u, (int)(bits - 64));
    }
    return x->v.neg ? -r : r;
}

// Compare a BigInt with a Number (7.2.13 IsLessThan, 7.2.14): -1, 0, 1, or 2 for NaN
int BigCompareNum(const Value& a, double d) {
    if (std::isnan(d)) return 2;
    if (std::isinf(d)) return d > 0 ? -1 : 1;
    const BigIntV* x = static_cast<BigIntV*>(a.p);
    double fl = std::floor(d);
    Big y;
    y.neg = fl < 0;
    double m = std::fabs(fl);
    if (m < 18446744073709551616.0) y.mag = SmallMag((std::uint64_t)m);
    else {
        int ex;
        double fr = std::frexp(m, &ex);
        Limbs base = SmallMag((std::uint64_t)std::ldexp(fr, 53));
        std::size_t k = (std::size_t)(ex - 53);
        y.mag.assign(k / 32, 0);
        Limbs shifted(base.size() + 1, 0);
        for (std::size_t i = 0; i < base.size(); ++i) {
            shifted[i] |= base[i] << (k % 32);
            if (k % 32) shifted[i + 1] |= base[i] >> (32 - k % 32);
        }
        y.mag.insert(y.mag.end(), shifted.begin(), shifted.end());
    }
    y = Norm(std::move(y));
    int c;
    if (x->v.neg != y.neg) c = x->v.neg ? -1 : 1;
    else {
        c = CmpMag(x->v.mag, y.mag);
        if (x->v.neg) c = -c;
    }
    if (c == 0 && fl != d) return -1;   // x == floor(d) < d
    return c;
}
int BigCompare(const Value& a, const Value& b) {
    const BigIntV* x = static_cast<BigIntV*>(a.p);
    const BigIntV* y = static_cast<BigIntV*>(b.p);
    if (x->v.neg != y->v.neg) return x->v.neg ? -1 : 1;
    int c = CmpMag(x->v.mag, y->v.mag);
    return x->v.neg ? -c : c;
}

// The binary operators on two BigInts (6.1.6.2); op is the bytecode (OP_Add ... OP_Shr)
Value BigBinary(Runtime& rt, int op, const Value& av, const Value& bv) {
    const Big& a = BigOf(av);
    const Big& b = BigOf(bv);
    switch (op) {
        case OP_Add: return Wrap(rt, AddBig(a, b));
        case OP_Sub: return Wrap(rt, AddBig(a, NegBig(b)));
        case OP_Mul: return Wrap(rt, Big{a.neg != b.neg, MulMag(rt, a.mag, b.mag)});
        case OP_Div: case OP_Mod: {
            if (b.mag.empty()) rt.ThrowRange("division by zero");
            Limbs q, r;
            DivMag(rt, a.mag, b.mag, q, r);
            return op == OP_Div ? Wrap(rt, Big{a.neg != b.neg, std::move(q)}) : Wrap(rt, Big{a.neg, std::move(r)});
        }
        case OP_Pow: return Wrap(rt, PowBig(rt, a, b));
        case OP_Shl: return Wrap(rt, ShiftBig(rt, a, b, true));
        case OP_Sar: return Wrap(rt, ShiftBig(rt, a, b, false));
        case OP_Shr: rt.ThrowType("BigInts have no unsigned right shift (>>>); use >>");
        case OP_BitAnd: case OP_BitOr: case OP_BitXor: {
            std::size_t len = std::max(a.mag.size(), b.mag.size()) + 1;
            rt.Charge(len / 16 + 1);
            Limbs x = ToTwos(a, len), y = ToTwos(b, len);
            for (std::size_t i = 0; i < len; ++i) x[i] = op == OP_BitAnd ? x[i] & y[i] : op == OP_BitOr ? x[i] | y[i] : x[i] ^ y[i];
            return Wrap(rt, FromTwos(std::move(x)));
        }
        default: rt.ThrowType("this operator does not take BigInts");
    }
}
// -x, ~x, x + 1, x - 1
Value BigUnary(Runtime& rt, int op, const Value& av) {
    const Big& a = BigOf(av);
    switch (op) {
        case OP_Neg: return Wrap(rt, NegBig(a));
        case OP_BitNot: return Wrap(rt, AddBig(NegBig(a), Big{true, {1}}));
        case OP_Inc: case OP_IncLoc: return Wrap(rt, AddBig(a, Big{false, {1}}));
        default: return Wrap(rt, AddBig(a, Big{true, {1}}));
    }
}

// 7.1.13 ToBigInt
Value ToBigIntValue(Runtime& rt, const Value& v) {
    Value p = rt.ToPrimitive(v, 1);
    switch (p.t) {
        case Tag::BigInt: return p;
        case Tag::Bool: return NewBigValue(rt, false, p.b ? Limbs{1} : Limbs{});
        case Tag::String: {
            Value out;
            if (!StringToBigInt(rt, p.S()->s, out)) rt.ThrowError(ErrorType::SyntaxError, "can not turn '" + rt.ToUtf8(p) + "' into a BigInt");
            return out;
        }
        case Tag::Number: rt.ThrowType("can not turn a Number into a BigInt implicitly; use BigInt(n)");
        case Tag::Symbol: rt.ThrowType("can not turn a Symbol into a BigInt");
        default: rt.ThrowType(std::string("can not turn ") + (p.IsNull() ? "null" : "undefined") + " into a BigInt");
    }
}

namespace {
Value ThisBig(Runtime& rt, const Value& v, const char* m) {
    if (v.t == Tag::BigInt) return v;
    if (v.IsObj() && v.O()->cls == Cls::BigIntBox) return static_cast<BoxObj*>(v.O())->prim;
    rt.ThrowType(std::string("BigInt.prototype.") + m + " needs a BigInt");
}
// BigInt.asIntN / asUintN (21.2.2.1, 21.2.2.2): the value modulo 2^bits, read as signed or unsigned
Value AsBits(Runtime& rt, CallArgs& a, bool isSigned) {
    double bd = rt.ToIntegerOrInfinity(a[0]);
    if (bd < 0 || bd > 9007199254740991.0) rt.ThrowRange("the bit count must be between 0 and 2^53 - 1");
    Value x = ToBigIntValue(rt, a[1]);
    rt.Keep(x);
    std::uint64_t bits = (std::uint64_t)bd;
    if (bits == 0) return NewBigValue(rt, false, {});
    const Big& v = BigOf(x);
    std::size_t len = BitLength(v.mag);
    if (isSigned ? len < bits : (!v.neg && len <= bits)) return x;   // already in range (len + 1 <= bits covers the sign)
    if (bits > kMaxBigBits) rt.ThrowRange("the BigInt is too big (over 1048576 bits)");
    std::size_t limbs = (std::size_t)((bits + 31) / 32);
    Limbs t = ToTwos(v, limbs);
    std::uint32_t rem = (std::uint32_t)(bits % 32);
    if (rem) t.back() &= (1u << rem) - 1;
    Big r{false, std::move(t)};
    r = Norm(std::move(r));
    if (isSigned && BitLength(r.mag) == bits) {   // the sign bit is set: subtract 2^bits
        Limbs pow2((std::size_t)(bits / 32) + 1, 0);
        pow2[(std::size_t)(bits / 32)] = 1u << (bits % 32);
        r = Norm({true, SubMag(pow2, r.mag)});
    }
    return Wrap(rt, std::move(r));
}
} // namespace

void InstallBigIntLib(Runtime& rt) {
    Object* bp = rt.NewObject();
    rt.bigintProto = bp;
    NativeFunc* ctor = DefCtor(rt, "BigInt", [](Runtime& rt, CallArgs& a) {
        if (a.newTarget) rt.ThrowType("BigInt is not a constructor (call BigInt(x) without new)");
        Value p = rt.ToPrimitive(a[0], 1);
        if (p.IsNum()) return BigFromDouble(rt, p.n);
        return ToBigIntValue(rt, p);
    }, 1, bp);
    ctor->ctor = false;
    DefNative(rt, ctor, "asIntN", [](Runtime& rt, CallArgs& a) { return AsBits(rt, a, true); }, 2);
    DefNative(rt, ctor, "asUintN", [](Runtime& rt, CallArgs& a) { return AsBits(rt, a, false); }, 2);
    DefNative(rt, bp, "toString", [](Runtime& rt, CallArgs& a) {
        Value x = ThisBig(rt, a.thisv, "toString");
        double rd = a[0].IsUndef() ? 10 : rt.ToIntegerOrInfinity(a[0]);   // checked before the cast (Infinity)
        if (rd < 2 || rd > 36) rt.ThrowRange("toString's radix must be between 2 and 36");
        int radix = (int)rd;
        return Value::Str(rt.NewString(BigToString(rt, x, radix)));
    }, 0);
    DefNative(rt, bp, "toLocaleString", [](Runtime& rt, CallArgs& a) {   // en-US: groups of three digits
        std::u16string s = BigToString(rt, ThisBig(rt, a.thisv, "toLocaleString"), 10);
        std::size_t first = s[0] == u'-' ? 1 : 0;
        for (std::size_t i = s.size(); i > first + 3; i -= 3) s.insert(i - 3, 1, u',');
        return Value::Str(rt.NewString(std::move(s)));
    }, 0);
    DefNative(rt, bp, "valueOf", [](Runtime& rt, CallArgs& a) { return ThisBig(rt, a.thisv, "valueOf"); }, 0);
    bp->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("BigInt")), kConfigurable);
}

// ============================================================ Intl (ECMA-402), partial: en-US only
// A small part of ECMA-402 so pages that format numbers and dates run: Intl.NumberFormat, DateTimeFormat,
// PluralRules, RelativeTimeFormat, ListFormat and Collator, plus Number / Date toLocale*String with options.
// Every locale argument is accepted and ignored: the results always follow en-US conventions, written by hand
// here (no CLDR data). Time zones: "UTC" (and "Etc/UTC", "GMT") or the platform's local zone; any other zone
// name is accepted and formats in the local zone. resolvedOptions().timeZone is the TZ variable when it holds
// an IANA name, else "UTC" or "Etc/GMT+N" from the local offset.
namespace {

enum IntlKind : std::uint8_t { IK_Number, IK_DateTime, IK_Plural, IK_Relative, IK_List, IK_Collator };

struct IntlObj : Object {
    IntlKind kind = IK_Number;
    std::unordered_map<std::string, std::string> opt;   ///< the resolved options, as text
    int minFrac = 0, maxFrac = 3, minInt = 1;
    Value boundFn;                                       ///< the bound format / compare function, made on first use
    IntlObj() : Object(Cls::Intl) {}
    void Trace(Runtime& rt) override {
        Object::Trace(rt);
        rt.MarkValue(boundFn);
    }
};

IntlObj* ThisIntl(Runtime& rt, const Value& v, IntlKind k, const char* what) {
    if (!v.IsObj() || v.O()->cls != Cls::Intl || static_cast<IntlObj*>(v.O())->kind != k)
        rt.ThrowType(std::string(what) + " needs an Intl." + what + " object as 'this'");
    return static_cast<IntlObj*>(v.O());
}

// 9.2.12 GetOption for a string option with its allowed values (nullptr-terminated; none = any); "" when absent
std::string GetOpt(Runtime& rt, const Value& options, const char* name, std::initializer_list<const char*> allowed, const char* dflt = "") {
    if (!options.IsObj()) return dflt;
    Value v = rt.Get(options, rt.Intern(name));
    if (v.IsUndef()) return dflt;
    std::string s = rt.ToUtf8(v);
    if (allowed.size() == 0) return s;
    for (const char* a : allowed)
        if (s == a) return s;
    rt.ThrowRange(std::string("the '") + name + "' option can not be '" + s + "'");
}
int GetNumOpt(Runtime& rt, const Value& options, const char* name, int lo, int hi, int dflt) {
    if (!options.IsObj()) return dflt;
    Value v = rt.Get(options, rt.Intern(name));
    if (v.IsUndef()) return dflt;
    double d = rt.ToNumber(v);
    if (std::isnan(d) || d < lo || d > hi) rt.ThrowRange(std::string("the '") + name + "' option is out of range");
    return (int)std::floor(d);
}
Value OptionsArg(Runtime& rt, const Value& v) {
    if (v.IsUndef()) return Value();
    return Value::Obj(rt.ToObject(v));
}

IntlObj* NewIntl(Runtime& rt, CallArgs& a, IntlKind k) {
    IntlObj* o = rt.Alloc<IntlObj>();
    o->kind = k;
    Object* nt = a.newTarget ? a.newTarget : a.callee;
    Value pr = rt.Get(Value::Obj(nt), rt.atoms->prototype);
    o->proto = pr.IsObj() ? pr.O() : a.callee->slots[0].O();
    o->opt["locale"] = "en-US";
    return o;
}

std::string Group3(const std::string& digits) {   // "1234567" -> "1,234,567"
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

// |x| with between minFrac and maxFrac decimals (half away from zero), grouped if asked
std::string PlainDigits(double x, int minFrac, int maxFrac, int minInt, bool group) {
    std::string t = ToFixedText(std::fabs(x), maxFrac);
    std::size_t dot = t.find('.');
    std::string ip = dot == std::string::npos ? t : t.substr(0, dot);
    std::string fp = dot == std::string::npos ? "" : t.substr(dot + 1);
    while ((int)fp.size() > minFrac && !fp.empty() && fp.back() == '0') fp.pop_back();
    if ((int)ip.size() < minInt) ip.insert(0, (std::size_t)(minInt - (int)ip.size()), '0');
    return (group ? Group3(ip) : ip) + (fp.empty() ? "" : "." + fp);
}

// en-US compact short: 1234 -> 1.2K, 12345 -> 12K, 1500000 -> 1.5M (two significant digits below 100)
std::string CompactDigits(double x, bool longForm) {
    static const char* const shortSuffix[] = {"", "K", "M", "B", "T"};
    static const char* const longSuffix[] = {"", " thousand", " million", " billion", " trillion"};
    double a = std::fabs(x);
    int unit = 0;
    while (unit < 4 && a >= 999.5) {
        a /= 1000;
        ++unit;
    }
    std::string body;
    if (a < 100 && a != 0) {
        std::string d;
        int e;
        ExactDigits(a, d, e);
        RoundDigits(d, e, 2);
        double r = std::stod(d.substr(0, 1) + "." + d.substr(1) + "e" + std::to_string(e));
        if (r >= 999.5 && unit < 4) { r /= 1000; ++unit; }
        body = PlainDigits(r, 0, r < 10 ? 1 : 0, 1, false);
    } else body = PlainDigits(a, 0, 0, 1, unit == 0);
    return body + (longForm ? longSuffix[unit] : shortSuffix[unit]);
}

std::string CurrencySymbol(const std::string& code, int& digits) {
    digits = 2;
    if (code == "USD") return "$";
    if (code == "EUR") return "\xE2\x82\xAC";
    if (code == "GBP") return "\xC2\xA3";
    if (code == "JPY") { digits = 0; return "\xC2\xA5"; }
    if (code == "KRW") { digits = 0; return "\xE2\x82\xA9"; }
    if (code == "INR") return "\xE2\x82\xB9";
    if (code == "CAD") return "CA$";
    if (code == "AUD") return "A$";
    if (code == "CNY") return "CN\xC2\xA5";
    return code + "\xC2\xA0";   // other codes: "CHF 12.00" with a no-break space
}

std::string FormatNumber(Runtime& rt, IntlObj* f, double x) {
    if (std::isnan(x)) return "NaN";
    const std::string& style = f->opt["style"];
    bool neg = std::signbit(x);
    std::string body;
    if (std::isinf(x)) body = "\xE2\x88\x9E";
    else if (f->opt["notation"] == "compact") body = CompactDigits(x, f->opt["compactDisplay"] == "long");
    else body = PlainDigits(style == "percent" ? x * 100 : x, f->minFrac, f->maxFrac, f->minInt, f->opt["useGrouping"] != "false");
    (void)rt;
    if (style == "percent") body += "%";
    if (style == "currency") {
        int digits;
        std::string sym = CurrencySymbol(f->opt["currency"], digits);
        if (f->opt["currencyDisplay"] == "code") sym = f->opt["currency"] + "\xC2\xA0";
        body = sym + body;
    }
    const std::string& sd = f->opt["signDisplay"];
    std::string sign;
    if (sd == "auto") sign = neg ? "-" : "";
    else if (sd == "always") sign = neg ? "-" : "+";
    else if (sd == "exceptZero") sign = x == 0 ? "" : neg ? "-" : "+";
    else if (sd == "negative") sign = neg && x != 0 ? "-" : "";
    return sign + body;
}

void SetupNumberFormat(Runtime& rt, IntlObj* f, const Value& optsIn) {
    Value o = OptionsArg(rt, optsIn);
    f->opt["numberingSystem"] = "latn";
    std::string style = GetOpt(rt, o, "style", {"decimal", "percent", "currency", "unit"}, "decimal");
    f->opt["style"] = style;
    std::string cur = GetOpt(rt, o, "currency", {});
    for (char& c : cur) c = (char)std::toupper((unsigned char)c);
    if (style == "currency" && cur.empty()) rt.ThrowType("Intl.NumberFormat: the currency style needs a 'currency' option");
    if (!cur.empty()) f->opt["currency"] = cur;
    f->opt["currencyDisplay"] = GetOpt(rt, o, "currencyDisplay", {"symbol", "narrowSymbol", "code", "name"}, "symbol");
    f->opt["notation"] = GetOpt(rt, o, "notation", {"standard", "scientific", "engineering", "compact"}, "standard");
    f->opt["compactDisplay"] = GetOpt(rt, o, "compactDisplay", {"short", "long"}, "short");
    f->opt["signDisplay"] = GetOpt(rt, o, "signDisplay", {"auto", "never", "always", "exceptZero", "negative"}, "auto");
    Value g = o.IsObj() ? rt.Get(o, rt.Intern("useGrouping")) : Value();
    f->opt["useGrouping"] = g.IsUndef() ? "auto" : rt.ToBoolean(g) ? "auto" : "false";
    if (g.IsStr() && rt.ToUtf8(g) == "false") f->opt["useGrouping"] = "false";
    int curDigits = 2;
    if (style == "currency") CurrencySymbol(cur, curDigits);
    int dMin = style == "currency" ? curDigits : 0;
    int dMax = style == "currency" ? curDigits : style == "percent" ? 0 : 3;
    f->minInt = GetNumOpt(rt, o, "minimumIntegerDigits", 1, 21, 1);
    int mn = GetNumOpt(rt, o, "minimumFractionDigits", 0, 100, -1);
    int mx = GetNumOpt(rt, o, "maximumFractionDigits", 0, 100, -1);
    if (mn < 0) mn = mx >= 0 ? std::min(dMin, mx) : dMin;
    if (mx < 0) mx = std::max(mn, dMax);
    if (mn > mx) rt.ThrowRange("minimumFractionDigits is more than maximumFractionDigits");
    f->minFrac = mn;
    f->maxFrac = mx;
    // significant digits: approximate with fraction digits after the first significant one (not exact for all inputs)
    int sigMax = GetNumOpt(rt, o, "maximumSignificantDigits", 1, 21, -1);
    if (sigMax > 0) {
        f->opt["maximumSignificantDigits"] = std::to_string(sigMax);
        f->minFrac = 0;
        f->maxFrac = -sigMax;   // marker: chosen per value in format
    }
}

std::string FormatNumberValue(Runtime& rt, IntlObj* f, double x) {
    if (f->maxFrac < 0 && std::isfinite(x) && x != 0) {   // maximumSignificantDigits
        int sig = -f->maxFrac;
        int mag = (int)std::floor(std::log10(std::fabs(x)));
        int frac = std::max(0, std::min(100, sig - 1 - mag));
        double scale = std::pow(10.0, (double)(mag + 1 - sig));
        double r = mag + 1 - sig > 0 ? std::round(x / scale) * scale : x;
        IntlObj tmp;
        tmp.opt = f->opt;
        tmp.minFrac = 0;
        tmp.maxFrac = frac;
        tmp.minInt = f->minInt;
        return FormatNumber(rt, &tmp, r);
    }
    if (f->maxFrac < 0) {
        IntlObj tmp;
        tmp.opt = f->opt;
        tmp.minFrac = 0;
        tmp.maxFrac = 0;
        return FormatNumber(rt, &tmp, x);
    }
    return FormatNumber(rt, f, x);
}

// ---- dates
const char* const kLongMonths[12] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                     "September", "October", "November", "December"};
const char* const kLongDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

bool IsUtcZone(const std::string& z) {
    std::string u;
    for (char c : z) u += (char)std::toupper((unsigned char)c);
    return u == "UTC" || u == "ETC/UTC" || u == "GMT" || u == "ETC/GMT" || u == "Z";
}
std::string LocalZoneName(double now) {
    const char* tz = std::getenv("TZ");
    if (tz && std::strchr(tz, '/') && tz[0] != ':' && std::strlen(tz) < 64) return tz;
    double off = LocalOffset(now) / 60000.0;
    if (off == 0) return "UTC";
    if (std::fmod(off, 60.0) != 0) return "UTC";
    int h = (int)(off / 60);
    return "Etc/GMT" + std::string(h > 0 ? "-" : "+") + std::to_string(h > 0 ? h : -h);   // Etc/GMT+5 is UTC-5
}

void SetupDateTimeFormat(Runtime& rt, IntlObj* f, const Value& optsIn, int defaults /* 0 date, 1 time, 2 both */) {
    Value o = OptionsArg(rt, optsIn);
    f->opt["calendar"] = "gregory";
    f->opt["numberingSystem"] = "latn";
    static const std::initializer_list<const char*> text = {"long", "short", "narrow"};
    static const std::initializer_list<const char*> num = {"numeric", "2-digit"};
    f->opt["weekday"] = GetOpt(rt, o, "weekday", text);
    f->opt["era"] = GetOpt(rt, o, "era", text);
    f->opt["year"] = GetOpt(rt, o, "year", num);
    f->opt["month"] = GetOpt(rt, o, "month", {"numeric", "2-digit", "long", "short", "narrow"});
    f->opt["day"] = GetOpt(rt, o, "day", num);
    f->opt["hour"] = GetOpt(rt, o, "hour", num);
    f->opt["minute"] = GetOpt(rt, o, "minute", num);
    f->opt["second"] = GetOpt(rt, o, "second", num);
    f->opt["timeZoneName"] = GetOpt(rt, o, "timeZoneName", {"short", "long", "shortOffset", "longOffset", "shortGeneric", "longGeneric"});
    f->opt["dateStyle"] = GetOpt(rt, o, "dateStyle", {"full", "long", "medium", "short"});
    f->opt["timeStyle"] = GetOpt(rt, o, "timeStyle", {"full", "long", "medium", "short"});
    std::string tz = GetOpt(rt, o, "timeZone", {});
    f->opt["utc"] = !tz.empty() && IsUtcZone(tz) ? "1" : "";
    f->opt["timeZone"] = tz.empty() ? LocalZoneName(NowMs()) : IsUtcZone(tz) ? "UTC" : tz;
    std::string hc = GetOpt(rt, o, "hourCycle", {"h11", "h12", "h23", "h24"});
    Value h12 = o.IsObj() ? rt.Get(o, rt.Intern("hour12")) : Value();
    bool twelve = h12.IsUndef() ? !(hc == "h23" || hc == "h24") : rt.ToBoolean(h12);
    f->opt["hour12"] = twelve ? "1" : "";
    f->opt["hourCycle"] = twelve ? "h12" : "h23";
    bool styles = !f->opt["dateStyle"].empty() || !f->opt["timeStyle"].empty();
    bool any = styles;
    for (const char* k : {"weekday", "year", "month", "day", "hour", "minute", "second"})
        if (!f->opt[k].empty()) any = true;
    if (styles) {
        for (const char* k : {"weekday", "year", "month", "day", "hour", "minute", "second", "timeZoneName"})
            if (!f->opt[k].empty()) rt.ThrowType(std::string("dateStyle and timeStyle can not be used with '") + k + "'");
    }
    if (!any) {   // 11.1.2 ToDateTimeOptions: the defaults
        if (defaults != 1) f->opt["year"] = f->opt["month"] = f->opt["day"] = "numeric";
        if (defaults != 0) f->opt["hour"] = f->opt["minute"] = f->opt["second"] = "numeric";
    }
}

using Parts = std::vector<std::pair<const char*, std::string>>;

std::string TwoDigits(double v) { return Pad(std::fmod(v, 100.0), 2); }

void DatePieces(IntlObj* f, const Fields& t, Parts& out) {
    std::string wd = f->opt["weekday"], y = f->opt["year"], m = f->opt["month"], d = f->opt["day"];
    auto lit = [&](const char* s) { out.push_back({"literal", s}); };
    std::string yearText = y == "2-digit" ? TwoDigits(t.year) : Pad(t.year, 1);
    std::string dayText = d == "2-digit" ? Pad(t.date, 2) : Pad(t.date, 1);
    if (!wd.empty()) {
        std::string w = kLongDays[(int)t.weekday];
        out.push_back({"weekday", wd == "long" ? w : wd == "short" ? w.substr(0, 3) : w.substr(0, 1)});
        if (!m.empty() || !d.empty() || !y.empty()) lit(", ");
    }
    if (m == "long" || m == "short" || m == "narrow") {   // "January 2, 2020", "Jan 2", "January 2020"
        std::string mt = kLongMonths[(int)t.month];
        out.push_back({"month", m == "long" ? mt : m == "short" ? mt.substr(0, 3) : mt.substr(0, 1)});
        if (!d.empty()) {
            lit(" ");
            out.push_back({"day", dayText});
        }
        if (!y.empty()) {
            lit(d.empty() ? " " : ", ");
            out.push_back({"year", yearText});
        }
        return;
    }
    bool first = true;   // numeric: M/D/Y with the fields asked for
    auto sep = [&]() { if (!first) lit("/"); first = false; };
    if (!m.empty()) { sep(); out.push_back({"month", m == "2-digit" ? Pad(t.month + 1, 2) : Pad(t.month + 1, 1)}); }
    if (!d.empty()) { sep(); out.push_back({"day", dayText}); }
    if (!y.empty()) { sep(); out.push_back({"year", yearText}); }
}

std::string ZoneText(IntlObj* f, double tv, bool longName) {
    if (f->opt["utc"] == "1") return longName ? "Coordinated Universal Time" : "UTC";
    double off = LocalOffset(tv) / 60000.0;
    if (off == 0) return longName ? "Coordinated Universal Time" : "UTC";
    int mins = (int)std::fabs(off);
    std::string s = std::string("GMT") + (off < 0 ? "-" : "+") + std::to_string(mins / 60);
    if (mins % 60) s += ":" + Pad(mins % 60, 2);
    return s;
}

void TimePieces(IntlObj* f, const Fields& t, double tv, Parts& out) {
    std::string h = f->opt["hour"], mi = f->opt["minute"], s = f->opt["second"];
    bool twelve = f->opt["hour12"] == "1";
    bool first = true;
    if (!h.empty()) {
        int hv = (int)t.hour;
        if (twelve) { hv %= 12; if (hv == 0) hv = 12; }
        out.push_back({"hour", (h == "2-digit" || (!twelve && (!mi.empty() || !s.empty()))) ? Pad(hv, 2) : std::to_string(hv)});
        first = false;
    }
    if (!mi.empty()) {
        if (!first) out.push_back({"literal", ":"});
        out.push_back({"minute", (!first || mi == "2-digit") ? Pad(t.minute, 2) : Pad(t.minute, 1)});
        first = false;
    }
    if (!s.empty()) {
        if (!first) out.push_back({"literal", ":"});
        out.push_back({"second", (!first || s == "2-digit") ? Pad(t.second, 2) : Pad(t.second, 1)});
        first = false;
    }
    if (!h.empty() && twelve) {
        out.push_back({"literal", " "});
        out.push_back({"dayPeriod", t.hour < 12 ? "AM" : "PM"});
    }
    std::string zn = f->opt["timeZoneName"];
    if (!zn.empty()) {
        if (!out.empty()) out.push_back({"literal", " "});
        out.push_back({"timeZoneName", ZoneText(f, tv, zn == "long")});
    }
}

Parts FormatDateParts(Runtime& rt, IntlObj* f, double tv) {
    if (!std::isfinite(tv)) rt.ThrowRange("Intl.DateTimeFormat: invalid date");
    double local = f->opt["utc"] == "1" ? tv : LocalTime(tv);
    Fields t = Split(local);
    Parts date, time;
    std::string ds = f->opt["dateStyle"], ts = f->opt["timeStyle"];
    if (!ds.empty() || !ts.empty()) {   // the four en-US styles
        IntlObj tmp;
        tmp.opt = f->opt;
        if (!ds.empty()) {
            tmp.opt["weekday"] = ds == "full" ? "long" : "";
            tmp.opt["month"] = ds == "full" || ds == "long" ? "long" : ds == "medium" ? "short" : "numeric";
            tmp.opt["day"] = "numeric";
            tmp.opt["year"] = ds == "short" ? "2-digit" : "numeric";
            DatePieces(&tmp, t, date);
        }
        if (!ts.empty()) {
            tmp.opt["hour"] = tmp.opt["minute"] = "numeric";
            tmp.opt["second"] = ts == "short" ? "" : "numeric";
            tmp.opt["timeZoneName"] = ts == "full" ? "long" : ts == "long" ? "short" : "";
            TimePieces(&tmp, t, tv, time);
        }
        Parts out = date;
        if (!date.empty() && !time.empty()) out.push_back({"literal", ds == "full" || ds == "long" ? " at " : ", "});
        out.insert(out.end(), time.begin(), time.end());
        return out;
    }
    DatePieces(f, t, date);
    TimePieces(f, t, tv, time);
    Parts out = date;
    if (!date.empty() && !time.empty()) out.push_back({"literal", ", "});
    out.insert(out.end(), time.begin(), time.end());
    return out;
}
std::string JoinParts(const Parts& p) {
    std::string s;
    for (const auto& x : p) s += x.second;
    return s;
}
double DateArg(Runtime& rt, const Value& v) {
    if (v.IsUndef()) return NowMs();
    if (v.IsObj() && v.O()->cls == Cls::DateBox) return static_cast<BoxObj*>(v.O())->prim.n;
    return TimeClip(rt.ToNumber(v));
}
Value PartsArray(Runtime& rt, const Parts& parts) {
    ArrayObj* arr = rt.NewArray(parts.size());
    for (const auto& p : parts) {
        Object* o = rt.NewObject();
        rt.DefineData(o, rt.Intern("type"), Value::Str(rt.Intern(p.first)));
        rt.DefineData(o, rt.atoms->value, Value::Str(rt.NewStringUtf8(p.second)));
        rt.DefineIndex(arr, arr->length, Value::Obj(o));
    }
    return Value::Obj(arr);
}

// ---- plural rules, relative time, lists
std::string PluralOf(IntlObj* f, double n) {
    if (!std::isfinite(n)) return "other";
    if (f->opt["type"] == "ordinal") {
        double a = std::fabs(n);
        if (a != std::floor(a)) return "other";
        long long i = (long long)std::fmod(a, 1000.0);
        if (i % 10 == 1 && i % 100 != 11) return "one";
        if (i % 10 == 2 && i % 100 != 12) return "two";
        if (i % 10 == 3 && i % 100 != 13) return "few";
        return "other";
    }
    return std::fabs(n) == 1 && f->maxFrac >= 0 && PlainDigits(n, f->minFrac, f->maxFrac, 1, false) == "1" ? "one" : "other";
}

std::string RelativeText(Runtime& rt, IntlObj* f, double v, std::string unit) {
    if (!std::isfinite(v)) rt.ThrowRange("Intl.RelativeTimeFormat needs a finite number");
    if (unit.size() > 1 && unit.back() == 's') unit.pop_back();
    static const char* const units[] = {"second", "minute", "hour", "day", "week", "month", "quarter", "year"};
    bool ok = false;
    for (const char* u : units) ok |= unit == u;
    if (!ok) rt.ThrowRange("Intl.RelativeTimeFormat: unknown unit '" + unit + "'");
    if (f->opt["numeric"] == "auto" && (v == 0 || v == 1 || v == -1)) {
        if (unit == "day") return v == 0 ? "today" : v > 0 ? "tomorrow" : "yesterday";
        if (unit == "second" && v == 0) return "now";
        if (unit == "second" || unit == "minute" || unit == "hour") {
            if (v == 0) return "this " + unit;
        } else return (v == 0 ? "this " : v > 0 ? "next " : "last ") + unit;
    }
    bool shortForm = f->opt["style"] != "long";
    std::string name = unit;
    if (shortForm) {
        static const std::pair<const char*, const char*> abbr[] = {{"second", "sec."}, {"minute", "min."}, {"hour", "hr."}, {"day", "day"},
                                                                  {"week", "wk."}, {"month", "mo."}, {"quarter", "qtr."}, {"year", "yr."}};
        for (const auto& p : abbr)
            if (unit == p.first) name = p.second;
    }
    double a = std::fabs(v);
    std::string num = PlainDigits(a, 0, 3, 1, true);
    bool plural = num != "1";
    if (plural && (!shortForm || name == "day")) name += "s";
    else if (plural && shortForm && name == "qtr.") name = "qtrs.";
    bool past = std::signbit(v);
    return past ? num + " " + name + " ago" : "in " + num + " " + name;
}

// 7.3.18 (CreateListFromArrayLike of strings) then en-US joining
std::string ListText(Runtime& rt, IntlObj* f, const Value& list) {
    std::vector<std::string> items;
    if (!list.IsUndef()) {
        IterRec* r = GetIterator(rt, list);
        Value e;
        while (IterStep(rt, r, e)) {
            if (!e.IsStr()) rt.ThrowType("Intl.ListFormat formats strings only");
            items.push_back(rt.ToUtf8(e));
            if (items.size() > (1u << 20)) rt.ThrowRange("the list is too long");
        }
    }
    std::string type = f->opt["type"], style = f->opt["style"];
    std::string word = type == "disjunction" ? "or" : style == "short" && type == "conjunction" ? "&" : "and";
    bool unit = type == "unit";
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        rt.Charge(1);
        if (i) {
            if (unit) out += style == "narrow" ? " " : ", ";
            else if (items.size() == 2) out += " " + word + " ";
            else if (i + 1 == items.size()) out += (style == "narrow" && type == "conjunction") ? ", " : ", " + word + " ";
            else out += ", ";
        }
        out += items[i];
    }
    return out;
}

// numeric-aware, case-folding comparison for Intl.Collator (code unit order otherwise; no locale tables)
int CollateCompare(IntlObj* f, const std::u16string& a, const std::u16string& b) {
    bool numeric = f->opt["numeric"] == "true";
    bool fold = f->opt["sensitivity"] == "base" || f->opt["sensitivity"] == "accent";
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        char16_t x = a[i], y = b[j];
        if (numeric && x >= u'0' && x <= u'9' && y >= u'0' && y <= u'9') {
            std::size_t i2 = i, j2 = j;
            while (i2 < a.size() && a[i2] == u'0') ++i2;
            while (j2 < b.size() && b[j2] == u'0') ++j2;
            std::size_t ie = i2, je = j2;
            while (ie < a.size() && a[ie] >= u'0' && a[ie] <= u'9') ++ie;
            while (je < b.size() && b[je] >= u'0' && b[je] <= u'9') ++je;
            if (ie - i2 != je - j2) return ie - i2 < je - j2 ? -1 : 1;
            int c = a.compare(i2, ie - i2, b, j2, je - j2);
            if (c) return c < 0 ? -1 : 1;
            i = ie;
            j = je;
            continue;
        }
        char16_t lx = JsLower(x), ly = JsLower(y);
        if (lx != ly) return lx < ly ? -1 : 1;
        if (!fold && x != y) return x > y ? -1 : 1;   // lower case first, as en-US orders "a" before "A"
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

Value ResolvedOptions(Runtime& rt, IntlObj* f, std::initializer_list<const char*> keys) {
    Object* o = rt.NewObject();
    for (const char* k : keys) {
        auto it = f->opt.find(k);
        if (it == f->opt.end() || it->second.empty()) continue;
        std::string v = it->second;
        if (std::string(k) == "hour12") { rt.DefineData(o, rt.Intern(k), Value::Bool(v == "1")); continue; }
        if (std::string(k) == "numeric" && f->kind == IK_Collator) { rt.DefineData(o, rt.Intern(k), Value::Bool(v == "true")); continue; }
        rt.DefineData(o, rt.Intern(k), Value::Str(rt.NewStringUtf8(v)));
    }
    if (f->kind == IK_Number || f->kind == IK_Plural) {
        rt.DefineData(o, rt.Intern("minimumIntegerDigits"), Value::Num(f->minInt));
        if (f->maxFrac >= 0) {
            rt.DefineData(o, rt.Intern("minimumFractionDigits"), Value::Num(f->minFrac));
            rt.DefineData(o, rt.Intern("maximumFractionDigits"), Value::Num(f->maxFrac));
        }
    }
    return Value::Obj(o);
}

// The bound format / compare function a getter hands out (10.3.3, 11.3.3, 10.? Collator compare)
Value BoundFn(Runtime& rt, IntlObj* f, NativeFn fn, const char* name, int len) {
    if (f->boundFn.IsUndef()) f->boundFn = Value::Obj(rt.NewNative(fn, name, len, {Value::Obj(f)}));
    return f->boundFn;
}

Object* IntlCtor(Runtime& rt, Object* intl, const char* name, NativeFn ctor, const char* tag) {
    Object* proto = rt.NewObject();
    NativeFunc* c = rt.NewNative(ctor, name, 0, {Value::Obj(proto)});
    c->ctor = true;
    rt.MakeLazyProps(c);
    c->AddRaw(rt.atoms->prototype, Value::Obj(proto), 0);
    proto->AddRaw(rt.atoms->constructor, Value::Obj(c), kHidden);
    proto->AddRaw(rt.symToStringTag, Value::Str(rt.Intern(tag)), kConfigurable);
    intl->AddRaw(rt.Intern(name), Value::Obj(c), kHidden);
    DefNative(rt, c, "supportedLocalesOf", [](Runtime& rt, CallArgs& a) {   // the requested English locales (all we format)
        ArrayObj* arr = rt.NewArray();
        auto consider = [&](const Value& v) {
            std::string s = rt.ToUtf8(v);
            if (s.size() >= 2 && (s[0] | 0x20) == 'e' && (s[1] | 0x20) == 'n' && (s.size() == 2 || s[2] == '-'))
                rt.DefineIndex(arr, arr->length, Value::Str(rt.NewStringUtf8(s)));
        };
        if (a[0].IsStr()) consider(a[0]);
        else if (a[0].IsObj()) {
            std::uint64_t n = std::min<std::uint64_t>(rt.LengthOf(a[0]), 1000);
            for (std::uint64_t k = 0; k < n; ++k) {
                Runtime::ScratchScope ss(rt);
                consider(rt.GetIndex(a[0], (std::uint32_t)k));
            }
        }
        return Value::Obj(arr);
    }, 1);
    return proto;
}

} // namespace

// Number.prototype.toLocaleString and Date.prototype.toLocale*String with options (ECMA-402 15.4.1, 20.4.1-3)
std::string NumberToLocale(Runtime& rt, double x, const Value& options) {
    IntlObj f;
    SetupNumberFormat(rt, &f, options);
    return FormatNumberValue(rt, &f, x);
}
std::string DateToLocale(Runtime& rt, double tv, const Value& options, int defaults) {
    if (std::isnan(tv)) return "Invalid Date";
    IntlObj f;
    SetupDateTimeFormat(rt, &f, options, defaults);
    return JoinParts(FormatDateParts(rt, &f, tv));
}

void InstallIntlLib(Runtime& rt) {
    Object* intl = rt.NewObject();
    rt.global->AddRaw(rt.Intern("Intl"), Value::Obj(intl), kHidden);
    intl->AddRaw(rt.symToStringTag, Value::Str(rt.Intern("Intl")), kConfigurable);
    DefNative(rt, intl, "getCanonicalLocales", [](Runtime& rt, CallArgs& a) {
        ArrayObj* arr = rt.NewArray(1);
        if (!a[0].IsUndef()) rt.DefineIndex(arr, 0, Value::Str(rt.Intern("en-US")));
        return Value::Obj(arr);
    }, 1);

    Object* nf = IntlCtor(rt, intl, "NumberFormat", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = NewIntl(rt, a, IK_Number);
        SetupNumberFormat(rt, f, a[1]);
        return Value::Obj(f);
    }, "Intl.NumberFormat");
    DefGetter(rt, nf, "format", [](Runtime& rt, CallArgs& a) {
        return BoundFn(rt, ThisIntl(rt, a.thisv, IK_Number, "NumberFormat"), [](Runtime& rt, CallArgs& a) {
            IntlObj* f = static_cast<IntlObj*>(a.callee->slots[0].O());
            Value n = rt.ToNumeric(a[0]);
            if (n.IsBig()) return Value::Str(rt.NewStringUtf8(FormatNumberValue(rt, f, BigToDouble(n))));
            return Value::Str(rt.NewStringUtf8(FormatNumberValue(rt, f, n.n)));
        }, "", 1);
    });
    DefNative(rt, nf, "resolvedOptions", [](Runtime& rt, CallArgs& a) {
        return ResolvedOptions(rt, ThisIntl(rt, a.thisv, IK_Number, "NumberFormat"),
                               {"locale", "numberingSystem", "style", "currency", "currencyDisplay", "notation", "compactDisplay", "signDisplay", "useGrouping"});
    }, 0);

    Object* df = IntlCtor(rt, intl, "DateTimeFormat", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = NewIntl(rt, a, IK_DateTime);
        SetupDateTimeFormat(rt, f, a[1], 0);
        return Value::Obj(f);
    }, "Intl.DateTimeFormat");
    DefGetter(rt, df, "format", [](Runtime& rt, CallArgs& a) {
        return BoundFn(rt, ThisIntl(rt, a.thisv, IK_DateTime, "DateTimeFormat"), [](Runtime& rt, CallArgs& a) {
            IntlObj* f = static_cast<IntlObj*>(a.callee->slots[0].O());
            return Value::Str(rt.NewStringUtf8(JoinParts(FormatDateParts(rt, f, DateArg(rt, a[0])))));
        }, "", 1);
    });
    DefNative(rt, df, "formatToParts", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = ThisIntl(rt, a.thisv, IK_DateTime, "DateTimeFormat");
        return PartsArray(rt, FormatDateParts(rt, f, DateArg(rt, a[0])));
    }, 1);
    DefNative(rt, df, "resolvedOptions", [](Runtime& rt, CallArgs& a) {
        return ResolvedOptions(rt, ThisIntl(rt, a.thisv, IK_DateTime, "DateTimeFormat"),
                               {"locale", "calendar", "numberingSystem", "timeZone", "hourCycle", "hour12", "weekday", "era", "year",
                                "month", "day", "hour", "minute", "second", "timeZoneName", "dateStyle", "timeStyle"});
    }, 0);

    Object* pr = IntlCtor(rt, intl, "PluralRules", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("Intl.PluralRules must be called with 'new'");
        IntlObj* f = NewIntl(rt, a, IK_Plural);
        Value o = OptionsArg(rt, a[1]);
        f->opt["type"] = GetOpt(rt, o, "type", {"cardinal", "ordinal"}, "cardinal");
        f->minFrac = GetNumOpt(rt, o, "minimumFractionDigits", 0, 100, 0);
        f->maxFrac = std::max(f->minFrac, GetNumOpt(rt, o, "maximumFractionDigits", 0, 100, 3));
        return Value::Obj(f);
    }, "Intl.PluralRules");
    DefNative(rt, pr, "select", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = ThisIntl(rt, a.thisv, IK_Plural, "PluralRules");
        return Value::Str(rt.Intern(PluralOf(f, rt.ToNumber(a[0])).c_str()));
    }, 1);
    DefNative(rt, pr, "resolvedOptions", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = ThisIntl(rt, a.thisv, IK_Plural, "PluralRules");
        Value r = ResolvedOptions(rt, f, {"locale", "type"});
        rt.Keep(r);
        ArrayObj* cats = rt.NewArray();
        for (const char* c : f->opt["type"] == "ordinal" ? std::initializer_list<const char*>{"few", "one", "two", "other"}
                                                         : std::initializer_list<const char*>{"one", "other"})
            rt.DefineIndex(cats, cats->length, Value::Str(rt.Intern(c)));
        rt.DefineData(r.O(), rt.Intern("pluralCategories"), Value::Obj(cats));
        return r;
    }, 0);

    Object* rp = IntlCtor(rt, intl, "RelativeTimeFormat", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("Intl.RelativeTimeFormat must be called with 'new'");
        IntlObj* f = NewIntl(rt, a, IK_Relative);
        Value o = OptionsArg(rt, a[1]);
        f->opt["style"] = GetOpt(rt, o, "style", {"long", "short", "narrow"}, "long");
        f->opt["numeric"] = GetOpt(rt, o, "numeric", {"always", "auto"}, "always");
        f->opt["numberingSystem"] = "latn";
        return Value::Obj(f);
    }, "Intl.RelativeTimeFormat");
    DefNative(rt, rp, "format", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = ThisIntl(rt, a.thisv, IK_Relative, "RelativeTimeFormat");
        double v = rt.ToNumber(a[0]);
        return Value::Str(rt.NewStringUtf8(RelativeText(rt, f, v, rt.ToUtf8(a[1]))));
    }, 2);
    DefNative(rt, rp, "resolvedOptions", [](Runtime& rt, CallArgs& a) {
        return ResolvedOptions(rt, ThisIntl(rt, a.thisv, IK_Relative, "RelativeTimeFormat"), {"locale", "style", "numeric", "numberingSystem"});
    }, 0);

    Object* lp = IntlCtor(rt, intl, "ListFormat", [](Runtime& rt, CallArgs& a) {
        if (!a.newTarget) rt.ThrowType("Intl.ListFormat must be called with 'new'");
        IntlObj* f = NewIntl(rt, a, IK_List);
        Value o = OptionsArg(rt, a[1]);
        f->opt["type"] = GetOpt(rt, o, "type", {"conjunction", "disjunction", "unit"}, "conjunction");
        f->opt["style"] = GetOpt(rt, o, "style", {"long", "short", "narrow"}, "long");
        return Value::Obj(f);
    }, "Intl.ListFormat");
    DefNative(rt, lp, "format", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = ThisIntl(rt, a.thisv, IK_List, "ListFormat");
        return Value::Str(rt.NewStringUtf8(ListText(rt, f, a[0])));
    }, 1);
    DefNative(rt, lp, "resolvedOptions", [](Runtime& rt, CallArgs& a) {
        return ResolvedOptions(rt, ThisIntl(rt, a.thisv, IK_List, "ListFormat"), {"locale", "type", "style"});
    }, 0);

    Object* cp = IntlCtor(rt, intl, "Collator", [](Runtime& rt, CallArgs& a) {
        IntlObj* f = NewIntl(rt, a, IK_Collator);
        Value o = OptionsArg(rt, a[1]);
        f->opt["usage"] = GetOpt(rt, o, "usage", {"sort", "search"}, "sort");
        f->opt["sensitivity"] = GetOpt(rt, o, "sensitivity", {"base", "accent", "case", "variant"}, "variant");
        Value num = o.IsObj() ? rt.Get(o, rt.Intern("numeric")) : Value();
        f->opt["numeric"] = rt.ToBoolean(num) ? "true" : "false";
        f->opt["caseFirst"] = "false";
        f->opt["collation"] = "default";
        return Value::Obj(f);
    }, "Intl.Collator");
    DefGetter(rt, cp, "compare", [](Runtime& rt, CallArgs& a) {
        return BoundFn(rt, ThisIntl(rt, a.thisv, IK_Collator, "Collator"), [](Runtime& rt, CallArgs& a) {
            IntlObj* f = static_cast<IntlObj*>(a.callee->slots[0].O());
            std::u16string x = rt.ToU16(a[0]), y = rt.ToU16(a[1]);
            rt.Charge(std::min(x.size(), y.size()) / 8 + 1);
            return Value::Num(CollateCompare(f, x, y));
        }, "", 2);
    });
    DefNative(rt, cp, "resolvedOptions", [](Runtime& rt, CallArgs& a) {
        return ResolvedOptions(rt, ThisIntl(rt, a.thisv, IK_Collator, "Collator"),
                               {"locale", "usage", "sensitivity", "collation", "numeric", "caseFirst"});
    }, 0);

    // the locale-sensitive methods of 402 ch. 15-20 take options now
    DefNative(rt, rt.numberProto, "toLocaleString", [](Runtime& rt, CallArgs& a) {
        double x = ThisNum(rt, a.thisv, "toLocaleString");
        return Value::Str(rt.NewStringUtf8(NumberToLocale(rt, x, a[1])));
    }, 0);
    DefNative(rt, rt.dateProto, "toLocaleString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringUtf8(DateToLocale(rt, ThisTime(rt, a.thisv, "toLocaleString"), a[1], 2)));
    }, 0);
    DefNative(rt, rt.dateProto, "toLocaleDateString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringUtf8(DateToLocale(rt, ThisTime(rt, a.thisv, "toLocaleDateString"), a[1], 0)));
    }, 0);
    DefNative(rt, rt.dateProto, "toLocaleTimeString", [](Runtime& rt, CallArgs& a) {
        return Value::Str(rt.NewStringUtf8(DateToLocale(rt, ThisTime(rt, a.thisv, "toLocaleTimeString"), a[1], 1)));
    }, 0);
}

} // namespace okay::js
