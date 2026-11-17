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

char *strcpy(char *dst, char *src) {
    unsigned int i;
    i = 0;
    while (src[i] != 0) {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = 0;
    return dst;
}

char *strcat(char *dst, char *src) {
    unsigned int n;
    n = strlen(dst);
    strcpy(dst + n, src);
    return dst;
}

int atoi(char *s) {
    int n;
    int sign;
    unsigned int i;
    n = 0;
    sign = 1;
    i = 0;
    if (s[0] == '-') {
        sign = -1;
        i = 1;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        n = n * 10 + (s[i] - '0');
        i = i + 1;
    }
    return n * sign;
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
