// Round 24: <numeric>: iota over a vector, accumulate (plain and with an operation),
// inner_product, partial_sum, adjacent_difference, gcd and lcm.
#include <cstdio>
#include <numeric>
#include <vector>
int main() {
    std::vector<int> v(6);
    std::iota(v.begin(), v.end(), 1);
    int sum = std::accumulate(v.begin(), v.end(), 0);
    long prod = std::accumulate(v.begin(), v.end(), 1L, [](long a, int b) { return a * b; });
    int dot = std::inner_product(v.begin(), v.end(), v.begin(), 0);
    std::vector<int> ps(6), ad(6);
    std::partial_sum(v.begin(), v.end(), ps.begin());
    std::adjacent_difference(ps.begin(), ps.end(), ad.begin());
    printf("%d %ld %d %d %d %d %d\n", sum, prod, dot, ps[5], ad[3], std::gcd(84, -36), std::lcm(4, 6));
}
