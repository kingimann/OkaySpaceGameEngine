// Round 23: our own <string_view> (and std::string to and from it) and <clocale>.
#include <clocale>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
static int count(std::string_view s, char c) {
    int n = 0;
    for (char x : s)
        if (x == c) ++n;
    return n;
}
static void show(std::string_view s) { std::printf("[%.*s] %d\n", (int)s.size(), s.data(), (int)s.size()); }
int main() {
    std::string_view a = "key=value;other=2";
    std::size_t eq = a.find('=');
    std::string_view key = a.substr(0, eq);
    std::string_view rest = a.substr(eq + 1);
    show(key);
    show(rest);
    std::printf("semi %d last= %d none %d\n", (int)rest.find(';'), (int)a.rfind('='),
                (int)(a.find("zz") == std::string_view::npos));
    std::string owned = "path/to/file.txt";
    show(owned);
    std::printf("count %d %d\n", count(owned, '/'), count("a.b.c", '.'));
    std::string_view v = owned;
    v.remove_prefix(5);
    v.remove_suffix(4);
    show(v);
    std::string back(v);
    back += std::string_view("!!");
    std::printf("%s %d\n", back.c_str(), (int)back.size());
    std::printf("cmp %d %d %d %d\n", (int)(key == "key"), (int)(key < rest), (int)(std::string_view("abc").compare("abd") < 0),
                (int)(owned == std::string_view("path/to/file.txt")));
    std::printf("prefix %d copy %d\n", (int)(a.substr(0, 3) == "key"), (int)a.copy(&owned[0], 3, 4));
    std::printf("ff %d %d %d\n", (int)a.find_first_of("=;"), (int)a.find_last_of("=;"), (int)a.find_first_not_of("key"));
    std::unordered_map<std::string, int> m;
    m[std::string(key)] = 3;
    std::printf("map %d\n", m["key"]);
    std::printf("locale %s %s dp=%s\n", std::setlocale(LC_ALL, "C"), std::setlocale(LC_ALL, "") ? "C" : "null",
                std::localeconv()->decimal_point);
    return 0;
}
