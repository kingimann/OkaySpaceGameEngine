// exit 1
int main() {
    int x;
    char c;
    unsigned char u;
    int a[2];
    char *p;
    int *q;
    x = 1000;
    c = (char)x;
    u = (unsigned char)x;
    if (!(c < 0)) return 2;
    if (u != 232) return 3;
    a[0] = 1;
    a[1] = 2;
    p = (char *)&a[0];
    p = p + 4;
    q = (int *)p;
    if (*q != 2) return 4;
    return 1;
}
