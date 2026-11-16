// Part of test_fbx_exact (see test_fbx_exact.cpp): the ASCII number fast path of okay::fbx against the plain
// conversion it replaced. This file compiles FbxReader.cpp itself, so its internal functions can be called.
#include "../engine/src/Render/FbxReader.cpp"

#include <cstdio>
#include <random>
#include <string>

// FastDecimalScan (one pass, eight digits at a time) must accept exactly the tokens FastDecimal accepts and give the
// same double, bit for bit, and that double must be strtod's (the plain conversion both replaced; any number of digits:
// tests/test_decimal_to_double.cpp checks the conversion itself much harder). Returns the number of mismatches;
// `checks` counts the tokens tried.
long FbxExactNumberChecks(long& checks) {
    using namespace okay::fbx;
    std::mt19937_64 r(42);
    long bad = 0;
    const char* alpha = "0123456789000000000.eE+-x,";
    for (long it = 0; it < 400000; ++it) {
        std::string t;
        const int mode = (int)(r() % 4);
        if (mode == 0) { const int len = 1 + (int)(r() % 30); for (int i = 0; i < len; ++i) t += alpha[r() % 26]; }
        else {
            char b[64];
            const double v = std::ldexp((double)(r() >> 11), (int)(r() % 80) - 60) * ((r() & 1) ? 1 : -1);
            const int prec = 1 + (int)(r() % 25);
            std::snprintf(b, sizeof b, mode == 1 ? "%.*g" : mode == 2 ? "%.*f" : "%.*e", prec, v);
            t = b;
            if (t.size() > 40) t.resize(40);
        }
        const std::string buf = t + ((r() & 1) ? "," : "");
        std::size_t tl = 0;
        while (tl < buf.size() && kIdent.t[(unsigned char)buf[tl]]) ++tl;
        double a = 0, b2 = 0;
        const bool old = FastDecimal(buf.data(), tl, a);
        const char* e = FastDecimalScan(buf.data(), buf.data() + buf.size(), b2);
        ++checks;
        bool same = e ? (old && (std::size_t)(e - buf.data()) == tl && std::memcmp(&a, &b2, 8) == 0) : !old;
        if (old) {   // what the general path would have given
            const std::string tok(buf.data(), tl);
            char* end = nullptr;
            const double ref = okay::detail::StrToD(tok.c_str(), &end);
            same = same && end && *end == '\0' && std::memcmp(&a, &ref, 8) == 0;
        }
        if (!same && bad++ < 5) std::printf("  number fast path differs on \"%s\"\n", t.c_str());
    }
    return bad;
}
