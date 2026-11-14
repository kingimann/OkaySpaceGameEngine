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

    // ---- third pass: IsValid / Sanitize ------------------------------------------------------------------------------
    {
        CHECK(utf8::IsValid(std::string()) && utf8::IsValid("plain ascii") && utf8::IsValid("h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80"));
        CHECK(utf8::IsValid("\xEF\xBF\xBD"));                         // a real U+FFFD is valid text...
        CHECK(!utf8::IsValid("\xFF") && !utf8::IsValid("\x80") && !utf8::IsValid("\xC3") && !utf8::IsValid("\xE2\x82") &&
              !utf8::IsValid("\xC0\x80") && !utf8::IsValid("\xED\xA0\x80") && !utf8::IsValid("\xF4\x90\x80\x80") &&
              !utf8::IsValid("a\xE2\x82" "b") && !utf8::IsValid("ok\xF0\x9F\x98"));   // ...a broken sequence that reads as one is not
        // IsValid agrees with "decoding then encoding gives the same bytes" for every 1..3 byte string drawn from edge bytes
        const unsigned char edge[] = {0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC2, 0xDF, 0xE0, 0xE1, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF4, 0xF5, 0xFF};
        bool agree = true;
        for (unsigned char a : edge) for (unsigned char b : edge) for (unsigned char c : edge) {
            const std::string s{(char)a, (char)b, (char)c};
            std::string re;
            for (auto cp : Decode(s)) utf8::Append(re, cp);
            if (utf8::IsValid(s) != (re == s)) agree = false;
            if (!utf8::IsValid(utf8::Sanitize(s))) agree = false;
        }
        CHECK(agree);
        const std::string good = "h\xC3\xA9llo";
        CHECK(utf8::Sanitize(good) == good);
        CHECK(utf8::Sanitize("a\xFF" "b") == "a\xEF\xBF\xBD" "b");
        CHECK(utf8::Sanitize("\xE2\x82") == "\xEF\xBF\xBD");        // one replacement for the broken sequence
        CHECK(utf8::Length(utf8::Sanitize("\x80\x80\xC3")) == 3);
    }

    // ---- walking backwards reads the same pieces as walking forwards -------------------------------------------------------
    {
        const std::string t = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z";
        std::size_t i = t.size();
        std::vector<std::uint32_t> back;
        while (i > 0) back.push_back(utf8::Prev(t, i));
        const auto fwd = Decode(t);
        CHECK(back.size() == fwd.size());
        bool same = back.size() == fwd.size();
        for (std::size_t k = 0; same && k < fwd.size(); ++k) same = back[k] == fwd[fwd.size() - 1 - k];
        CHECK(same);
        std::size_t zero = 0;
        CHECK(utf8::Prev(t, zero) == 0 && zero == 0);                // at the start: nothing, and it stays
        std::size_t past = 999;
        CHECK(utf8::Prev(t, past) == 'z' && past == t.size() - 1);   // past the end counts as the end
        // property: on random bytes (mostly broken text) the backwards walk visits the forward walk's tokens in reverse
        std::uint32_t seed = 12345u;
        auto rnd = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
        int mismatches = 0;
        for (int round = 0; round < 4000; ++round) {
            std::string s;
            const int n = (int)(rnd() % 12);
            for (int k = 0; k < n; ++k) {
                const unsigned r = rnd() % 8;
                s += (char)(r < 2 ? 0x41 + (rnd() % 3) : r < 4 ? 0x80 + (rnd() % 64) : r < 6 ? 0xC2 + (rnd() % 0x33) : (rnd() & 0xFF));
            }
            std::vector<std::size_t> starts;
            for (std::size_t p = 0; p < s.size();) { starts.push_back(p); utf8::Next(s, p); }
            std::size_t q = s.size();
            for (std::size_t k = starts.size(); k-- > 0;) {
                utf8::Prev(s, q);
                if (q != starts[k]) { ++mismatches; break; }
            }
        }
        CHECK(mismatches == 0);
    }

    // ---- Substr / Truncate / TruncateBytes -------------------------------------------------------------------------------
    {
        const std::string t = "h\xC3\xA9llo \xE2\x82\xAC\xF0\x9F\x98\x80";   // 8 code points, 14 bytes (1+2+1+1+1+1+3+4)
        CHECK(utf8::Length(t) == 8 && t.size() == 14);
        CHECK(utf8::Substr(t, 1, 2) == "\xC3\xA9l");
        CHECK(utf8::Substr(t, 6) == "\xE2\x82\xAC\xF0\x9F\x98\x80");
        CHECK(utf8::Substr(t, 99).empty() && utf8::Substr(t, 0, 0).empty() && utf8::Substr(t, 7, 5) == "\xF0\x9F\x98\x80");
        CHECK(utf8::Truncate(t, 3) == "h\xC3\xA9l" && utf8::Truncate(t, 100) == t && utf8::Truncate(t, 0).empty());
        CHECK(utf8::TruncateBytes(t, 100) == t);
        CHECK(utf8::TruncateBytes(t, 2) == "h");                    // would cut the e-acute: stop before it
        CHECK(utf8::TruncateBytes(t, 3) == "h\xC3\xA9");
        CHECK(utf8::TruncateBytes(t, 9) == "h\xC3\xA9llo ");        // the euro sign would end at byte 10
        CHECK(utf8::TruncateBytes(t, 10) == "h\xC3\xA9llo \xE2\x82\xAC");
        CHECK(utf8::TruncateBytes(t, 13) == "h\xC3\xA9llo \xE2\x82\xAC");   // the emoji needs 4
        CHECK(utf8::TruncateBytes(t, 0).empty());
        bool boundary = true;                                       // every cut of every length ends on a boundary
        for (std::size_t n = 0; n <= t.size(); ++n) {
            const std::string c = utf8::TruncateBytes(t, n);
            if (c.size() > n || !utf8::IsValid(c) || t.compare(0, c.size(), c) != 0) boundary = false;
        }
        CHECK(boundary);
    }

    // ---- UTF-16 / UTF-32 / wide ---------------------------------------------------------------------------------------------
    {
        const std::string t = "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80";
        const std::u16string u16 = utf8::ToUtf16(t);
        CHECK(u16.size() == 10 && u16[1] == 0xE9 && u16[6] == 0x20AC && u16[8] == 0xD83D && u16[9] == 0xDE00);   // the emoji is a surrogate pair
        CHECK(utf8::FromUtf16(u16) == t);
        const std::u32string u32 = utf8::ToUtf32(t);
        CHECK(u32.size() == 9 && u32[8] == 0x1F600 && utf8::FromUtf32(u32) == t);
        CHECK(utf8::FromWide(utf8::ToWide(t)) == t);
        CHECK(utf8::ToUtf16("").empty() && utf8::FromUtf16(u"").empty() && utf8::ToUtf32("").empty());
        // lone surrogates (a UTF-16 string cut in the middle of a pair) and broken UTF-8 become U+FFFD, never a crash
        CHECK(utf8::FromUtf16(std::u16string(1, (char16_t)0xD83D)) == "\xEF\xBF\xBD");
        CHECK(utf8::FromUtf16(std::u16string(1, (char16_t)0xDE00)) == "\xEF\xBF\xBD");
        const std::u16string wrongOrder{(char16_t)0xDE00, (char16_t)0xD83D};
        CHECK(utf8::FromUtf16(wrongOrder) == "\xEF\xBF\xBD\xEF\xBF\xBD");   // wrong order: two lone halves
        const std::u16string halfThenA{(char16_t)0xD83D, u'a'};
        CHECK(utf8::FromUtf16(halfThenA) == "\xEF\xBF\xBD" "a");
        const std::u16string brokenAsU16{u'a', (char16_t)0xFFFD, u'b'};
        CHECK(utf8::ToUtf16("a\xFF" "b") == brokenAsU16);
        const std::u32string badScalars{U'a', (char32_t)0x110000, (char32_t)0xD800};
        CHECK(utf8::FromUtf32(badScalars) == "a\xEF\xBF\xBD\xEF\xBF\xBD");
        // every scalar value round-trips through UTF-16 and UTF-32 (the valid range, sampled around the edges)
        bool all = true;
        for (std::uint32_t cp : {0u, 0x7Fu, 0x80u, 0x7FFu, 0x800u, 0xD7FFu, 0xE000u, 0xFFFDu, 0xFFFFu, 0x10000u, 0x1F600u, 0x10FFFFu}) {
            std::string s; utf8::Append(s, cp);
            if (utf8::FromUtf16(utf8::ToUtf16(s)) != s || utf8::FromUtf32(utf8::ToUtf32(s)) != s || utf8::FromWide(utf8::ToWide(s)) != s) all = false;
        }
        CHECK(all);
    }

    TEST_MAIN_RESULT();
}
