// exit 4
int main() {
    int a[4];
    struct P { int x; int y; };
    struct P p;
    char *s;
    if (sizeof(int) != 8) return 1;
    if (sizeof(a) != 32) return 2;
    if (sizeof(p) != 16) return 3;
    if (sizeof(p.y) != 8) return 4;
    if (sizeof(*s) != 1) return 5;
    if (sizeof(s) != 8) return 6;
    if (sizeof("hi") != 3) return 7;
    if (sizeof(a) / sizeof(a[0]) != 4) return 8;
    return 4;
}
