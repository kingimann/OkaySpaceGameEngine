// Round 32: std::next, std::prev, std::advance and std::distance with only a container header included,
// as g++'s containers give them (Scene/SceneMerge.cpp:96, Scene/ObjectPool.cpp:119; [iterator.operations]).
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>
#include <list>
int main() {
    std::map<int, int> m{{1, 10}, {2, 20}, {3, 30}};
    auto it = std::next(m.begin());
    auto last = std::prev(m.end());
    std::vector<int> v{4, 5, 6, 7};
    auto vi = std::next(v.begin(), 2);
    std::list<int> l{1, 2, 3};
    auto li = l.begin();
    std::advance(li, 2);
    std::unordered_map<int, int> u{{5, 1}};
    std::printf("%d %d %d %d %ld %ld\n", it->second, last->first, *vi, *li, (long)std::distance(v.begin(), v.end()),
                (long)std::distance(u.begin(), u.end()));
    return 0;
}
