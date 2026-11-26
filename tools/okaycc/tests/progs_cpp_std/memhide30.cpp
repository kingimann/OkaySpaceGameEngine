// A data member with the name of a function template (std::max, std::min) hides it inside the
// class's member functions, so `max < 0` compares the member (okayscript/Native.hpp:25).
#include <algorithm>
#include <cstdio>
struct Arity {
    int min = 0;
    int max = -1;
    bool Accepts(unsigned long n) const { return (int)n >= min && (max < 0 || (int)n <= max); }
    bool Below(int n) const;
};
bool Arity::Below(int n) const { return max < n && min < n; }
struct Wide : Arity {
    bool Big() const { return max > 10 || min > 5; }
};
int main() {
    Arity a{1, 3};
    Arity any{2};
    Wide w;
    w.max = 20;
    std::printf("%d %d %d %d\n", a.Accepts(2), a.Accepts(5), any.Accepts(9), any.Accepts(1));
    std::printf("%d %d %d\n", a.Below(4), a.Below(2), w.Big());
    std::printf("%d\n", std::max(a.min, a.max));
    return 0;
}
