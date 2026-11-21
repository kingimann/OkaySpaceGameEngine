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

// ASCII arrays kept as int32 or float32 when every value fits exactly: each element must read back (Array::operator[])
// as the double strtod gives for its token, bit for bit, whatever storage the reader chose. Arrays of integers,
// exact floats, plain doubles, -0, huge and tiny values, and mixes of them. Returns the number of mismatches; `checks`
// counts the values compared, `packed` the arrays stored as int32 or float32.
long FbxExactArrayChecks(long& checks, long& packed) {
    using namespace okay::fbx;
    std::mt19937_64 r(7);
    long bad = 0;
    for (int it = 0; it < 3000; ++it) {
        const int kind = (int)(r() % 6), n = 1 + (int)(r() % 60);
        std::vector<std::string> toks;
        for (int k = 0; k < n; ++k) {
            char b[64];
            const int vk = kind == 5 ? (int)(r() % 5) : kind;   // 5: a mix
            if (vk == 0) std::snprintf(b, sizeof b, "%lld", (long long)((std::int64_t)(r() % 4294967296ull) - 2147483648ll));   // int32
            else if (vk == 1) std::snprintf(b, sizeof b, "%.9g", (double)(float)std::ldexp((double)(r() >> 40) - 8388608.0, (int)(r() % 60) - 30));  // exact floats
            else if (vk == 2) std::snprintf(b, sizeof b, "%.17g", std::ldexp((double)(r() >> 11), (int)(r() % 200) - 100) * ((r() & 1) ? 1 : -1));
            else if (vk == 3) { static const char* kSpecial[] = {"-0", "0", "-0.0", "2147483648", "-2147483649", "3.4028235e38", "1e-46", "1e300", "16777217", "0.1"}; std::snprintf(b, sizeof b, "%s", kSpecial[r() % 10]); }
            else std::snprintf(b, sizeof b, "%lld", (long long)(r() % 2000) - 1000);
            toks.push_back(b);
        }
        std::string text = "A: *" + std::to_string(n) + " {\n a: ";
        for (int k = 0; k < n; ++k) { if (k) text += (r() % 8 == 0) ? ",\n" : ","; text += toks[(std::size_t)k]; }
        text += "\n}\n";
        Document doc; std::string err;
        if (!ParseAscii((const std::uint8_t*)text.data(), text.size(), doc, &err) || !doc.root.children.first ||
            doc.root.children.first->props.size() != 1 || doc.root.children.first->props[0].type != 'a') { if (bad++ < 5) std::printf("  array not parsed: %s\n", err.c_str()); continue; }
        const Array& a = doc.root.children.first->props[0].arr;
        if (a.type == 'i' || a.type == 'f') ++packed;
        if (a.size() != (std::size_t)n) { if (bad++ < 5) std::printf("  array size %zu, want %d\n", a.size(), n); continue; }
        for (int k = 0; k < n; ++k) {
            ++checks;
            char* end = nullptr;
            const double want = std::strtod(toks[(std::size_t)k].c_str(), &end), got = a[(std::size_t)k];
            if (std::memcmp(&want, &got, 8) != 0 && bad++ < 5)
                std::printf("  array value %s read back as %.17g (stored as '%c')\n", toks[(std::size_t)k].c_str(), got, a.type);
        }
    }
    return bad;
}
