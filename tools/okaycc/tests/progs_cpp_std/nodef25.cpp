// An aggregate with a member that has no default constructor (std::type_index): it is made by a
// braced list only ([dcl.init.aggr]); round 24 gave the class a default constructor that called
// type_index() ("wrong number of arguments").
#include <cstdio>
#include <typeindex>
#include <typeinfo>
#include <vector>
struct A {};
struct B {};
struct Requirement {
    std::type_index type;
    int (*has)(int);
};
static int twice(int x) { return 2 * x; }
int main() {
    std::vector<Requirement> rs;
    rs.push_back(Requirement{typeid(A), twice});
    rs.push_back({typeid(B), nullptr});
    std::printf("%d %d %d\n", (int)(rs[0].type == std::type_index(typeid(A))), rs[0].has(21),
                (int)(rs[1].type == std::type_index(typeid(A))));
}
