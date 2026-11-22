// exit 0
int printf(char *fmt, ...);
int mandel(double cx, double cy) {
    double x;
    double y;
    int i;
    x = 0;
    y = 0;
    i = 0;
    while (i < 12) {
        double xx;
        double yy;
        xx = x * x - y * y + cx;
        yy = 2 * x * y + cy;
        if (xx * xx + yy * yy > 4) return i;
        x = xx;
        y = yy;
        i = i + 1;
    }
    return 12;
}
int main() {
    int row;
    int col;
    row = 0;
    while (row < 6) {
        col = 0;
        while (col < 16) {
            double cx;
            double cy;
            int n;
            cx = -1.5 + col * 0.15;
            cy = -1.0 + row * 0.3;
            n = mandel(cx, cy);
            if (n > 8) printf("*");
            else if (n > 4) printf("+");
            else printf(".");
            col = col + 1;
        }
        printf("\n");
        row = row + 1;
    }
    return 0;
}
