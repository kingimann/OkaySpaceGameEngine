// Fixes from the review of round 17: a for loop whose first part has ?: (was taken for a range-for),
// vector::insert of one of its own elements, fmin and fmax with NaN, s += s.c_str() when s grows.
#include <cmath>
#include <string>
#include <vector>

extern "C" int printf(const char *fmt, ...);

int main(int argc, char **argv) {
    (void)argv;
    int n = 0;
    for (int i = argc > 5 ? 1 : 0; i < 3; ++i) n += i;
    int m = 0;
    for (int k = argc > 0 ? (argc > 7 ? 2 : 1) : 0; k < 4; ++k) m += k;
    printf("for %d %d\n", n, m);
    std::vector<int> v;
    v.push_back(2);
    v.push_back(4);
    v.push_back(6);
    v.push_back(8);  // full (capacity 4): the next insert grows the vector
    v.insert(v.begin(), v[1]);
    printf("insert");
    for (int x : v) printf(" %d", x);
    printf("\n");
    volatile double zero = 0.0;
    double nan = zero / zero;
    printf("fmin %d %d fmax %d %d\n", (int)std::fmin(nan, 1.0), (int)std::fmin(2.0, nan), (int)std::fmax(nan, 3.0),
           (int)std::fmax(4.0, nan));
    std::string s = "abcdefghij";
    s += s.c_str();
    s += s.c_str();
    printf("string %s %d\n", s.c_str(), (int)s.size());
    return 0;
}
