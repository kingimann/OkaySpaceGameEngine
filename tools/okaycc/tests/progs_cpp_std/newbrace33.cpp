// Round 33: a chain of 100 else-ifs is not "nesting too deep" (engine/src/Components/ActionList.cpp
// has one of about a hundred); and new T{..} ([expr.new] p18): a class with constructors, an
// aggregate with members that construct, a plain aggregate, a scalar (Net/Matchmaker.cpp).
#include <cstdio>
#include <string>
#include <vector>
#define E(n) else if (op == n) r = n * 3 + (n % 7);
static int Pick(int op) {
    int r = -1;
    if (op == 0) r = 100;
    E(1) E(2) E(3) E(4) E(5) E(6) E(7) E(8) E(9) E(10)
    E(11) E(12) E(13) E(14) E(15) E(16) E(17) E(18) E(19) E(20)
    E(21) E(22) E(23) E(24) E(25) E(26) E(27) E(28) E(29) E(30)
    E(31) E(32) E(33) E(34) E(35) E(36) E(37) E(38) E(39) E(40)
    E(41) E(42) E(43) E(44) E(45) E(46) E(47) E(48) E(49) E(50)
    E(51) E(52) E(53) E(54) E(55) E(56) E(57) E(58) E(59) E(60)
    E(61) E(62) E(63) E(64) E(65) E(66) E(67) E(68) E(69) E(70)
    E(71) E(72) E(73) E(74) E(75) E(76) E(77) E(78) E(79) E(80)
    E(81) E(82) E(83) E(84) E(85) E(86) E(87) E(88) E(89) E(90)
    E(91) E(92) E(93) E(94) E(95) E(96) E(97) E(98) E(99) E(100)
    else r = -2;
    return r;
}
struct Req {
    std::string game;
    int n;
};
struct RunArgs {
    int* m;
    int g;
    Req req;
    bool poll;
    double wait;
};
struct Plain {
    int a, b;
};
struct Ctor {
    int v;
    Ctor(int x, int y) : v(x * 10 + y) {}
};
int main() {
    int s = 0;
    for (int op = 0; op <= 101; op += 3) s += Pick(op);
    int m = 5;
    Req req{"x", 2};
    RunArgs* a = new RunArgs{&m, 3, req, true, 1.5};
    Plain* p = new Plain{4, 9};
    Ctor* c = new Ctor{7, 2};
    int* i = new int{42};
    double* z = new double{};
    std::vector<int>* v = new std::vector<int>{1, 2, 3};
    std::printf("%d %d %d %s %d %.1f\n", s, *a->m, a->g, a->req.game.c_str(), a->poll, a->wait);
    std::printf("%d %d %d %d %.1f %d\n", p->a, p->b, c->v, *i, *z, (int)v->size());
    delete a;
    delete p;
    delete c;
    delete i;
    delete z;
    delete v;
    return 0;
}
