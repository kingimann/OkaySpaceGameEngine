// Temporaries in operands that may not run: a destructor must run only for an object that was
// constructed. Covers &&, ||, ?: and a while condition (one temporary per iteration).
extern "C" int printf(const char *fmt, ...);

struct T {
    int v;
    T(int x) : v(x) { printf("  T(%d)\n", v); }
    ~T() { printf("  ~T(%d)\n", v); }
};

int peek(const T &t) { return t.v; }
int twice(T t) { return t.v * 2; }

int main() {
    int zero = 0;
    int one = 1;
    printf("and skipped\n");
    if (zero && peek(T(5))) printf("no\n");
    printf("and run\n");
    if (one && peek(T(6))) printf("yes\n");
    printf("or skipped\n");
    if (one || peek(T(7))) printf("yes\n");
    printf("or run\n");
    if (zero || peek(T(8))) printf("yes\n");
    printf("cond\n");
    int a = one ? peek(T(9)) : peek(T(10));
    int b = zero ? peek(T(11)) : 3;
    printf("%d %d\n", a, b);
    printf("while\n");
    int n = 0;
    while (n < 3 && peek(T(n)) < 10) {
        printf("body %d\n", n);
        n = n + 1;
    }
    printf("end\n");
    return 0;
}
