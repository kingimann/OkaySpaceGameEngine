int write(int fd, char *buf, int n);
char *strcpy(char *dst, char *src) {
    int i;
    i = 0;
    while (src[i] != 0) {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = 0;
    return dst;
}
char *strcat(char *dst, char *src) {
    int i;
    int j;
    i = 0;
    while (dst[i] != 0) i = i + 1;
    j = 0;
    while (src[j] != 0) {
        dst[i] = src[j];
        i = i + 1;
        j = j + 1;
    }
    dst[i] = 0;
    return dst;
}
int atoi(char *s) {
    int i;
    int n;
    int neg;
    i = 0;
    n = 0;
    neg = 0;
    while (s[i] == ' ') i = i + 1;
    if (s[i] == '-') {
        neg = 1;
        i = i + 1;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        n = n * 10 + (s[i] - '0');
        i = i + 1;
    }
    if (neg) n = -n;
    return n;
}
int puts(char *s) {
    int n;
    int i;
    n = 0;
    i = 0;
    while (s[i] != 0) {
        char b;
        b = s[i];
        if (write(1, &b, 1) != 1) return -1;
        n = n + 1;
        i = i + 1;
    }
    if (write(1, "\n", 1) != 1) return -1;
    return n + 1;
}
