// Our own <iterator>: next, prev, distance, advance, back_inserter.
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <vector>

int main() {
    std::vector<int> v = {3, 1, 4, 1, 5};
    std::vector<int>::iterator it = std::next(v.begin(), 2);
    std::printf("%d %d %ld\n", *it, *std::prev(v.end()), std::distance(v.begin(), v.end()));
    std::advance(it, -1);
    std::printf("%d\n", *it);
    std::vector<int> out;
    std::copy(v.begin(), v.end(), std::back_inserter(out));
    std::printf("%d %d\n", (int)out.size(), out[4]);
    std::map<std::string, int> m;
    m["a"] = 1;
    m["b"] = 2;
    m["c"] = 3;
    auto mi = std::next(m.begin());
    std::printf("%s %d %ld\n", mi->first.c_str(), mi->second, std::distance(m.begin(), m.end()));
    return 0;
}
