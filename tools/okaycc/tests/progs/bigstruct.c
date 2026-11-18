// exit 15
struct S {
    int a;
    int b;
    int c;
};
struct H {
    int a[6];
};
struct S make(int x) {
    struct S s;
    s.a = x;
    s.b = x + 1;
    s.c = x + 2;
    return s;
}
int sum(struct S s) { return s.a + s.b + s.c; }
struct H fill(int x) {
    struct H h;
    int i;
    for (i = 0; i < 6; i = i + 1) h.a[i] = x + i;
    return h;
}
int mix(int a, int b, int c, int d, int e, struct S s, int z) {
    return a + e + s.a + s.c + z;
}
int main() {
    struct S s;
    struct H h;
    s = make(4);
    if (s.a != 4 || s.b != 5 || s.c != 6) return 1;
    if (sum(s) != 15) return 2;
    h = fill(3);
    if (h.a[0] != 3 || h.a[5] != 8) return 3;
    if (mix(1, 2, 3, 4, 5, s, 6) != 22) return 4;
    return s.a + s.b + s.c;
}
