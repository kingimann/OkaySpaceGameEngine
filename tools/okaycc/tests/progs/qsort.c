// exit 0
void swap(int *a, int *b) {
    int t;
    t = *a;
    *a = *b;
    *b = t;
}
int part(int *a, int lo, int hi) {
    int pivot;
    int i;
    int j;
    pivot = a[hi];
    i = lo;
    j = lo;
    while (j < hi) {
        if (a[j] < pivot) {
            swap(&a[i], &a[j]);
            i = i + 1;
        }
        j = j + 1;
    }
    swap(&a[i], &a[hi]);
    return i;
}
void sort(int *a, int lo, int hi) {
    int p;
    if (lo < hi) {
        p = part(a, lo, hi);
        sort(a, lo, p - 1);
        sort(a, p + 1, hi);
    }
}
int main() {
    int a[8];
    int i;
    int s;
    a[0] = 4;
    a[1] = 1;
    a[2] = 7;
    a[3] = 3;
    a[4] = 8;
    a[5] = 2;
    a[6] = 6;
    a[7] = 5;
    sort(a, 0, 7);
    s = 0;
    i = 0;
    while (i < 8) {
        if (a[i] != i + 1) return 1;
        s = s + a[i];
        i = i + 1;
    }
    return s - 36;
}
