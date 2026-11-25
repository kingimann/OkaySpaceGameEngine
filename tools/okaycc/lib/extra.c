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
