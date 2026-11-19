// try blocks left by return, break and continue; a handler that returns or throws a new exception;
// exceptions thrown from a destructor-owning loop; catch by pointer; catch (...) with rethrow out
// of a function; the same function called again after an exception.
extern "C" int printf(const char *fmt, ...);

struct Guard {
    int id;
    Guard(int n) : id(n) { printf("  guard %d\n", id); }
    ~Guard() { printf("  unguard %d\n", id); }
};

int viaReturn(int x) {
    Guard g(1);
    try {
        Guard inner(2);
        if (x > 0) return x * 2;
        throw x;
    } catch (int v) {
        printf("  caught %d\n", v);
        return -v;
    }
}

int loop() {
    int sum = 0;
    for (int i = 0; i < 6; i = i + 1) {
        try {
            Guard g(10 + i);
            if (i == 1) continue;
            if (i == 4) break;
            if (i == 2) throw i;
            sum = sum + i;
        } catch (int v) {
            sum = sum + 100;
        }
    }
    return sum;
}

void inner() {
    Guard g(20);
    throw 5;
}

void middle() {
    try {
        inner();
    } catch (...) {
        printf("  middle sees it\n");
        throw;
    }
}

struct Info { int a; };

int main() {
    printf("%d\n", viaReturn(3));
    printf("%d\n", viaReturn(0));
    printf("%d\n", loop());
    try {
        middle();
    } catch (int v) {
        printf("main got %d\n", v);
    }
    Info stat;
    stat.a = 9;
    try {
        throw &stat;
    } catch (Info *p) {
        printf("pointer %d\n", p->a);
    }
    try {
        try {
            throw 1;
        } catch (int v) {
            throw v + 10;
        }
    } catch (int v) {
        printf("second %d\n", v);
    }
    for (int k = 0; k < 2; k = k + 1) {
        try {
            if (k == 0) throw k;
            printf("no throw %d\n", k);
        } catch (int v) {
            printf("again %d\n", v);
        }
    }
    return 0;
}
