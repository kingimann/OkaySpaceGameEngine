// The definitions for later25_main.cpp.
#include <cstdio>
#include <vector>
struct Later {
    Later();
    explicit Later(int n);
    ~Later();
    std::vector<int> v;
};
Later::Later() { v.push_back(1); }
Later::Later(int n) : v(n, 0) {}
Later::~Later() { std::printf("dtor %d\n", (int)v.size()); }
