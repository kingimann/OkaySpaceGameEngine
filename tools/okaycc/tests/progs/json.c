// exit 0
int printf(char *fmt, ...);
int sum(char *s) {
    int i;
    int n;
    int v;
    i = 0;
    n = 0;
    while (s[i] != 0) {
        if (s[i] >= '0' && s[i] <= '9') {
            v = 0;
            while (s[i] >= '0' && s[i] <= '9') {
                v = v * 10 + (s[i] - '0');
                i++;
            }
            n = n + v;
        } else {
            i++;
        }
    }
    return n;
}
int main() {
    int n;
    n = sum("{\"a\":1,\"b\":20,\"c\":3}");
    printf("%d\n", n);
    if (n != 24) return 1;
    return 0;
}
