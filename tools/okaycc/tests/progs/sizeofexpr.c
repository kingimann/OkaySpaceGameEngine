// exit 8
int main() {
    int a[4];
    int *p;
    p = a;
    if (sizeof(a + 0) != 8) return 1;
    if (sizeof(p[0] + 1) != 4) return 2;
    if (sizeof(*p) != 4) return 3;
    if (sizeof(p) != 8) return 4;
    return 8;
}
