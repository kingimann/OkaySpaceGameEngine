// throw and catch: int, pointer, class by value and by reference, base-class handlers, catch (...),
// rethrow, nested try blocks, an exception crossing several calls, handlers chosen in order.
extern "C" int printf(const char *fmt, ...);

struct Err {
    int code;
    Err(int c) : code(c) {}
};
struct DerivedErr : Err {
    int extra;
    DerivedErr(int c, int e) : Err(c), extra(e) {}
};

int level3(int x) {
    if (x == 1) throw 11;
    if (x == 2) throw Err(22);
    if (x == 3) throw DerivedErr(33, 3);
    if (x == 4) throw "text";
    return x;
}
int level2(int x) { return level3(x) + 100; }
int level1(int x) { return level2(x) + 1000; }

int run(int x) {
    try {
        return level1(x);
    } catch (int v) {
        printf("int %d\n", v);
        return -1;
    } catch (Err &e) {
        printf("Err& %d\n", e.code);
        return -2;
    } catch (const char *s) {
        printf("str %s\n", s);
        return -4;
    }
}

int main() {
    for (int i = 0; i <= 4; i = i + 1) printf("run %d = %d\n", i, run(i));
    try {
        try {
            throw DerivedErr(5, 6);
        } catch (DerivedErr &d) {
            printf("inner %d %d\n", d.code, d.extra);
            throw;
        }
    } catch (Err e) {
        printf("outer by value %d\n", e.code);
    }
    try {
        try {
            throw 7;
        } catch (Err &e) {
            printf("wrong\n");
        }
    } catch (...) {
        printf("catch all\n");
    }
    int total = 0;
    for (int k = 0; k < 3; k = k + 1) {
        try {
            if (k == 1) throw k;
            total = total + 10;
        } catch (int v) {
            total = total + v;
        }
    }
    printf("total %d\n", total);
    return 0;
}
