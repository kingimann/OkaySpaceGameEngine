// A local of an inner block with the name of an object with a destructor of an enclosing block:
// a return, break or continue from the inner block destroys both objects, each once. Before round
// 30 the outer one was destroyed by name, which there meant the inner one (silent wrong code: the
// inner object destroyed twice, the outer one never), or did not compile with another type.
#include <cstdio>
#include <string>
#include <vector>
struct T {
    int id;
    explicit T(int i) : id(i) { std::printf("T %d\n", id); }
    ~T() { std::printf("~T %d\n", id); }
};
static int f(int s) {
    T v(1);
    for (int i = 0; i < 3; ++i) {
        T v(10 + i);
        if (i == s) return v.id;
        if (i == 1) continue;
    }
    {
        T v(5);
        if (s == 7) return 0;
    }
    return -1;
}
struct Box {
    int n = 4;
    int Get() const { return n; }
};
static Box gBox;
static Box* Find(int s) { return s ? &gBox : nullptr; }
static std::string g(int s) {
    const std::vector<std::string> v = {"a", "b"};
    if (s >= (int)v.size()) return std::string();
    if (auto* v = Find(s)) return std::to_string(v->Get());
    while (true) {
        std::vector<int> v(3, s);
        if (v.size() == 3) break;
    }
    return v[0];
}
int main() {
    std::printf("%d\n", f(0));
    std::printf("%d\n", f(2));
    std::printf("%d\n", f(7));
    std::printf("%d\n", f(9));
    std::printf("%s %s %s\n", g(1).c_str(), g(0).c_str(), g(5).c_str());
    return 0;
}
