// Round 24: a comma fold whose pattern holds a less-than inside brackets ((n += (v < 10) ? 1 : 0),
// ...): the '<' was taken for a template argument list and the copies lost a bracket.
#include <cstdio>
template <class... T>
int countSmall(T... v) {
    int n = 0;
    ((n += (v < 10) ? 1 : 0), ...);
    return n;
}
template <class... T>
int firstBig(T... v) {
    int j = 0, r = -1;
    ((r = (r < 0 && v > 9) ? j : r, ++j), ...);
    return r;
}
int main() { printf("%d %d %d\n", countSmall(1, 20, 3, 40L), firstBig(1, 2, 30, 40), firstBig()); }
