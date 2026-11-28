// Round 33: a lambda that returns c ? T() : x of a class, with an auto local, converted to a
// pointer to a function ([expr.prim.lambda.closure] p8, [expr.cond] p6-p7);
// engine/src/JavaScript/JsLibCollections.cpp's Map.prototype.get. It was "not lowered".
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
struct Value {
    int t = 0;
    std::string s;
    Value() {}
    Value(int x) : t(x), s(std::to_string(x)) {}
};
struct Entry {
    Value k, v;
};
struct MapObj {
    std::unordered_map<int, std::size_t> index;
    std::vector<Entry> entries;
};
using NativeFn = Value (*)(MapObj*, int);
static Value Run(NativeFn f, MapObj* m, int k) { return f(m, k); }
int main() {
    MapObj m;
    m.entries.push_back({Value(1), Value(42)});
    m.entries.push_back({Value(2), Value(7)});
    m.index[5] = 0;
    m.index[9] = 1;
    NativeFn get = [](MapObj* m, int k) {
        auto it = m->index.find(k);
        return it == m->index.end() ? Value() : m->entries[it->second].v;
    };
    Value a = Run(get, &m, 5), b = Run(get, &m, 6);
    Value c = Run([](MapObj* m, int k) {
        auto n = k * 2;
        return n > 10 ? Value(n) : Value(-n);
    }, &m, 9);
    std::printf("%d [%s] %d [%s] %d [%s] %d\n", a.t, a.s.c_str(), b.t, b.s.c_str(), c.t, c.s.c_str(), Run(get, &m, 9).t);
    return 0;
}
