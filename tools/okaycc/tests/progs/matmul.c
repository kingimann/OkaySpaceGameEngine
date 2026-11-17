// exit 0
int printf(char *fmt, ...);
int main() {
    double a[2][2];
    double b[2][2];
    double r[2][2];
    int i;
    int j;
    int k;
    a[0][0] = 1;
    a[0][1] = 2;
    a[1][0] = 3;
    a[1][1] = 4;
    b[0][0] = 5;
    b[0][1] = 6;
    b[1][0] = 7;
    b[1][1] = 8;
    i = 0;
    while (i < 2) {
        j = 0;
        while (j < 2) {
            double s;
            s = 0;
            k = 0;
            while (k < 2) {
                s = s + a[i][k] * b[k][j];
                k++;
            }
            r[i][j] = s;
            j++;
        }
        i++;
    }
    printf("%f %f\n", r[0][0], r[0][1]);
    printf("%f %f\n", r[1][0], r[1][1]);
    if (r[0][0] != 19) return 1;
    if (r[1][1] != 50) return 2;
    return 0;
}
