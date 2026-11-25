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

/* memchr (ISO/IEC 9899 7.24.5.1): the first of the n bytes at s equal to c (as unsigned char), or null. */
void *memchr(void *s, int c, unsigned long long n) {
    unsigned char *p;
    unsigned long long i;
    p = s;
    for (i = 0; i < n; i = i + 1) {
        if (p[i] == (unsigned char)c) return p + i;
    }
    return 0;
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

void *memmove(void *dst, void *src, unsigned int n) {
    unsigned char *d;
    unsigned char *s;
    unsigned int i;
    d = dst;
    s = src;
    if (d <= s || d >= s + n) {
        for (i = 0; i < n; i = i + 1) d[i] = s[i];
    } else {
        i = n;
        while (i > 0) {
            i = i - 1;
            d[i] = s[i];
        }
    }
    return dst;
}

int memcmp(void *a, void *b, unsigned int n) {
    unsigned char *x;
    unsigned char *y;
    unsigned int i;
    x = a;
    y = b;
    for (i = 0; i < n; i = i + 1) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

int strncmp(char *a, char *b, unsigned int n) {
    unsigned int i;
    for (i = 0; i < n; i = i + 1) {
        unsigned char x;
        unsigned char y;
        x = a[i];
        y = b[i];
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
    }
    return 0;
}

char *strchr(char *s, int c) {
    unsigned int i;
    i = 0;
    for (;;) {
        if (s[i] == (char)c) return s + i;
        if (s[i] == 0) return 0;
        i = i + 1;
    }
}

/* Number conversions, written from the numeric string rules of ISO/IEC 9899 7.22.1: leading white space, an optional
 * sign, then digits (a base prefix 0x for base 16, and 0 or 0x when the base is 0); a decimal floating number is
 * digits, an optional fraction and an optional exponent. The floating value is built from the digits and scaled by
 * powers of ten, so it is not always the nearest double (the last digit or two can differ). */
static int is_space_char(int c) { return c == 32 || (c >= 9 && c <= 13); }

static int digit_of(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

unsigned long strtoul(char *s, char **end, int base) {
    unsigned long value;
    int neg;
    int d;
    char *p;
    char *start;
    value = 0;
    neg = 0;
    p = s;
    while (is_space_char(p[0])) p = p + 1;
    if (p[0] == '-') {
        neg = 1;
        p = p + 1;
    } else if (p[0] == '+') {
        p = p + 1;
    }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p = p + 2;
        base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    start = p;
    while ((d = digit_of(p[0])) < base) {
        value = value * base + d;
        p = p + 1;
    }
    if (p == start && end != 0) {
        *end = s;
        return 0;
    }
    if (end != 0) *end = p;
    return neg ? 0 - value : value;
}

long strtol(char *s, char **end, int base) {
    char *p;
    int neg;
    unsigned long v;
    p = s;
    while (is_space_char(p[0])) p = p + 1;
    neg = p[0] == '-';
    v = strtoul(s, end, base);
    return neg ? 0 - (long)(0 - v) : (long)v;
}

double strtod(char *s, char **end) {
    double value;
    double scale;
    int neg;
    int exp10;
    int any;
    char *p;
    value = 0.0;
    neg = 0;
    exp10 = 0;
    any = 0;
    p = s;
    while (is_space_char(p[0])) p = p + 1;
    if (p[0] == '-') {
        neg = 1;
        p = p + 1;
    } else if (p[0] == '+') {
        p = p + 1;
    }
    while (p[0] >= '0' && p[0] <= '9') {
        value = value * 10.0 + (p[0] - '0');
        any = 1;
        p = p + 1;
    }
    if (p[0] == '.') {
        p = p + 1;
        while (p[0] >= '0' && p[0] <= '9') {
            value = value * 10.0 + (p[0] - '0');
            exp10 = exp10 - 1;
            any = 1;
            p = p + 1;
        }
    }
    if (!any) {
        if (end != 0) *end = s;
        return 0.0;
    }
    if (p[0] == 'e' || p[0] == 'E') {
        char *q;
        int eneg;
        int e;
        q = p + 1;
        eneg = 0;
        e = 0;
        if (q[0] == '-') {
            eneg = 1;
            q = q + 1;
        } else if (q[0] == '+') {
            q = q + 1;
        }
        if (q[0] >= '0' && q[0] <= '9') {
            while (q[0] >= '0' && q[0] <= '9') {
                e = e * 10 + (q[0] - '0');
                q = q + 1;
            }
            exp10 = exp10 + (eneg ? 0 - e : e);
            p = q;
        }
    }
    scale = 1.0;
    if (exp10 < 0) {
        while (exp10 < 0) {
            scale = scale * 10.0;
            exp10 = exp10 + 1;
        }
        value = value / scale;
    } else {
        while (exp10 > 0) {
            scale = scale * 10.0;
            exp10 = exp10 - 1;
        }
        value = value * scale;
    }
    if (end != 0) *end = p;
    return neg ? 0.0 - value : value;
}

float strtof(char *s, char **end) { return (float)strtod(s, end); }

double atof(char *s) { return strtod(s, 0); }
