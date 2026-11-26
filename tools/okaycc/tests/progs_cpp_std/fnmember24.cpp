// Round 24: obj.f() where f is a std::function member (a class object with operator()), and this
// in a default member initializer and in lambdas of a member function.
#include <cstdio>
#include <functional>
struct Node {
    int v = 3;
    Node *self = this;
    std::function<int()> get = [this]() { return v * 2; };
    int run() {
        auto f = [this](int k) { return this->v + k; };
        auto g = [=]() { return v + 1; };
        return f(10) + g();
    }
};
int main() { Node n; printf("%d %d %d\n", n.run(), n.self == &n ? 1 : 0, n.get()); }
