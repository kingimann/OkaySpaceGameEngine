// Classes with constructors, destructors, member initializers and member access.
extern "C" int printf(const char *fmt, ...);

class Counter {
public:
    int value;
    int step;
    Counter() : value(0), step(1) { printf("Counter() %d\n", value); }
    Counter(int v) : value(v), step(1) { printf("Counter(int) %d\n", value); }
    Counter(int v, int s) : value(v), step(s) { printf("Counter(int,int) %d %d\n", value, step); }
    ~Counter() { printf("~Counter %d\n", value); }
    void bump() { value = value + step; }
    int get() { return value; }
private:
    int hidden;
};

class Pair {
public:
    Counter a;
    Counter b;
    int tag;
    Pair(int x, int y) : a(x), b(y, 5), tag(7) { printf("Pair() %d\n", tag); }
    ~Pair() { printf("~Pair\n"); }
    int sum() { return a.get() + b.get(); }
};

int helper(int n) {
    Counter local(n);
    if (n > 2) {
        return local.get() * 2;
    }
    local.bump();
    return local.get();
}

int main() {
    Counter c;
    Counter d(10);
    Counter e(20, 3);
    d.bump();
    e.bump();
    printf("d=%d e=%d\n", d.get(), e.get());
    {
        Pair p(1, 2);
        printf("sum=%d\n", p.sum());
        p.b.bump();
        printf("sum=%d\n", p.sum());
    }
    printf("helper=%d\n", helper(1));
    printf("helper=%d\n", helper(5));
    for (int i = 0; i < 3; i = i + 1) {
        Counter loopc(100 + i);
        if (i == 1) continue;
        loopc.bump();
        printf("loop %d\n", loopc.get());
    }
    Counter *hp = new Counter(42, 2);
    hp->bump();
    printf("heap %d\n", hp->get());
    delete hp;
    printf("sizeof %d %d\n", (int)sizeof(Counter), (int)sizeof(Pair));
    return 0;
}
