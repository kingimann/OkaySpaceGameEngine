// Reference data members ([dcl.ref], [class.base.init] p8): bound by the constructor, used as
// the object they refer to, copied as the same reference. Before round 22: "operand of '.' is not a struct".
#include <cstdio>
struct Src { int base; int Read(int k) { return base + k; } };
class Bits {
public:
    Bits(Src &s, int n) : src_(s), n_(n) {}
    int Get() { return src_.Read(n_); }
    void Bump() { src_.base += 10; }
private:
    Src &src_;
    int n_ = 0;
};
class Early {
public:
    Src &src;
    int &cnt;
    Early(Src &s, int &c) : src(s), cnt(c) {}
    int Get() { ++cnt; return src.Read(1); }
};
int main() {
    Src s{5};
    Bits b(s, 2);
    std::printf("%d\n", b.Get());
    b.Bump();
    std::printf("%d %d\n", b.Get(), s.base);
    int c = 0;
    Early e(s, c);
    int got = e.Get();
    std::printf("%d %d\n", got, c);
    const Src &cs = s;
    struct View { const Src &s; int k; View(const Src &x, int y) : s(x), k(y) {} int sum() const { return s.base + k; } };
    View v(cs, 3);
    View w = v;
    s.base = 40;
    std::printf("%d %d\n", v.sum(), w.sum());
    return 0;
}
