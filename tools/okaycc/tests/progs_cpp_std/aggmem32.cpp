// Round 32 (silent wrong code): an aggregate whose members construct, given a braced list. Each member is
// copy-initialized in place ([dcl.init.aggr] p4) and the rest take their default member initializers (p5).
// Before: T x = {..} left n at 0 and crashed; T x{..}, T{..} and v.push_back({..}) built Tracked by
// assignment to a member never built, so live ended below 0.
#include <cstdio>
#include <string>
#include <vector>
static int live = 0;
struct Tracked { int v; Tracked(int x) : v(x) { ++live; } Tracked(const Tracked& o) : v(o.v) { ++live; } ~Tracked() { --live; } };
struct Named { std::string name; Tracked t; int n = 9; };
struct Outer { Named a; int k = 4; std::string tail = "end"; };
static Named make(int i) { return {"made", i}; }
int main() {
    {
        Named a = {"first", Tracked(1)};
        Named b{std::string("second") + "!", 2, 5};
        Named c = Named{"third", 3};
        std::printf("[%s] %d %d [%s] %d %d [%s] %d live %d\n", a.name.c_str(), a.t.v, a.n, b.name.c_str(), b.t.v, b.n,
                    c.name.c_str(), c.t.v, live);
    }
    std::printf("live %d\n", live);
    {
        Outer o{{"in", Tracked(1)}, 3};
        Outer p = {{"in2", 2}};
        std::printf("%s %d %d %d %s | %s %d %d %s live %d\n", o.a.name.c_str(), o.a.t.v, o.a.n, o.k, o.tail.c_str(),
                    p.a.name.c_str(), p.a.t.v, p.k, p.tail.c_str(), live);
        std::vector<Named> v;
        v.push_back({"pushed", 5});
        v.push_back(make(6));
        v.emplace_back(Named{"emp", 7, 1});
        for (auto& x : v) std::printf("%s %d %d\n", x.name.c_str(), x.t.v, x.n);
        std::printf("live %d\n", live);
    }
    std::printf("live %d\n", live);
    return 0;
}
