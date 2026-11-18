// Temporaries in a for or do condition die once per test, and (c ? a : b) is an lvalue for two
// objects of one scalar type.
extern "C" int printf(const char *fmt, ...);

struct T {
    int v;
    T(int x) : v(x) { printf("  T(%d)\n", v); }
    ~T() { printf("  ~T(%d)\n", v); }
};

int peek(const T &t) { return t.v; }

int main() {
    printf("for\n");
    for (int i = 0; i < 4 && peek(T(i)) < 2; i = i + 1) printf("body %d\n", i);
    printf("do\n");
    int n = 0;
    do {
        n = n + 1;
        printf("n=%d\n", n);
    } while (peek(T(n)) < 2);
    printf("lvalue\n");
    int a = 1;
    int b = 2;
    int pick = 0;
    (pick ? a : b) = 20;
    (pick ? a : b) += 5;
    (pick ? b : a)++;
    printf("%d %d\n", a, b);
    return 0;
}
