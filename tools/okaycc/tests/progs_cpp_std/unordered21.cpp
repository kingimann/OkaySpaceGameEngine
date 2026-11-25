// Round 21: our own <unordered_map> and <unordered_set> (a chained hash table) against libstdc++.
// The order of a walk over an unordered container is unspecified, so walks only sum or count.
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class Kind { A, B, C };

struct Obj {
    int id;
    std::string name;
};

int main() {
    std::unordered_map<int, int> m;
    for (int i = 0; i < 1000; ++i) m[i * 7] = i;
    long sum = 0;
    for (auto &kv : m) sum += kv.first + kv.second;
    std::printf("size %lu sum %ld find %d %d\n", m.size(), sum, m.find(70)->second, m.find(71) == m.end() ? 1 : 0);
    for (int i = 0; i < 1000; i += 2) m.erase(i * 7);
    std::printf("after %lu count %lu %lu\n", m.size(), m.count(7), m.count(14));
    int erased = 0;
    for (auto it = m.begin(); it != m.end();) {
        if (it->second % 3 == 0) {
            it = m.erase(it);
            ++erased;
        } else {
            ++it;
        }
    }
    std::printf("erased %d left %lu\n", erased, m.size());

    std::unordered_map<std::string, Obj> byName;
    byName["alpha"] = Obj{1, "alpha"};
    byName.emplace("beta", Obj{2, "beta"});
    auto r = byName.insert(std::make_pair(std::string("alpha"), Obj{9, "x"}));
    std::printf("%d %d %s %lu\n", (int)r.second, byName["alpha"].id, byName.at("beta").name.c_str(), byName.size());
    byName.erase("alpha");
    std::printf("%lu %lu\n", byName.count("alpha"), byName.count("beta"));

    std::unordered_map<Kind, int> kinds;
    kinds[Kind::B] = 5;
    kinds[Kind::C] += 2;
    std::printf("kinds %d %d %lu\n", kinds[Kind::B], kinds[Kind::C], kinds.count(Kind::A));

    std::unordered_set<std::string> seen;
    const char *words[] = {"a", "b", "a", "c", "b", "d"};
    int dups = 0;
    for (const char *w : words)
        if (!seen.insert(w).second) ++dups;
    std::printf("seen %lu dups %d has %lu %lu\n", seen.size(), dups, seen.count("c"), seen.count("z"));
    std::unordered_set<int> ids = {3, 1, 4, 1, 5};
    int total = 0;
    for (int v : ids) total += v;
    std::printf("ids %lu %d\n", ids.size(), total);
    std::unordered_map<int, std::vector<int>> groups;
    for (int i = 0; i < 10; ++i) groups[i % 3].push_back(i);
    std::printf("groups %lu %lu %d\n", groups.size(), groups[1].size(), groups[2][1]);
    std::unordered_map<int, int> copy = m;
    copy[123456] = 1;
    std::printf("copy %lu %lu\n", copy.size(), m.size());
    return 0;
}
