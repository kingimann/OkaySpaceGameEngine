// exit 12
struct S {
    char a;
    int b;
    short c;
};
struct T {
    char c;
    long x;
};
int main() {
    struct S s;
    struct T t;
    short h;
    long g;
    if (sizeof(char) != 1) return 1;
    if (sizeof(short) != 2) return 2;
    if (sizeof(int) != 4) return 3;
    if (sizeof(long) != 8) return 4;
    if (sizeof(int *) != 8) return 5;
    if (sizeof(s) != 12) return 6;
    if (sizeof(t) != 16) return 7;
    if (sizeof(s.a) != 1 || sizeof(s.b) != 4 || sizeof(s.c) != 2) return 8;
    s.a = 11;
    s.b = 22;
    s.c = 33;
    if (s.a != 11 || s.b != 22 || s.c != 33) return 9;
    t.c = 7;
    t.x = 9;
    if (t.c + t.x != 16) return 10;
    h = 300;
    g = h;
    if (g != 300) return 11;
    return sizeof(s);
}
