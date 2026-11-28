// Round 33: std::make_heap, push_heap, pop_heap, sort_heap ([alg.heap.operations]) and
// std::adjacent_find ([alg.adjacent.find]); priority_queue now uses the same heap functions.
// engine/src/Core/ImageWrite.cpp (make_heap) and engine/src/Browser/WebFont.cpp (adjacent_find).
#include <algorithm>
#include <cstdio>
#include <functional>
#include <queue>
#include <vector>
struct Item {
    int key, id;
};
int main() {
    std::vector<int> v = {5, 1, 9, 3, 7, 2, 8};
    std::make_heap(v.begin(), v.end());
    std::printf("top %d\n", v.front());
    v.push_back(11);
    std::push_heap(v.begin(), v.end());
    std::pop_heap(v.begin(), v.end());
    std::printf("popped %d top %d\n", v.back(), v.front());
    v.pop_back();
    std::sort_heap(v.begin(), v.end());
    for (int x : v) std::printf("%d ", x);
    std::printf("\n");
    std::vector<Item> items = {{4, 0}, {1, 1}, {6, 2}, {3, 3}};
    auto byKey = [](const Item& a, const Item& b) { return a.key > b.key; };  // a min-heap by key
    std::make_heap(items.begin(), items.end(), byKey);
    std::printf("min %d\n", items.front().key);
    std::sort_heap(items.begin(), items.end(), byKey);
    for (const Item& it : items) std::printf("%d:%d ", it.key, it.id);
    std::printf("\n");
    std::vector<int> runs = {1, 2, 3, 3, 4, 4};
    auto a = std::adjacent_find(runs.begin(), runs.end());
    auto b = std::adjacent_find(runs.begin(), runs.end(), [](int x, int y) { return y == x + 2; });
    std::vector<int> none = {1, 2, 3};
    std::printf("%d %d %d\n", (int)(a - runs.begin()), (int)(b - runs.begin()),
                (int)(std::adjacent_find(none.begin(), none.end()) == none.end()));
    std::priority_queue<int, std::vector<int>, std::greater<int> > pq;
    for (int x : {5, 2, 8, 1, 9}) pq.push(x);
    while (!pq.empty()) {
        std::printf("%d ", pq.top());
        pq.pop();
    }
    std::printf("\n");
    return 0;
}
