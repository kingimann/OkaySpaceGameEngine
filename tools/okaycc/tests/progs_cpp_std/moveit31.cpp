// std::move_iterator and make_move_iterator ([move.iterators]) with vector::insert
// (Core/EventBus.hpp).
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
int main() {
    std::vector<std::string> batch = {"a", "bb", "ccc", "dddd"};
    std::vector<std::string> queue = {"x"};
    queue.insert(queue.begin(), std::make_move_iterator(batch.begin() + 1), std::make_move_iterator(batch.end()));
    for (const auto& s : queue) std::printf("%s ", s.c_str());
    std::printf("| %zu\n", queue.size());
    std::vector<std::function<int(int)>> fs;
    fs.push_back([](int a) { return a + 1; });
    fs.push_back([](int a) { return a * 2; });
    std::vector<std::function<int(int)>> q;
    q.insert(q.begin(), std::make_move_iterator(fs.begin()), std::make_move_iterator(fs.end()));
    std::printf("%d %d %zu\n", q[0](5), q[1](5), q.size());
    return 0;
}
