// std::begin and std::end of arrays and containers ([iterator.range]), std::atoll.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
struct Prop { int v; };
int main() {
    const Prop sp[] = {{1}, {2}, {3}};
    std::vector<Prop> span;
    span.assign(std::begin(sp), std::end(sp));
    int xs[4] = {4, 5, 6, 7};
    int sum = 0;
    for (const int* p = std::begin(xs); p != std::end(xs); ++p) sum += *p;
    std::vector<int> v = {10, 20, 30};
    const std::vector<int>& cv = v;
    for (auto it = std::begin(v); it != std::end(v); ++it) sum += *it;
    for (auto it = std::begin(cv); it != std::end(cv); ++it) sum += *it;
    std::string word = "abc";
    for (auto it = std::begin(word); it != std::end(word); ++it) sum += *it - 'a';
    long long big = std::atoll("-9000000000123");
    std::printf("%zu %d %d %lld %lld\n", span.size(), span[2].v, sum, big, atoll("42"));
    return 0;
}
