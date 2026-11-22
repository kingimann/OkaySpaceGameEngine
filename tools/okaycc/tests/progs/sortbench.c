// exit 0
int printf(char *fmt, ...);
int cmp(int *a, int *b) { return *a - *b; }
void sort(int *a, int n, int (*f)(int *, int *)) {
    int i;
    int j;
    i = 1;
    while (i < n) {
        int key;
        key = a[i];
        j = i - 1;
        while (j >= 0 && f(&a[j], &key) > 0) {
            a[j + 1] = a[j];
            j = j - 1;
        }
        a[j + 1] = key;
        i = i + 1;
    }
}
int main() {
    int a[12];
    int i;
    int sum;
    a[0] = 9;
    a[1] = 4;
    a[2] = 7;
    a[3] = 1;
    a[4] = 12;
    a[5] = 3;
    a[6] = 8;
    a[7] = 2;
    a[8] = 11;
    a[9] = 6;
    a[10] = 5;
    a[11] = 10;
    sort(a, 12, cmp);
    sum = 0;
    i = 0;
    while (i < 12) {
        if (i > 0 && a[i] < a[i - 1]) return 1;
        sum = sum + a[i];
        i = i + 1;
    }
    printf("%d\n", sum);
    return 0;
}
