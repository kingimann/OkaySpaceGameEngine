// exit 0
enum Color { Red = 1, Blue = 2 };
union U { int i; char c; };
int g[3] = {3, 1, 2};
int cmp(int *a, int *b) { return *a - *b; }
void sort(int *a, int n, int (*f)(int *, int *)) {
    int i;
    int j;
    i = 0;
    while (i < n) {
        j = i + 1;
        while (j < n) {
            if (f(a + i, a + j) > 0) {
                int t;
                t = a[i];
                a[i] = a[j];
                a[j] = t;
            }
            j++;
        }
        i++;
    }
}
int main() {
    enum Color c;
    union U u;
    long double x;
    int a[2][2];
    c = Blue;
    switch (c) {
    case Red:
        return 1;
    case Blue:
        break;
    default:
        return 2;
    }
    u.i = 0x41;
    if (u.c != 'A') return 3;
    x = 1.5;
    if (x != 1.5) return 4;
    a[1][1] = 9;
    if (a[1][1] != 9) return 5;
    sort(g, 3, cmp);
    if (g[0] != 1 || g[2] != 3) return 6;
    return 0;
}
