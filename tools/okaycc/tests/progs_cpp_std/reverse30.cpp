// std::reverse_iterator and rbegin/rend of vector, string, deque, map, set and list
// ([reverse.iterators]; Components/ModelAnimator.hpp:880, AI/GridAStar.hpp:151).
#include <cstdio>
#include <deque>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>
struct Node {
    int id;
    std::vector<Node*> kids;
};
int main() {
    std::vector<int> v{1, 2, 3, 4};
    for (auto it = v.rbegin(); it != v.rend(); ++it) std::printf("%d ", *it);
    std::printf("\n");
    std::vector<int> r(v.rbegin(), v.rend());
    std::printf("%d %d %d\n", r[0], r[3], (int)(v.rend() - v.rbegin()));
    *v.rbegin() = 40;
    std::printf("%d %d\n", v[3], v.rbegin()[1]);
    const std::vector<int>& cv = v;
    int sum = 0;
    for (auto it = cv.crbegin(); it != cv.crend(); it++) sum = sum * 10 + *it % 10;
    std::printf("%d\n", sum);
    std::string s = "abc";
    std::string t(s.rbegin(), s.rend());
    std::printf("%s\n", t.c_str());
    std::map<int, std::string> m{{1, "one"}, {3, "three"}, {2, "two"}};
    std::printf("%d %s\n", m.rbegin()->first, m.rbegin()->second.c_str());
    for (auto it = m.rbegin(); it != m.rend(); ++it) std::printf("%s ", it->second.c_str());
    std::printf("\n");
    std::set<int> used{5, 9, 2};
    std::printf("%d\n", *used.rbegin());
    std::deque<int> d{7, 8, 9};
    for (auto it = d.rbegin(); it != d.rend(); ++it) std::printf("%d ", *it);
    std::list<std::string> l{"x", "y", "z"};
    for (auto it = l.rbegin(); it != l.rend(); ++it) std::printf("%s ", it->c_str());
    std::printf("\n");
    Node c{3, {}}, b{2, {}}, a{1, {&b, &c}};
    std::vector<Node*> stack{&a};
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        std::printf("%d ", n->id);
        const auto& ch = n->kids;
        for (auto it = ch.rbegin(); it != ch.rend(); ++it) stack.push_back(*it);
    }
    std::printf("\n");
    auto rb = std::make_reverse_iterator(v.end());
    std::printf("%d %d\n", *rb, *rb.base() == 0 || true);
    return 0;
}
