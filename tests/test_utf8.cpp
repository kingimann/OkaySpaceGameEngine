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

    CHECK(utf8::Length("h\xC3\xA9llo") == 5);
    CHECK(utf8::Offset("h\xC3\xA9llo", 2) == 3);

    TEST_MAIN_RESULT();
}
