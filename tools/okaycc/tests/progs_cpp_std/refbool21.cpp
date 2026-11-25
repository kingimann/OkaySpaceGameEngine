// !f() and if (f()) with f returning a smart pointer by reference (round 21: "operand is not an
// integer", engine/src/Platform/Steam/Steam.cpp).
#include <memory>
#include <cstdio>
struct I { virtual ~I() {} int v = 3; };
std::unique_ptr<I> &Service() { static std::unique_ptr<I> s; return s; }
int main() {
    int a = 0;
    if (!Service()) a = 1;
    Service().reset(new I());
    if (Service()) a += 10;
    std::printf("%d %d\n", a, Service() ? Service()->v : -1);
    return 0;
}
