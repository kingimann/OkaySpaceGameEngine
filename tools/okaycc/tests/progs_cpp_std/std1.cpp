// Our own standard headers (tools/okaycc/include) against libstdc++: okay-cc uses ours, g++ its own.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

extern "C" int printf(const char *fmt, ...);

struct Item {
    int id = 0;
    float w = 1.0f;
};

std::vector<int> evens(int n) {
    std::vector<int> out;
    for (int i = 0; i < n; ++i)
        if (i % 2 == 0) out.push_back(i);
    return out;
}

int total(std::vector<int> v) {
    int s = 0;
    for (int x : v) s += x;
    return s;
}

int main() {
    std::vector<int> v;
    for (int i = 0; i < 20; ++i) v.push_back(i * 3 % 7);
    printf("size %d front %d back %d empty %d\n", (int)v.size(), v.front(), v.back(), v.empty());
    std::vector<int> w = v;
    w[0] = 100;
    v = w;
    w.pop_back();
    printf("copy %d %d %d\n", v[0], (int)v.size(), (int)w.size());
    auto it = std::find(v.begin(), v.end(), 5);
    std::ptrdiff_t at = it - v.begin();
    v.erase(it);
    printf("find %d now %d size %d\n", (int)at, v[at], (int)v.size());
    int limit = 4;
    auto big = std::find_if(v.begin(), v.end(), [&](int x) { return x > limit && x < 100; });
    printf("find_if %d\n", big == v.end() ? -1 : *big);
    std::stable_sort(v.begin(), v.end(), [](int a, int b) { return a < b; });
    for (int x : v) printf("%d ", x);
    printf("\n");
    std::vector<Item> items;
    for (int i = 0; i < 5; ++i) {
        Item it2;
        it2.id = i;
        it2.w = (float)(i % 3);
        items.push_back(it2);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.w < b.w; });
    for (const Item &x : items) printf("%d:%d ", x.id, (int)x.w);
    printf("\n");
    std::vector<int> e = evens(9);
    int esize = (int)e.size();
    int etotal = total(std::move(e));
    printf("evens %d total %d moved %d\n", esize, etotal, total(evens(5)));
    std::vector<const Item *> ptrs;
    for (Item &x : items) ptrs.push_back(&x);
    std::reverse(ptrs.begin(), ptrs.end());
    printf("ptr %d %d\n", ptrs[0]->id, ptrs.back()->id);
    int a = 3, b = 9;
    std::swap(a, b);
    printf("minmax %d %d %d %d\n", std::min(a, b), std::max(a, b), (int)(std::max(0.5, 1.25) * 4), a);
    printf("math %d %d %d %d %d\n", (int)(std::fabs(-2.5) * 2), (int)std::floor(-2.5), (int)std::ceil(2.25),
           (int)std::floor(7.0), std::isfinite(1.0));
    printf("limits %d %d %u %ld %d\n", std::numeric_limits<int>::max(), std::numeric_limits<int>::min(),
           (unsigned)std::numeric_limits<unsigned char>::max(), std::numeric_limits<long>::max(),
           std::numeric_limits<short>::is_signed);
    std::uint8_t u8 = 250;
    u8 += 10;
    std::int64_t big64 = INT64_MAX;
    std::size_t sz = sizeof(std::uint16_t) + sizeof(std::int32_t);
    printf("int %d %d %d\n", u8, big64 == 9223372036854775807L, (int)sz);
    std::string s = "okay";
    s += "-cc";
    s.push_back('!');
    std::string t = s;
    t[0] = 'O';
    printf("string %s %s %d %d %d\n", s.c_str(), t.c_str(), (int)s.size(), s == "okay-cc!", t.compare(s) < 0);
    return 0;
}
