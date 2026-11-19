// Global objects declared after main, a static local object (built the first time its declaration
// is reached, once), and destruction at program end in reverse order.
extern "C" int printf(const char *fmt, ...);

struct G {
    int v;
    G(int n) : v(n) { printf("G(%d)\n", n); }
    ~G() { printf("~G(%d)\n", v); }
};

G first(1);

int counter() {
    static G calls(50);
    calls.v = calls.v + 1;
    return calls.v;
}

int secondValue();

int main() {
    printf("main %d\n", first.v);
    printf("%d\n", counter());
    printf("%d\n", counter());
    return secondValue();
}

G second(2);

int secondValue() { return second.v; }
