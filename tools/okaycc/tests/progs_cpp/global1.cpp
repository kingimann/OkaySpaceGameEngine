// Global objects: constructed first thing in main (in declaration order), destroyed when main
// returns (in reverse order), also on an early return.
extern "C" int printf(const char *fmt, ...);

struct G {
    int v;
    G(int n) : v(n) { printf("G(%d)\n", n); }
    ~G() { printf("~G(%d)\n", v); }
};

struct Pair {
    G first;
    G second;
    Pair() : first(10), second(20) { printf("Pair()\n"); }
    ~Pair() { printf("~Pair()\n"); }
};

G g1(1);
G g2(2);
Pair pr;

int work(int n) {
    G local(n);
    return local.v * 2;
}

int main() {
    printf("main %d %d %d\n", g1.v, g2.v, pr.second.v);
    int r = work(4);
    printf("r=%d\n", r);
    if (r > 5) return r;
    printf("not reached\n");
    return 0;
}
