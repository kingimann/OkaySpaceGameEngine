// Destructors run while an exception unwinds: locals of every frame in reverse order, members
// already built when a constructor throws, hidden temporaries, array elements; handlers that leave
// by return, and the object thrown is destroyed after its handler.
extern "C" int printf(const char *fmt, ...);

struct Noisy {
    int id;
    Noisy(int n) : id(n) { printf("  make %d\n", id); }
    Noisy(const Noisy &o) : id(o.id + 100) { printf("  copy %d\n", id); }
    ~Noisy() { printf("  free %d\n", id); }
};

struct Cell {
    int id;
    Cell() : id(7) { printf("  Cell()\n"); }
    ~Cell() { printf("  ~Cell\n"); }
};

struct Pair {
    Noisy a;
    Noisy b;
    Pair(int x, int fail) : a(x), b(x + 1) {
        printf("  Pair body\n");
        if (fail) throw 99;
    }
    ~Pair() { printf("  ~Pair\n"); }
};

struct Thrown {
    int v;
    Thrown(int x) : v(x) { printf("  Thrown(%d)\n", v); }
    Thrown(const Thrown &o) : v(o.v) { printf("  Thrown copy %d\n", v); }
    ~Thrown() { printf("  ~Thrown(%d)\n", v); }
};

int peekThrow(const Noisy &n) {
    printf("  in call %d\n", n.id);
    throw n.id;
    return 0;
}

void deep(int level) {
    Noisy local(level);
    if (level == 3) throw Thrown(7);
    deep(level + 1);
    printf("  not reached %d\n", level);
}

int main() {
    printf("unwind\n");
    try {
        Noisy outer(1);
        deep(2);
    } catch (Thrown &t) {
        printf("caught %d\n", t.v);
    }
    printf("ctor throws\n");
    try {
        Pair p(10, 1);
        printf("not reached\n");
    } catch (int c) {
        printf("caught %d\n", c);
    }
    printf("no throw\n");
    try {
        Pair p(20, 0);
        printf("body\n");
    } catch (int c) {
        printf("wrong\n");
    }
    printf("array\n");
    try {
        Cell arr[2];
        throw 5;
    } catch (int c) {
        printf("caught %d\n", c);
    }
    printf("handler return\n");
    int r = 0;
    for (int i = 0; i < 2; i = i + 1) {
        try {
            Noisy n(40 + i);
            if (i == 0) throw i;
            r = r + 1;
        } catch (int v) {
            Noisy h(50);
            r = r + 10;
        }
    }
    printf("r=%d\n", r);
    printf("temporary\n");
    try {
        printf("%d\n", peekThrow(Noisy(60)));
    } catch (int c) {
        printf("caught temp\n");
    }
    return 0;
}
