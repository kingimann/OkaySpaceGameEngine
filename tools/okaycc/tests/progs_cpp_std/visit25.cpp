// std::visit over one variant ([variant.visit]): a visitor class with overloaded operator(), a
// generic lambda-free visitor holding state, a void visitor, and a visitor that visits again
// (recursion through *this, as the engine's script interpreter does).
#include <cstdio>
#include <memory>
#include <string>
#include <variant>
#include <vector>
struct Cell;
using CellRef = std::shared_ptr<Cell>;
using Value = std::variant<int, double, std::string, CellRef>;
struct Cell {
    Value v;
};
struct Show {
    std::string operator()(int i) const { return "int " + std::to_string(i); }
    std::string operator()(double d) const { return "double " + std::to_string((int)(d * 10)); }
    std::string operator()(const std::string &s) const { return "text " + s; }
    std::string operator()(const CellRef &c) const { return c ? "cell(" + std::visit(*this, c->v) + ")" : "null"; }
};
struct Count {
    int *n;
    void operator()(int) const { *n += 1; }
    void operator()(double) const { *n += 10; }
    void operator()(const std::string &) const { *n += 100; }
    void operator()(const CellRef &) const { *n += 1000; }
};
int main() {
    std::vector<Value> vs;
    vs.push_back(Value(3));
    vs.push_back(Value(2.5));
    vs.push_back(Value(std::string("hi")));
    auto c = std::make_shared<Cell>();
    c->v = std::string("inner");
    vs.push_back(Value(c));
    vs.push_back(Value(CellRef()));
    int n = 0;
    for (const Value &v : vs) {
        std::printf("%s\n", std::visit(Show{}, v).c_str());
        std::visit(Count{&n}, v);
    }
    std::printf("%d\n", n);
}
