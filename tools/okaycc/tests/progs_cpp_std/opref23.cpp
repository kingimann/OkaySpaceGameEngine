// Round 23: a member operator that returns a reference, called on a temporary (*(it - 1) of a class
// iterator), gave a copy, not the element: "a reference must bind to an lvalue", and swap() in
// std::sort swapped two copies (the deque sort printed "-11 100 100").
#include <cstdio>
#include <deque>
#include <utility>
int main() {
    std::deque<int> d = {3, 1, 2};
    auto j = d.begin() + 1;
    std::printf("%d %d\n", *j, *(j - 1));
    int& r = *(j - 1);
    std::printf("r=%d\n", r);
    std::swap(*j, *(j - 1));
    std::printf("%d %d %d\n", d[0], d[1], d[2]);
    bool lt = *j < *(j - 1);
    std::printf("lt %d\n", (int)lt);
    return 0;
}
