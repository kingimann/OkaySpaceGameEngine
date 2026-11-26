// string::assign(view, pos, n) and string(view, pos, n) ([string.assign], [string.cons]):
// SplitInto in Render/TextReaderUtil.hpp:161 (10 engine files).
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
inline void SplitInto(std::string_view s, std::vector<std::string>& r) {
    std::size_t n = 0;
    auto put = [&](std::size_t a, std::size_t len) {
        if (n < r.size()) r[n].assign(s, a, len); else r.emplace_back(s, a, len);
        ++n;
    };
    std::size_t i = 0, st = 0;
    for (; i <= s.size(); ++i)
        if (i == s.size() || s[i] == ' ') { if (i > st) put(st, i - st); st = i + 1; }
    r.resize(n);
}
int main() {
    std::vector<std::string> r;
    SplitInto("alpha beta  gamma", r);
    for (auto& x : r) std::printf("[%s]", x.c_str());
    SplitInto("one two", r);
    for (auto& x : r) std::printf("[%s]", x.c_str());
    std::string t = "hello world";
    std::string u;
    u.assign(t, 6, 3);
    std::string_view sv = t;
    std::string v(sv, 1, 100);
    std::printf(" %s %s\n", u.c_str(), v.c_str());
}
