// Round 33: a reference to const (or an rvalue reference) bound to a temporary keeps it alive to
// the end of the reference's block, where it is destroyed ([class.temporary] p6-p7);
// engine/src/Components/ActionList.cpp's const std::string &k = Str(c, i).
#include <cstdio>
#include <string>
#include <utility>
static int live = 0, made = 0;
struct Tracked {
    int v;
    explicit Tracked(int x) : v(x) {
        ++live;
        ++made;
    }
    Tracked(const Tracked& o) : v(o.v) {
        ++live;
        ++made;
    }
    ~Tracked() { --live; }
};
static std::string Str(int i) { return std::string(i, 'k'); }
static Tracked Make(int v) { return Tracked(v); }
static int Twice(int v) { return v * 2; }
// A reference local between two objects with destructors, then a throw: the unwinding records of
// the objects stay paired (the reference has none; a pop for it dropped the record before it).
static int Throws(int k) {
    Tracked first(1);
    const Tracked& r = first;
    Tracked second(2);
    if (k > 0) throw k + r.v + second.v;
    return 0;
}
int main() {
    const std::string& a = Str(3);
    for (int i = 0; i < 3; ++i) {
        const std::string& k = Str(i);
        if (!k.empty()) std::printf("%s ", k.c_str());
    }
    std::printf("%s\n", a.c_str());
    {
        const Tracked& t = Make(7);
        Tracked&& u = Tracked(9);
        const int& n = Twice(21);
        const auto& s = Str(2) + "!";
        std::printf("%d %d %d %s live %d\n", t.v, u.v, n, s.c_str(), live);
    }
    Tracked keep(1);
    const Tracked& alias = keep;  // an lvalue: no temporary, nothing destroyed at its end
    Tracked&& moved = std::move(keep);
    std::printf("%d %d live %d made>0 %d\n", alias.v, moved.v, live, made > 0);
    try {
        Throws(4);
    } catch (int v) {
        std::printf("caught %d live %d\n", v, live);
    }
    return 0;
}
