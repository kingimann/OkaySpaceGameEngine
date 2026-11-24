// Braced lists for a class with an initializer-list constructor ([dcl.init.list], [over.match.list]):
// vector<int> v{7} has one element 7 (not seven elements), v{1, 2, 3} three.
#include <string>
#include <vector>

extern "C" int printf(const char *fmt, ...);

struct P {
    int a, b;
};

int main() {
    std::vector<int> one{7};
    std::vector<int> three{1, 2, 3};
    std::vector<int> seven(7);
    int r = 4;
    std::vector<int> fromVar{r, r + 1};
    std::vector<P> ps{{1, 2}, {3, 4}};
    std::vector<std::string> ss{"ab", "cd", "ef"};
    std::vector<int> copyList = {5, 6, 7};
    std::vector<int> copyOne = {9};
    printf("%d %d %d %d\n", (int)copyList.size(), copyList[2], (int)copyOne.size(), copyOne[0]);
    printf("%d %d | %d %d %d %d | %d | %d %d %d\n", (int)one.size(), one[0], (int)three.size(), three[0], three[1],
           three[2], (int)seven.size(), (int)fromVar.size(), fromVar[0], fromVar[1]);
    printf("%d %d %d | %d %s %s\n", (int)ps.size(), ps[0].a, ps[1].b, (int)ss.size(), ss[0].c_str(), ss[2].c_str());
    return 0;
}
