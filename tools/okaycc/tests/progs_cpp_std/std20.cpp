// Round 20: more of our own <cstring>, <cctype>, <string>, <vector> and <algorithm> against libstdc++.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

extern "C" int printf(const char *fmt, ...);

struct Finger {
    long long id;
    float x;
};

static int sign(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

int main() {
    // <cstring>
    const char *a = "RIFF....WAVE";
    printf("%d %d %d\n", std::memcmp(a, "RIFF", 4), sign(std::memcmp(a + 8, "WAVX", 4)), sign(memcmp("ab", "aa", 2)));
    char buf[16] = "abcdefgh";
    std::memmove(buf + 2, buf, 5);
    printf("%s\n", buf);
    std::memmove(buf, buf + 3, 4);
    printf("%s\n", buf);
    const char *m = (const char *)std::memchr(a, 'W', 12);
    printf("%d %d\n", (int)(m - a), std::memchr(a, 'z', 12) == nullptr);
    printf("%d %d %d\n", sign(std::strncmp("apple", "apply", 4)), sign(std::strncmp("apple", "apply", 5)),
           sign(strncmp("a", "a", 9)));
    const char *p = "path/to/file.txt";
    printf("%s %s %s %d\n", std::strchr(p, '/'), std::strrchr(p, '/'), std::strstr(p, "file"),
           std::strstr(p, "nope") == nullptr);
    // <cctype>
    int counts[6] = {0, 0, 0, 0, 0, 0};
    const char *mix = "Hello, World 42!\t\n";
    for (const char *c = mix; *c; ++c) {
        unsigned char u = (unsigned char)*c;
        if (std::isalpha(u)) ++counts[0];
        if (std::isdigit(u)) ++counts[1];
        if (std::isspace(u)) ++counts[2];
        if (std::ispunct(u)) ++counts[3];
        if (std::isupper(u)) ++counts[4];
        if (isxdigit(u)) ++counts[5];
    }
    printf("%d %d %d %d %d %d %c%c\n", counts[0], counts[1], counts[2], counts[3], counts[4], counts[5],
           std::toupper('q'), std::tolower('Q'));
    // <string>
    std::string s = "  key = value  ";
    std::size_t b = s.find_first_not_of(' ');
    std::size_t e = s.find_last_not_of(' ');
    std::string t = s.substr(b, e - b + 1);
    printf("[%s] %d\n", t.c_str(), (int)t.size());
    std::size_t eq = t.find('=');
    std::string k = t.substr(0, eq), v = t.substr(eq + 1);
    printf("[%s] [%s] %d %d\n", k.c_str(), v.c_str(), (int)t.find("value"), t.find("zzz") == std::string::npos);
    printf("%d %d %d\n", (int)t.rfind('e'), (int)t.find_first_of("=v"), (int)t.find_last_of("ky"));
    std::string r(3, 'x');
    std::string n("abcdef", 4);
    printf("%s %s %c %c\n", r.c_str(), n.c_str(), n.front(), n.back());
    n.erase(1, 2);
    n.insert(1, "XY");
    n.pop_back();
    printf("%s %d\n", n.c_str(), (int)n.size());
    n.resize(6, '-');
    printf("%s\n", n.c_str());
    std::string joined = "<" + n + ">";
    printf("%s %d %d %d\n", joined.c_str(), joined == "<aXY--->" , "x" == r.substr(0, 1), joined != "q");
    printf("%d %d\n", sign(n.compare("aXY")), sign(std::string("b").compare("a")));
    int up = 0;
    for (char c : joined)
        if (c == '-') ++up;
    printf("%d\n", up);
    // <vector> and <algorithm>
    std::vector<Finger> f;
    for (int i = 0; i < 6; ++i) f.push_back({i * 10, i * 0.5f});
    long long gone = 20;
    f.erase(std::remove_if(f.begin(), f.end(), [&](const Finger &x) { return x.id == gone || x.id == 40; }), f.end());
    for (const Finger &x : f) printf("%lld ", x.id);
    printf("| %d\n", (int)f.size());
    std::vector<int> w = {5, 1, 4, 1, 5, 9, 2, 6};
    w.erase(std::remove(w.begin(), w.end(), 1), w.end());
    printf("%d %d %d %d\n", (int)w.size(), (int)std::count(w.begin(), w.end(), 5),
           *std::min_element(w.begin(), w.end()), *std::max_element(w.begin(), w.end()));
    printf("%d %d %d\n", std::any_of(w.begin(), w.end(), [](int x) { return x > 8; }),
           std::all_of(w.begin(), w.end(), [](int x) { return x > 1; }),
           std::none_of(w.begin(), w.end(), [](int x) { return x == 3; }));
    std::sort(w.begin(), w.end());
    for (int x : w) printf("%d ", x);
    printf("| %d %d\n", (int)(std::lower_bound(w.begin(), w.end(), 5) - w.begin()),
           (int)(std::upper_bound(w.begin(), w.end(), 5) - w.begin()));
    w.erase(std::unique(w.begin(), w.end()), w.end());
    std::vector<int> c2(w.size());
    std::copy(w.begin(), w.end(), c2.begin());
    for (int x : c2) printf("%d ", x);
    printf("| %d %d %d\n", std::clamp(12, 0, 10), std::clamp(-3, 0, 10), std::min(3, 7));
    return 0;
}
