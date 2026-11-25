// Round 23: our own <deque> (a ring buffer): both ends, indexing, iterators, erase and insert,
// elements with destructors.
#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
int main() {
    std::deque<int> d;
    for (int i = 0; i < 5; ++i) d.push_back(i);
    for (int i = 1; i <= 12; ++i) d.push_front(-i);
    d.pop_front();
    d.pop_back();
    std::printf("size %d front %d back %d [3] %d\n", (int)d.size(), d.front(), d.back(), d[3]);
    long sum = 0;
    for (int x : d) sum += x;
    std::printf("sum %ld dist %ld\n", sum, (long)(d.end() - d.begin()));
    d.erase(d.begin() + 2, d.begin() + 5);
    d.insert(d.begin() + 1, 100);
    for (auto it = d.begin(); it != d.end(); ++it) std::printf("%d ", *it);
    std::printf("\n");
    std::sort(d.begin(), d.end());
    std::printf("sorted %d %d %d\n", d[0], d[1], d.back());
    std::deque<std::string> q = {"a", "b"};
    q.emplace_back("c");
    q.push_front("z");
    while (!q.empty()) {
        std::printf("%s", q.front().c_str());
        q.pop_front();
    }
    std::printf("\n");
    std::deque<std::string> r(3, "x");
    std::deque<std::string> r2 = r;
    r2.back() = "y";
    std::printf("%s%s%s %s %d\n", r2[0].c_str(), r2[1].c_str(), r2[2].c_str(), r[2].c_str(), (int)r2.size());
    return 0;
}
