// Copy constructors and copy assignment, run at the places C++ runs them.
extern "C" int printf(const char *fmt, ...);

struct Noisy {
    int id;
    Noisy(int n) : id(n) { printf("  make %d\n", id); }
    Noisy(const Noisy &o) : id(o.id + 100) { printf("  copy of %d -> %d\n", o.id, id); }
    Noisy &operator=(const Noisy &o) {
        printf("  assign %d <- %d\n", id, o.id);
        id = o.id + 1000;
        return *this;
    }
    ~Noisy() { printf("  free %d\n", id); }
};

struct Pair {
    int a;
    int b;
    Pair(int x, int y) : a(x), b(y) {}
    Pair(const Pair &o) : a(o.b), b(o.a) {}
};

Noisy build(int n) {
    Noisy local(n);
    return local;
}

int main() {
    printf("one\n");
    Noisy a(1);
    printf("two\n");
    Noisy b(a);
    printf("three\n");
    Noisy c = a;
    printf("four\n");
    Noisy d(4);
    d = a;
    printf("five\n");
    Noisy e = Noisy(5);
    printf("six\n");
    Pair p(3, 4);
    Pair q = p;
    Pair r(q);
    printf("%d %d %d %d\n", q.a, q.b, r.a, r.b);
    printf("end\n");
    return 0;
}
