// Round 32: u"..", U".." and L".." string literals ([lex.string] p6-p10: arrays of char16_t in UTF-16,
// char32_t and wchar_t), u16string_view and hash of the wide strings ([string.view.template],
// [basic.string.hash]), and the u16string members the engine's JavaScript runtime uses
// (JavaScript/JavaScript.hpp:456, JsLibJson.cpp:204).
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
static void show(const std::u16string& s) {
    for (char16_t c : s) std::printf("%x ", (unsigned)c);
    std::printf("| %d\n", (int)s.size());
}
int main() {
    const char16_t* a = u"hé\U0001F600!";
    std::u16string s = a;
    show(s);
    std::u16string out;
    out += u"\\\"";
    out += u'x';
    out += s.substr(1, 1);
    out.insert(out.begin(), u'[');
    out.append(2, u'.');
    show(out);
    std::u16string t = u"get " + std::u16string(u"name") + u"()";
    show(t);
    std::printf("%d %d %d %d\n", (int)t.find(u"name"), (int)t.rfind(u'e'), (int)t.find_first_of(u"()"),
                (int)(t.find(u"zz") == std::u16string::npos));
    std::u16string_view v(t);
    std::u16string_view w = v.substr(4, 4);
    std::unordered_map<std::u16string_view, int> atoms;
    atoms[w] = 7;
    atoms[std::u16string_view(u"other")] = 9;
    std::unordered_map<std::u16string, int> reg;
    reg[u"sym"] = 3;
    std::printf("%d %d %d %d %d\n", atoms[std::u16string_view(u"name")], atoms[u"other"], reg[u"sym"],
                (int)(sizeof(u"abc") / sizeof(char16_t)), (int)(sizeof(U"ab") / sizeof(char32_t)));
    const char32_t* u = U"\U0001F600z";
    const wchar_t* l = L"wide";
    std::printf("%x %x %x %d\n", (unsigned)u[0], (unsigned)u[1], (unsigned)l[0], (int)std::u16string(u"abc").compare(u"abd"));
    t.erase(0, 4);
    t.resize(4);
    show(t);
    return 0;
}
