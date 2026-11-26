// A default member initializer in braces for a class with an initializer-list constructor
// ([class.mem] p10, [dcl.init.list] p3.6): items{"Copy", "Paste"} (Components/UIContextMenu.hpp).
#include <cstdio>
#include <string>
#include <vector>
struct Menu {
    std::vector<std::string> items{"Copy", "Paste", "-", "Delete"};
    std::vector<int> nums = {3, 1, 2};
    std::vector<int> two{5, 6};
    std::vector<int> none{};
    int k = 2;
};
struct Sub : Menu {
    std::vector<std::string> more{"x"};
};
int main() {
    Menu m;
    Sub s;
    std::printf("%zu %s %zu %d %zu %zu %zu %s\n", m.items.size(), m.items[3].c_str(), m.nums.size(), m.two[1], m.none.size(),
                s.items.size(), s.more.size(), s.more[0].c_str());
    Menu c = m;
    std::printf("%s %d\n", c.items[1].c_str(), c.nums[0]);
}
