// Round 21: our own <map> and <set> (an AVL tree) against libstdc++.
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>

struct Stats {
    int hits = 0;
    float weight = 1.0f;
};

int main() {
    std::map<int, std::string> names;
    names[3] = "three";
    names[1] = "one";
    names[2] = "two";
    names.insert(std::make_pair(5, std::string("five")));
    names.emplace(4, "four");
    auto r = names.insert(std::make_pair(1, std::string("uno")));
    std::printf("size %lu inserted %d first %s\n", names.size(), (int)r.second, r.first->second.c_str());
    for (auto it = names.begin(); it != names.end(); ++it) std::printf("%d=%s ", it->first, it->second.c_str());
    std::printf("\n");
    std::printf("find %d count %lu %lu at %s\n", names.find(9) == names.end() ? 1 : 0, names.count(2), names.count(7),
                names.at(4).c_str());
    names.erase(3);
    auto next = names.erase(names.find(1));
    std::printf("after erase %lu next %d\n", names.size(), next->first);
    auto lb = names.lower_bound(3);
    auto ub = names.upper_bound(4);
    std::printf("bounds %d %d\n", lb->first, ub->first);
    auto last = names.end();
    --last;
    std::printf("last %d\n", last->first);

    std::map<std::string, Stats> stats;
    const char *words[] = {"b", "a", "c", "a", "b", "a"};
    for (const char *w : words) stats[w].hits += 1;
    for (auto &kv : stats) std::printf("%s:%d:%.1f ", kv.first.c_str(), kv.second.hits, kv.second.weight);
    std::printf("\n");

    // Many keys in order and out of order: the tree stays balanced and ordered.
    std::map<int, int> big;
    for (int i = 0; i < 2000; ++i) big[i] = i * 2;
    for (int i = 0; i < 2000; i += 3) big.erase(i);
    for (int i = 5000; i > 4000; i -= 7) big[i] = 1;
    long sum = 0;
    int prev = -1;
    bool ordered = true;
    for (auto &kv : big) {
        sum += kv.first + kv.second;
        if (kv.first <= prev) ordered = false;
        prev = kv.first;
    }
    std::printf("big %lu %ld %d\n", big.size(), sum, (int)ordered);
    std::map<int, int> copy = big;
    copy[1] = 77;
    std::printf("copy %lu %d %d\n", copy.size(), copy[1], big[1]);

    std::set<int> s = {5, 1, 4, 1, 3};
    s.insert(2);
    s.insert(4);
    for (int v : s) std::printf("%d ", v);
    std::printf("| %lu %lu %lu\n", s.size(), s.count(4), s.count(9));
    s.erase(1);
    std::printf("first %d\n", *s.begin());
    std::set<std::string> ss;
    ss.insert("pear");
    ss.insert("apple");
    ss.insert("fig");
    for (auto it = ss.begin(); it != ss.end(); ++it) std::printf("%s ", it->c_str());
    std::printf("\n");
    std::map<int, int> il = {{1, 10}, {2, 20}};
    std::printf("il %d %d\n", il[1], il[2]);
    return 0;
}
