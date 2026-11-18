// exit 0
int printf(char *fmt, ...);
int at(int g[4][4], int r, int c) {
    if (r < 0 || r >= 4 || c < 0 || c >= 4) return 0;
    return g[r][c];
}
int main() {
    int a[4][4];
    int b[4][4];
    int step;
    int r;
    int c;
    int n;
    r = 0;
    while (r < 4) {
        c = 0;
        while (c < 4) {
            a[r][c] = 0;
            c++;
        }
        r++;
    }
    a[1][0] = 1;
    a[1][1] = 1;
    a[1][2] = 1;
    step = 0;
    while (step < 1) {
        r = 0;
        while (r < 4) {
            c = 0;
            while (c < 4) {
                n = at(a, r - 1, c - 1) + at(a, r - 1, c) + at(a, r - 1, c + 1) + at(a, r, c - 1) +
                    at(a, r, c + 1) + at(a, r + 1, c - 1) + at(a, r + 1, c) + at(a, r + 1, c + 1);
                if (a[r][c]) b[r][c] = n == 2 || n == 3;
                else b[r][c] = n == 3;
                c++;
            }
            r++;
        }
        r = 0;
        while (r < 4) {
            c = 0;
            while (c < 4) {
                a[r][c] = b[r][c];
                c++;
            }
            r++;
        }
        step++;
    }
    r = 0;
    while (r < 4) {
        c = 0;
        while (c < 4) {
            printf("%d", a[r][c]);
            c++;
        }
        printf("\n");
        r++;
    }
    return 0;
}
