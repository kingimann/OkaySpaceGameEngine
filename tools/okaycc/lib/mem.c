char *memcpy(char *dst, char *src, int n) {
    int i;
    i = 0;
    while (i < n) {
        dst[i] = src[i];
        i = i + 1;
    }
    return dst;
}
char *memset(char *dst, int c, int n) {
    int i;
    i = 0;
    while (i < n) {
        dst[i] = (char)c;
        i = i + 1;
    }
    return dst;
}
/* ISO C 7.24.4.1: compare n bytes as unsigned char. */
int memcmp(char *a, char *b, unsigned long n) {
    unsigned long i;
    unsigned char ca;
    unsigned char cb;
    i = 0;
    while (i < n) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        i = i + 1;
    }
    return 0;
}
/* ISO C 7.24.2.2: copy n bytes; the two areas may overlap. */
char *memmove(char *dst, char *src, unsigned long n) {
    unsigned long i;
    if (dst < src) {
        i = 0;
        while (i < n) {
            dst[i] = src[i];
            i = i + 1;
        }
    } else {
        i = n;
        while (i > 0) {
            i = i - 1;
            dst[i] = src[i];
        }
    }
    return dst;
}
/* ISO C 7.24.5.1: the first of n bytes equal to (unsigned char)c, or null. */
char *memchr(char *s, int c, unsigned long n) {
    unsigned long i;
    i = 0;
    while (i < n) {
        if ((unsigned char)s[i] == (unsigned char)c) return s + i;
        i = i + 1;
    }
    return 0;
}
