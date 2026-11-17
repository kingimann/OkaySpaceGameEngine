void *memcpy(void *dst, void *src, unsigned int n) {
    unsigned char *d;
    unsigned char *s;
    unsigned int i;
    d = dst;
    s = src;
    for (i = 0; i < n; i = i + 1) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int value, unsigned int n) {
    unsigned char *d;
    unsigned int i;
    d = dst;
    for (i = 0; i < n; i = i + 1) d[i] = value;
    return dst;
}

unsigned int strlen(char *s) {
    unsigned int n;
    n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}

int strcmp(char *a, char *b) {
    unsigned int i;
    i = 0;
    while (a[i] != 0 && b[i] != 0) {
        if (a[i] != b[i]) break;
        i = i + 1;
    }
    return a[i] - b[i];
}
