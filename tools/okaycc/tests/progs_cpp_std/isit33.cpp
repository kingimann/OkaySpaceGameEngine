// Round 33: std::istream_iterator ([istream.iterator]) and a vector made from a pair of them in a
// braced return; engine/src/JavaScript/JavaScriptVM.cpp's Names(list).
#include <cstdio>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
static std::vector<std::string> Names(const char* list) {
    std::istringstream in(list);
    return {std::istream_iterator<std::string>(in), std::istream_iterator<std::string>()};
}
int main() {
    std::vector<std::string> v = Names("  Math.abs  Math.max\tparseInt\n isNaN ");
    for (const std::string& s : v) std::printf("[%s]", s.c_str());
    std::printf(" %d\n", (int)v.size());
    std::istringstream nums("3 14 -15 92 x 7");
    int sum = 0, n = 0;
    for (std::istream_iterator<int> it(nums), end; it != end; ++it) {
        sum += *it;
        ++n;
    }
    std::printf("%d %d %d\n", sum, n, (int)Names("").size());
    std::istringstream two("a b");
    std::istream_iterator<std::string> a(two), b;
    std::string first = *a++;
    std::string second = a->c_str();
    const bool atEnd = ++a == b;
    std::printf("%s %s %d %d\n", first.c_str(), second.c_str(), (int)atEnd, (int)(a != b));
    return 0;
}
