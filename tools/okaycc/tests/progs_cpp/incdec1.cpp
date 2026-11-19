// ++ and -- on fields, elements, dereferences and globals (not only on local names).
extern "C" int printf(const char *fmt, ...);

struct C { int n; char c; };
int g = 5;
int arr[3];

int main() {
    C s;
    s.n = 10;
    s.c = 65;
    int a = s.n++;
    int b = ++s.n;
    s.c--;
    arr[1] = 7;
    int d = arr[1]++;
    --arr[1];
    int *p = arr;
    p++;
    int e = (*p)++;
    g++;
    int h = --g;
    C *ps = &s;
    ps->n++;
    printf("%d %d %d %d %d %d %d %d %d\n", a, b, s.c, d, arr[1], e, g, h, ps->n);
    return 0;
}
