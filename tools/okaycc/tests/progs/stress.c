// exit 0
char *malloc(int n);
void free(char *p);
char *realloc(char *p, int n);
char *calloc(int count, int size);
int printf(char *fmt, ...);
int main() {
    char *a[20];
    int i;
    int sum;
    char *big;
    char *grown;
    char *z;
    char *left;
    char *mid;
    char *right;
    char *again;
    i = 0;
    while (i < 20) {
        a[i] = malloc(16 + (i % 5) * 8);
        if (a[i] == 0) return 1;
        a[i][0] = (char)(i + 1);
        a[i][1] = (char)(100 + i);
        i = i + 1;
    }
    i = 0;
    while (i < 20) {
        if ((i % 2) == 0) free(a[i]);
        i = i + 1;
    }
    i = 0;
    while (i < 20) {
        if ((i % 2) == 0) {
            a[i] = malloc(16);
            if (a[i] == 0) return 2;
            a[i][0] = (char)(50 + i);
        } else if (a[i][0] != (char)(i + 1)) {
            return 3;
        }
        i = i + 1;
    }
    left = malloc(32);
    mid = malloc(32);
    right = malloc(32);
    if (left == 0 || mid == 0 || right == 0) return 4;
    left[0] = 11;
    mid[0] = 22;
    right[0] = 33;
    free(mid);
    again = malloc(32);
    if (again == 0) return 5;
    again[0] = 44;
    if (left[0] != 11 || right[0] != 33) return 6;
    free(left);
    free(again);
    free(right);
    big = malloc(200);
    if (big == 0) return 7;
    i = 0;
    sum = 0;
    while (i < 200) {
        big[i] = (char)(i & 127);
        sum = sum + big[i];
        i = i + 1;
    }
    grown = realloc(big, 400);
    if (grown == 0) return 8;
    if (grown[0] != 0 || grown[199] != (char)(199 & 127)) return 9;
    i = 200;
    while (i < 400) {
        grown[i] = 1;
        i = i + 1;
    }
    z = calloc(8, 4);
    if (z == 0) return 10;
    i = 0;
    while (i < 32) {
        if (z[i] != 0) return 11;
        i = i + 1;
    }
    free(grown);
    free(z);
    free(0);
    printf("%d\n", sum);
    return 0;
}
