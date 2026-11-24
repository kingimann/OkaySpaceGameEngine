// Round 17: default arguments, functional casts, T{...} temporaries.
extern "C" int printf(const char *fmt, ...);

typedef unsigned long usize;

struct Options {
    int wrap = 2;
    float weight = 2.0f;
    bool tabs = true;
};

struct Trap {
    int group = 0;
    int restore = 0;
};

struct Pt {
    int x, y;
};

int add(int a, int b = 10, int c = 100) { return a + b + c; }

int score(const Options &o = {}) { return o.wrap * 100 + (int)(o.weight * 10) + o.tabs; }

struct Graph {
    Trap traps[4];
    int n = 0;
    int Push(int group, int restore = -1);
    int Move(int from, const Options &options = {}) const { return from + options.wrap; }
    void Keep(Trap t) { traps[n++] = t; }
};

int Graph::Push(int group, int restore) {
    Keep(Trap{group, restore});
    return n;
}

int sumPt(Pt p) { return p.x * 10 + p.y; }

struct V2 {
    float x, y;
    V2 operator+(const V2 &o) const { return {x + o.x, y + o.y}; }
};

struct Base {
    int b = 4;
};
struct Leaf final : public Base {
    int c = 5;
};

Pt makePt(int a) { return {a, a + 1}; }

int main() {
    printf("add %d %d %d\n", add(1), add(1, 2), add(1, 2, 3));
    Options o;
    o.wrap = 5;
    printf("score %d %d %d\n", score(), score(o), score());
    Graph g;
    g.Push(3);
    g.Push(4, 9);
    printf("graph %d %d %d %d %d %d\n", g.n, g.traps[0].group, g.traps[0].restore, g.traps[1].restore, g.Move(1),
           g.Move(1, o));
    double k = 7.75;
    int i = int(k);
    float f = float(i) / 2;
    usize u = usize(i) * 3;
    char ch = char(65 + i);
    printf("cast %d %d %d %c %d %d\n", i, (int)(f * 10), (int)u, ch, (int)double(3), bool(i));
    Trap t = Trap{};
    Trap t2 = Trap{8};
    printf("brace %d %d %d %d %d\n", t.group, t.restore, t2.group, t2.restore, sumPt(Pt{3, 4}));
    V2 a2 = V2{1.5f, 2.0f};
    V2 b2 = V2{0.5f, 1.0f};
    V2 c2 = a2 + b2;
    Pt mp = makePt(6);
    const V2 d2{c2.x * 2, 1.0f};
    int iv{7};
    int ia[3]{1, 2};
    Trap tr{5};
    printf("direct %d %d %d %d %d %d\n", (int)d2.x, (int)d2.y, iv, ia[1] + ia[2], tr.group, tr.restore);
    Leaf lf;
    printf("ret %d %d %d %d %d %d\n", (int)(c2.x * 10), (int)(c2.y * 10), mp.x, mp.y, lf.b, lf.c);
    return 0;
}
