// exit 0
int main() {
    int a, b;
    a = 1;
    b = 2;
    a += b;
    a++;
    if (((a > 3) ? a : 0) != 4) return 1;
    int s, i;
    s = 0;
    i = 0;
    while (i < 3) {
        s += i;
        i++;
    }
    if (s != 3) return 2;
    a = (b = 5, b + 1);
    if (a != 6) return 3;
    char *p;
    p = "hi\n";
    if (p[2] != 10) return 4;
    char c;
    c = '\n';
    if (c != 10) return 5;
    int arr[3] = {1, 2, 3};
    if (arr[0] + arr[2] != 4) return 6;
#if 1 + 2 * 3 != 7
    return 7;
#endif
    return 0;
}
