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
    while (s[i] == ' ' || (s[i] >= 9 && s[i] <= 13)) i = i + 1; /* as strtol (7.22.1.2): any white space, a sign */
    if (s[i] == '-' || s[i] == '+') {
        neg = s[i] == '-';
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
/* strtoul and strtol (ISO C 7.22.1.4): leading white space, an optional sign, then digits in base 2
 * to 36 (base 0: 0x for 16, a leading 0 for 8, else 10; base 16 also takes 0x). An out-of-range value
 * gives ULONG_MAX, LONG_MAX or LONG_MIN (errno is not set: there is no errno yet). *end is the first
 * character not used, or s when there is no number. */
static int digitOf(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}
static unsigned long scanInt(char *s, char **end, int base, int *neg, int *over) {
    int i;
    unsigned long v;
    int any;
    unsigned long limit;
    i = 0;
    v = 0;
    any = 0;
    *neg = 0;
    *over = 0;
    while (s[i] == ' ' || (s[i] >= 9 && s[i] <= 13)) i = i + 1;
    if (s[i] == '-' || s[i] == '+') {
        *neg = s[i] == '-';
        i = i + 1;
    }
    if ((base == 0 || base == 16) && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X') && digitOf(s[i + 2]) < 16) {
        base = 16;
        i = i + 2;
    } else if (base == 0 && s[i] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    limit = (unsigned long)-1 / (unsigned long)base;
    while (digitOf(s[i]) < base) {
        unsigned long d;
        d = (unsigned long)digitOf(s[i]);
        if (v > limit || (v == limit && d > (unsigned long)-1 % (unsigned long)base)) *over = 1;
        else v = v * (unsigned long)base + d;
        any = 1;
        i = i + 1;
    }
    if (end) *end = any ? s + i : s;
    return v;
}
unsigned long strtoul(char *s, char **end, int base) {
    int neg;
    int over;
    unsigned long v;
    v = scanInt(s, end, base, &neg, &over);
    if (over) return (unsigned long)-1;
    return neg ? (unsigned long)0 - v : v;
}
long strtol(char *s, char **end, int base) {
    int neg;
    int over;
    unsigned long v;
    unsigned long lim;
    v = scanInt(s, end, base, &neg, &over);
    lim = neg ? (unsigned long)1 << 63 : ((unsigned long)1 << 63) - 1;
    if (over || v > lim) return neg ? (long)((unsigned long)1 << 63) : (long)lim;
    return neg ? (long)((unsigned long)0 - v) : (long)v;
}
long long strtoll(char *s, char **end, int base) { return strtol(s, end, base); }
unsigned long long strtoull(char *s, char **end, int base) { return strtoul(s, end, base); }
long atol(char *s) { return strtol(s, 0, 10); }
long long atoll(char *s) { return strtol(s, 0, 10); }
/* strtod, strtof and atof (ISO C 7.22.1.3, 7.22.1.2): leading white space, a sign, then decimal digits
 * with an optional point and exponent, or "inf", "infinity" or "nan" (any case). Our own code. The
 * first 19 significant digits are kept as an integer m and the rest only move the decimal exponent e;
 * the value is m * 10^e. When m < 2^53 and |e| <= 22 both factors are exact doubles, so one IEEE
 * multiplication or division rounds correctly (Clinger, "How to read floating point numbers
 * accurately", 1990, the fast path). Outside it the power is built from exact steps of 10^22 and the
 * result can be one unit in the last place away from the correctly rounded value. Hexadecimal
 * floating input (0x1.8p3) is not read. */
static double tenPow(int n) {
    double p;
    p = 1.0;
    while (n > 0) {
        p = p * 10.0;
        n = n - 1;
    }
    return p;
}
static int lowerIs(char *s, char *word) {
    int i;
    for (i = 0; word[i] != 0; i = i + 1) {
        char c;
        c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != word[i]) return 0;
    }
    return 1;
}
double strtod(char *s, char **end) {
    int i;
    int neg;
    int any;
    int digits;
    int e;
    int ex;
    int exNeg;
    int j;
    unsigned long m;
    double v;
    i = 0;
    while (s[i] == ' ' || (s[i] >= 9 && s[i] <= 13)) i = i + 1;
    neg = 0;
    if (s[i] == '-' || s[i] == '+') {
        neg = s[i] == '-';
        i = i + 1;
    }
    if (lowerIs(s + i, "inf")) {
        i = i + (lowerIs(s + i, "infinity") ? 8 : 3);
        if (end) *end = s + i;
        v = 1.0 / 0.0;
        return neg ? -v : v;
    }
    if (lowerIs(s + i, "nan")) {
        if (end) *end = s + i + 3;
        v = 0.0 / 0.0;
        return neg ? v : -v;
    }
    m = 0;
    any = 0;
    digits = 0;
    e = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        any = 1;
        if (digits < 19) {
            m = m * 10 + (unsigned long)(s[i] - '0');
            if (m != 0) digits = digits + 1;
        } else {
            e = e + 1;
        }
        i = i + 1;
    }
    if (s[i] == '.') {
        i = i + 1;
        while (s[i] >= '0' && s[i] <= '9') {
            any = 1;
            if (digits < 19) {
                m = m * 10 + (unsigned long)(s[i] - '0');
                if (m != 0) digits = digits + 1;
                e = e - 1;
            }
            i = i + 1;
        }
    }
    if (!any) {
        if (end) *end = s;
        return 0.0;
    }
    if (s[i] == 'e' || s[i] == 'E') {
        j = i + 1;
        exNeg = 0;
        if (s[j] == '-' || s[j] == '+') {
            exNeg = s[j] == '-';
            j = j + 1;
        }
        if (s[j] >= '0' && s[j] <= '9') {
            ex = 0;
            while (s[j] >= '0' && s[j] <= '9') {
                if (ex < 100000) ex = ex * 10 + (s[j] - '0');
                j = j + 1;
            }
            e = exNeg ? e - ex : e + ex;
            i = j;
        }
    }
    if (end) *end = s + i;
    v = (double)m;
    if (m == 0) {
        v = 0.0;
    } else if (e >= 0) {
        while (e > 22) {
            v = v * 1e22;
            e = e - 22;
        }
        v = v * tenPow(e);
    } else {
        e = -e;
        while (e > 22) {
            v = v / 1e22;
            e = e - 22;
        }
        v = v / tenPow(e);
    }
    return neg ? -v : v;
}
float strtof(char *s, char **end) { return (float)strtod(s, end); }
double atof(char *s) { return strtod(s, 0); }
/* getenv (ISO C 7.22.4.6). Our program start does not keep envp, so the environment is read once
 * from /proc/self/environ (Linux proc(5): NAME=value strings, each ended by a zero byte) with the
 * system calls open 2, read 0 and close 3, into a fixed buffer. */
long syscall(long n, long a, long b, long c);
static char gEnv[65536];
static int gEnvLen = -1;
char *getenv(char *name) {
    int i;
    int k;
    long fd;
    long r;
    if (gEnvLen < 0) {
        gEnvLen = 0;
        fd = syscall(2, (long)"/proc/self/environ", 0, 0);
        if (fd >= 0) {
            for (;;) {
                if (gEnvLen >= 65535) break;
                r = syscall(0, fd, (long)(gEnv + gEnvLen), (long)(65535 - gEnvLen));
                if (r <= 0) break;
                gEnvLen = gEnvLen + (int)r;
            }
            syscall(3, fd, 0, 0);
        }
        gEnv[gEnvLen] = 0;
    }
    i = 0;
    while (i < gEnvLen) {
        k = 0;
        while (name[k] != 0 && gEnv[i + k] == name[k]) k = k + 1;
        if (name[k] == 0 && gEnv[i + k] == '=') return gEnv + i + k + 1;
        while (i < gEnvLen && gEnv[i] != 0) i = i + 1;
        i = i + 1;
    }
    return 0;
}

/* errno (ISO C 7.5): one int for the program (one thread). <cerrno> makes errno (*__okay_errno()). */
int __okay_errno_value;
int *__okay_errno(void) { return &__okay_errno_value; }
