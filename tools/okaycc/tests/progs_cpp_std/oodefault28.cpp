// Round 28: defaulted copy and move members, inside and outside the class, with and without
// noexcept ([dcl.fct.def.default], [class.copy.ctor] p14, [class.copy.assign] p12): a defaulted
// copy or move constructor or assignment copies or moves the base and each member in turn, like
// the implicit one. (Round 27 gave them an empty body or none: the new object lost its members.)
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Base {
    std::string tag;
};

// Declared in the class, defaulted outside it.
struct Out : Base {
    std::string s;
    std::vector<int> v;
    std::unique_ptr<int> p;
    int n = 0;
    float arr[3] = {0, 0, 0};
    Out();
    ~Out();
    Out(Out&&) noexcept;
    Out& operator=(Out&&) noexcept;
};
Out::Out() = default;
Out::~Out() = default;
Out::Out(Out&&) noexcept = default;
Out& Out::operator=(Out&&) noexcept = default;

// Copy members defaulted outside, without noexcept.
struct Copy {
    std::string s;
    std::vector<int> v;
    int n = 0;
    Copy() = default;
    Copy(const Copy& o);
    Copy& operator=(const Copy& o);
};
Copy::Copy(const Copy&) = default;
Copy& Copy::operator=(const Copy&) = default;

// Defaulted inside the class.
struct In {
    std::string s;
    std::vector<int> v;
    std::unique_ptr<int> p;
    int n = 0;
    In() = default;
    In(In&&) noexcept = default;
    In& operator=(In&&) noexcept(true) = default;
};

// A constructor defined outside with noexcept before its body.
struct Body {
    int a;
    Body() noexcept;
};
Body::Body() noexcept : a(7) {}

static void show(const char* what, const Out& o) {
    std::printf("%s: %s %s %d %d %d %g %g %g\n", what, o.tag.c_str(), o.s.c_str(), (int)o.v.size(), o.p ? *o.p : -1,
                o.n, o.arr[0], o.arr[1], o.arr[2]);
}

int main() {
    Out a;
    a.tag = "base";
    a.s = "hello";
    a.v = {1, 2, 3};
    a.p.reset(new int(5));
    a.n = 9;
    a.arr[0] = 1.5f;
    a.arr[2] = 2.5f;
    Out b(std::move(a));
    show("moved", b);
    std::printf("source keeps p: %d\n", a.p ? 1 : 0);
    Out c;
    c = std::move(b);
    show("assigned", c);
    std::printf("source keeps p: %d\n", b.p ? 1 : 0);

    Copy x;
    x.s = "copy";
    x.v = {4, 5};
    x.n = 3;
    Copy y(x);
    Copy z;
    z = y;
    y.s += "!";
    std::printf("%s %d %d / %s %d %d / %s\n", z.s.c_str(), (int)z.v.size(), z.n, y.s.c_str(), (int)y.v.size(), y.n,
                x.s.c_str());

    In i;
    i.s = "inside";
    i.v = {7};
    i.p.reset(new int(11));
    i.n = 4;
    In j(std::move(i));
    In k;
    k = std::move(j);
    std::printf("%s %d %d %d %d\n", k.s.c_str(), (int)k.v.size(), k.p ? *k.p : -1, k.n, j.p ? 1 : 0);

    Body bd;
    std::printf("%d\n", bd.a);
    return 0;
}
