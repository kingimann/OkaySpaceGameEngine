// Round 32: an object of a derived class passed by value as its base class (Scene/Tags.hpp's TagString,
// a std::string, given to VsValue(std::string): engine/src/Scripting/OkayScriptVMInternal.hpp:330).
// The parameter is copied from the base subobject ([dcl.init] p17.6.2) and the derived-to-base
// conversion takes part in overload resolution ([over.best.ics] p6).
#include <cstdio>
#include <string>
static int live = 0;
struct Tag : public std::string { Tag() = default; Tag(const char* s) : std::string(s) {} };
struct Value {
    int k;
    std::string s;
    Value(bool) : k(1) {}
    Value(float) : k(2) {}
    Value(std::string v) : k(3), s(std::move(v)) {}
    Value(const char* v) : k(4), s(v) {}
};
struct Counted { int v; Counted(int x) : v(x) { ++live; } Counted(const Counted& o) : v(o.v) { ++live; } ~Counted() { --live; } };
struct Pad { long a = 1, b = 2; };
struct Both : Pad, Counted { Both(int x) : Counted(x) {} };
struct Kid : Counted { int extra; Kid(int x) : Counted(x), extra(x * 10) {} };
static int take(std::string v) { return (int)v.size(); }
static int count(Counted c) { return c.v + live * 100; }
int main() {
    Tag t("hello");
    Value a{t};
    Value b = Value(t);
    const Tag& r = t;
    Value c{r};
    std::printf("%d %s %d %s %d %s %d\n", a.k, a.s.c_str(), b.k, b.s.c_str(), c.k, c.s.c_str(), take(t));
    {
        Kid k(4);
        Both bo(6);
        int x1 = count(k);
        int x2 = count(Kid(5));
        int x3 = count(bo);
        std::printf("%d %d %d live %d\n", x1, x2, x3, live);
    }
    int n = take(Tag("temp"));
    std::printf("live %d %d\n", live, n);
    return 0;
}
