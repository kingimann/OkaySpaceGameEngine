// A class template instance's name stays inside its own scope: after map<string, int> (whose
// comparison is less<string>), a parameter called less in std::stable_sort is still called as
// less(a, b). Mid-round 22 the instance's name leaked out ("'less<i>' has no constructor").
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

int main() {
    std::map<std::string, int> m;
    m["b"] = 2;
    m["a"] = 1;
    std::vector<int> order = {3, 1, 2};
    std::stable_sort(order.begin(), order.end(), [](int a, int b) { return a > b; });
    std::printf("%d %d %d %d\n", order[0], order[1], order[2], m.begin()->second);
    return 0;
}
