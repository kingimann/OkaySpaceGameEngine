#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::uint32_t> Decode(const std::string& s) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) out.push_back(utf8::Next(s, i));
    return out;
}

int main() {
    RUN_SUITE("utf8");

    struct Valid { std::string bytes; std::uint32_t cp; };
    const Valid valid[] = {
        {"A", 0x41}, {"\x7F", 0x7F}, {"\xC2\x80", 0x80}, {"\xDF\xBF", 0x7FF},
        {"\xE0\xA0\x80", 0x800}, {"\xEF\xBF\xBF", 0xFFFF}, {"\xED\x9F\xBF", 0xD7FF},
        {"\xEE\x80\x80", 0xE000}, {"\xF0\x90\x80\x80", 0x10000}, {"\xF4\x8F\xBF\xBF", 0x10FFFF},
        {"\xC3\xA9", 0xE9}, {"\xE2\x82\xAC", 0x20AC},
    };
    for (const auto& v : valid) {
        std::size_t i = 0;
        CHECK(utf8::Next(v.bytes, i) == v.cp);
        CHECK(i == v.bytes.size());
        std::string re; utf8::Append(re, v.cp);
        CHECK(re == v.bytes);                       // round trip
    }

    const char* invalid[] = {
        "\xC0\x80", "\xC1\xBF", "\xE0\x80\x80", "\xE0\x9F\xBF", "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF",   // overlong
        "\xED\xA0\x80", "\xED\xBF\xBF",                                                                   // surrogates
        "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xF7\xBF\xBF\xBF", "\xF8\x88\x80\x80\x80", "\xFF", "\xFE",// too big / never valid
        "\x80", "\xBF",                                                                                   // lone continuation
        "\xC3", "\xE2\x82", "\xF0\x9F\x98",                                                               // truncated at end
        "\xC3(", "\xE2\x82(", "\xF0\x9F(\x80",                                                            // broken by a non-continuation
    };
    for (const char* s : invalid) {
        std::string str(s);
        auto cps = Decode(str);
        CHECK(!cps.empty() && cps[0] == utf8::kReplacement);
        for (auto c : cps) CHECK(c <= 0x10FFFF && !(c >= 0xD800 && c <= 0xDFFF));
    }

    // Maximal subpart: a broken sequence eats its good prefix, the next byte starts afresh.
    {
        auto a = Decode(std::string("\xE2\x82" "A"));
        CHECK(a.size() == 2 && a[0] == utf8::kReplacement && a[1] == 'A');
        auto b = Decode(std::string("\xED\xA0\x80"));      // surrogate: lead alone, then two stray continuations
        CHECK(b.size() == 3);
        auto c = Decode(std::string("\xC0\x80"));
        CHECK(c.size() == 2);
        auto d = Decode(std::string("\xF0\x9F\x98\x80"));  // valid emoji untouched
        CHECK(d.size() == 1 && d[0] == 0x1F600);
    }

    // Every possible byte string of length 1..2 terminates and never yields an invalid scalar.
    {
        bool ok = true;
        for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; ++b) {
            std::string s; s += (char)a; s += (char)b;
            for (auto c : Decode(s)) if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) ok = false;
        }
        CHECK(ok);
    }

    // A surrogate is not a character: Append writes U+FFFD, not the three bytes ED A0 80 that
    // no decoder reads back as one code point.
    {
        for (std::uint32_t cp : {0xD800u, 0xDBFFu, 0xDC00u, 0xDFFFu, 0x110000u, 0xFFFFFFFFu}) {
            std::string out; utf8::Append(out, cp);
            CHECK(out == "\xEF\xBF\xBD");
            CHECK(utf8::Length(out) == 1);
        }
        std::string edge; utf8::Append(edge, 0xD7FFu); utf8::Append(edge, 0xE000u);   // neighbours stay valid
        auto cps = Decode(edge);
        CHECK(cps.size() == 2 && cps[0] == 0xD7FF && cps[1] == 0xE000);
    }

    // Next at or past the end reads nothing and stays inside the string.
    {
        std::string s = "ab";
        std::size_t i = 2;
        CHECK(utf8::Next(s, i) == 0 && i == 2);              // used to walk to 3
        i = 99;
        CHECK(utf8::Next(s, i) == 0 && i == 2);              // used to read out of bounds
        const char* p = s.data() + s.size();
        CHECK(utf8::Next(p, s.data() + s.size()) == 0 && p == s.data() + s.size());
        std::string empty; i = 0;
        CHECK(utf8::Next(empty, i) == 0 && i == 0);
        // a loop guarded by i < size still decodes everything
        std::string t = "a\xC3\xA9\xE2\x82\xAC";
        std::size_t n = 0; for (std::size_t k = 0; k < t.size();) { utf8::Next(t, k); ++n; }
        CHECK(n == 3);
    }

    CHECK(utf8::Length("h\xC3\xA9llo") == 5);
    CHECK(utf8::Offset("h\xC3\xA9llo", 2) == 3);

    TEST_MAIN_RESULT();
}
