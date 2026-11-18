int strlen(char *s) {
    int n;
    n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}
int strcmp(char *a, char *b) {
    int i;
    unsigned char ca;
    unsigned char cb;
    i = 0;
    while (1) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
        i = i + 1;
    }
    return 0;
}
