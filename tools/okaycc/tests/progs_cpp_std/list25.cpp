// <list> ([list]): push and pop at both ends, insert and erase keep other iterators, splice moves
// a node to the front (an LRU), remove_if, sort (stable), reverse, unique and copies.
#include <cstdio>
#include <list>
#include <string>
#include <unordered_map>
static void show(const std::list<int> &l) {
    for (int x : l) std::printf("%d ", x);
    std::printf("(%d)\n", (int)l.size());
}
struct E {
    int k;
    std::string v;
};
int main() {
    std::list<int> l = {5, 3, 8};
    l.push_front(1);
    l.push_back(9);
    show(l);
    auto it = l.begin();
    ++it;
    ++it;
    auto keep = it;  // 3
    l.insert(it, 7);
    l.erase(l.begin());
    show(l);
    std::printf("%d %d %d\n", *keep, l.front(), l.back());
    l.splice(l.begin(), l, keep);
    show(l);
    l.remove_if([](int x) { return x > 7; });
    show(l);
    std::list<int> m = {4, 2, 2, 9, 1, 2, 6};
    m.sort();
    show(m);
    m.unique();
    m.reverse();
    show(m);
    std::list<int> c = m;
    c.pop_front();
    c.pop_back();
    show(c);
    std::printf("%d\n", (int)(c == m));
    std::list<E> lru;
    std::unordered_map<int, std::list<E>::iterator> idx;
    for (int i = 0; i < 4; ++i) {
        lru.push_front(E{i, std::to_string(i * 10)});
        idx[i] = lru.begin();
    }
    lru.splice(lru.begin(), lru, idx[1]);
    for (auto &e : lru) std::printf("%d:%s ", e.k, e.v.c_str());
    std::printf("\n");
    std::list<std::string> w(3, "ab");
    std::list<std::string> r(w.begin(), w.end());
    r.sort([](const std::string &a, const std::string &b) { return a > b; });
    std::printf("%s %d\n", r.front().c_str(), (int)r.size());
}
