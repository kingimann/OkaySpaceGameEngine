// exit 9
int inc(int x) { return x + 1; }
int dec(int x) { return x - 1; }
int apply(int (*f)(int), int x) { return f(x); }
int main() {
    int (*p)(int);
    int (*tab[2])(int);
    p = inc;
    tab[0] = inc;
    tab[1] = dec;
    if (p(4) != 5) return 1;
    if (tab[0](4) != 5) return 2;
    if (tab[1](4) != 3) return 3;
    if (apply(dec, 4) != 3) return 4;
    return 9;
}
