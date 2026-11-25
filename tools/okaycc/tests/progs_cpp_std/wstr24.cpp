// Round 24: char16_t, char32_t and wchar_t, and the wide strings u16string, u32string and wstring
// (built by +=, from an iterator range, iterated, compared), as Core/Utf8.hpp uses them.
#include <cstdio>
#include <string>
static std::u16string toUtf16(const std::u32string &s) {
    std::u16string out;
    out.reserve(s.size());
    for (char32_t c : s) {
        if (c >= 0x10000) {
            c -= 0x10000;
            out += (char16_t)(0xD800 + (c >> 10));
            out += (char16_t)(0xDC00 + (c & 0x3FF));
        } else {
            out += (char16_t)c;
        }
    }
    return out;
}
int main() {
    char16_t a = 0x41;
    char32_t b = 0x1F600;
    wchar_t c = 66;
    printf("%d %d %d %d %d %d\n", (int)a, (int)b, (int)c, (int)sizeof(a), (int)sizeof(b), (int)sizeof(c));
    std::u32string s;
    s += (char32_t)'h';
    s += (char32_t)0x1F600;
    s += (char32_t)'i';
    std::u16string u = toUtf16(s);
    printf("%d:", (int)u.size());
    for (char16_t x : u) printf(" %X", (unsigned)x);
    printf("\n");
    std::wstring w(u.begin(), u.end());
    std::u16string back(w.begin(), w.end());
    printf("%d %d %d %d\n", (int)w.size(), back == u ? 1 : 0, back != u ? 1 : 0, (int)w.substr(1, 2).size());
    std::u16string e;
    printf("%d %d\n", e.empty() ? 1 : 0, (int)(e < u));
}
